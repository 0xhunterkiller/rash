#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <sys/types.h>
#include <tomlc17.h>

void write_log(FILE *log, char *msg, ...){
    if (log == NULL) return;
    
    time_t epoch = time(NULL);

    fprintf(log, "%ld ", (long)epoch);

    va_list args;
    va_start(args, msg);
    vfprintf(log, msg, args);
    va_end(args);

    fprintf(log, "\n");

    fflush(log);
}

typedef struct Configuration {
    bool parse_success;
    char parser_feedback[200];

    int wl_size;
    char **wl;

    int env_count; // list of env vars specified excluding PATH and NULL
    char **allowed_env; // list of valid env vars + constructed PATH + NULL
} conf;

conf parse_config(FILE *fp) {
    conf rash_config;
    toml_result_t result = toml_parse_file(fp);

    rash_config.parse_success = true;
    strcpy(rash_config.parser_feedback, "");

    if (!result.ok) {
        rash_config.parse_success = result.ok;
        strcpy(rash_config.parser_feedback, result.errmsg);
        return rash_config;
    }

    // Get Whitelist
    toml_datum_t whitelist = toml_get(result.toptab, "whitelist");
    if(whitelist.type == TOML_UNKNOWN) {
        rash_config.wl = NULL;
        rash_config.wl_size = 0;
    }

    if(whitelist.type == TOML_ARRAY) {
        rash_config.wl = malloc(whitelist.u.arr.size * sizeof(char *));
        rash_config.wl_size = whitelist.u.arr.size;
        for(int i=0;i<whitelist.u.arr.size;i++) {
            toml_datum_t command = whitelist.u.arr.elem[i];
            if (command.type == TOML_STRING) {
                rash_config.wl[i] = malloc((strlen(command.u.s)+1) * sizeof(char));
                strcpy(rash_config.wl[i], command.u.s);
            }
        }
    }

    // Set PATH Variable
    char *pathenv = "PATH=/usr/local/bin:/usr/bin";
    
    // Get Allowed Env
    int envvarlen = 0;
    toml_datum_t env_specs = toml_get(result.toptab, "env");

    if(env_specs.type == TOML_ARRAY) {
        rash_config.env_count = 0;
        char **valid_envvars = malloc((env_specs.u.arr.size+2) * sizeof(char *));
        for(int i=0;i<env_specs.u.arr.size;i++){
            toml_datum_t envvarname = env_specs.u.arr.elem[i];
            if(envvarname.type == TOML_STRING) {
                char *tmp = getenv(envvarname.u.s);
                if (tmp == NULL || strcmp(envvarname.u.s, "PATH") == 0) continue;
                envvarlen = strlen(tmp)+strlen(envvarname.u.s)+2; // accommodate \0 and the =
                valid_envvars[rash_config.env_count] = malloc(envvarlen * sizeof(char));
                snprintf(valid_envvars[rash_config.env_count], envvarlen, "%s=%s", envvarname.u.s, tmp);
                rash_config.env_count += 1;
            }
        }
        valid_envvars[rash_config.env_count] = pathenv;
        valid_envvars[rash_config.env_count+1] = NULL;
        rash_config.allowed_env = valid_envvars;
    } else {
        rash_config.allowed_env = malloc(sizeof(char *) * 2);
        rash_config.allowed_env[0] = pathenv;
        rash_config.allowed_env[1] = NULL;
        rash_config.env_count = 0;
    }

    toml_free(result);
    return rash_config;
}

void free_config(conf rash_config) {
    if (rash_config.wl_size > 0) {
        for(int i=0;i<rash_config.wl_size;i++){
            free(rash_config.wl[i]);
        }
        free(rash_config.wl);
    }

    if (rash_config.env_count > 0){
        for(int i = 0; i < rash_config.env_count; i++) {
            free(rash_config.allowed_env[i]);
        }
    }
    free(rash_config.allowed_env);
}

void cleanup(FILE *f, FILE *log, conf rash_config){
    if (f != NULL) fclose(f);
    if (log != NULL) fclose(log);
    free_config(rash_config);
}

int main(int argc, char *argv[]) {

    // check access and drop if required
    // this prevents accessing resoruces with  
    // sudo or sudo su <higher priviledge user>    
    char *sudo_uid_str = getenv("SUDO_UID");
    char *sudo_gid_str = getenv("SUDO_GID");
    if (sudo_gid_str != NULL && setgid((gid_t)atoi(sudo_gid_str)) < 0) {
        fprintf(stderr, "cannot sudo into higher priviledge, unable to drop priviledges\n");        
        goto exit_failed;
    }
    if (sudo_uid_str != NULL && setuid((uid_t)atoi(sudo_uid_str)) < 0) {
        fprintf(stderr, "cannot sudo into higher priviledge, unable to drop priviledges\n");        
        goto exit_failed;
    } 
    
    char *home = getenv("HOME");
    if(home == NULL) goto exit_failed;

    char workdir[512];
    strcpy(workdir, home);
    strcat(workdir, "/.local/state/rash");   

    char configfile[512];
    strcpy(configfile, workdir);
    strcat(configfile, "/rash.toml");
    
    char logfile[512];
    strcpy(logfile, workdir);
    strcat(logfile, "/rash.log");

    char buffer[512];    
    
    if (argc < 2) {
        fprintf(stderr, "USAGE: rash <command>\n");
        goto exit_failed;
    }    
    
    FILE *f = fopen(configfile, "r");
    FILE *log = fopen(logfile, "a");
    
    if (log == NULL) {
        fprintf(stderr, "config path doesn't exist\n");
        perror("rash");
        goto exit_failed;
    }

    if (f == NULL) {
        fprintf(stderr, "rash.toml not found, please create it\n");
        perror("rash");
        goto exit_failed;
    }

    conf rash_config = parse_config(f);
    
    if (!rash_config.parse_success) {
        fprintf(stderr, "config failed to load: %s\n", rash_config.parser_feedback);
        return EXIT_FAILURE;
    }
    
    bool found = false;
    if (rash_config.wl_size > 0) {
        for(int i=0;i<rash_config.wl_size;i++){
            if (strcmp(rash_config.wl[i], argv[1]) == 0) {
                found = true;
                break;
            }
        }
    }
        
    if(!found){
        fprintf(stderr, "this command is not allowed: %s\n", argv[1]);
        write_log(log, "tried to run an unknown command: %s", argv[1]);
        goto exit_failed;
    }

    int exitcode = 0;

    pid_t pid = fork();

    if (pid == 0) {
        execve(argv[1], &argv[1], rash_config.allowed_env);
        perror("rash");
        exit(errno == ENOENT ? 127 : 126);
    } else if (pid > 0) {
        int status;
        wait(&status);    
        exitcode = WEXITSTATUS(status);
        write_log(log, "ran %s with exit status %d", argv[1], exitcode);
        cleanup(f, log, rash_config);
        exit(exitcode);
    } else {
        free_config(rash_config);
        cleanup(f, log, rash_config);
        exit(1);
    }
    
    

    exit_failed:
        cleanup(f, log, rash_config);    
        return EXIT_FAILURE;

    
    return 0;
}
