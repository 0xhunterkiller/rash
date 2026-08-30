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
#include <stdint.h>
#include <tomlc17.h>
#include <pwd.h>

// Logging
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

// Configuration
typedef struct Configuration {
    bool parse_success;
    char parser_feedback[200];

    int wl_size;
    char **wl;

    int env_count;
    char **env_names;
    char **env_values;

    char *path;

    char *sysuser;
} conf;

conf parse_config(char *configfilepath) {
    conf rash_config;

    FILE *fp = fopen(configfilepath, "r");
    toml_result_t result = toml_parse_file(fp);
    if (fp != NULL) fclose(fp);
    
    rash_config.parse_success = true;
    strcpy(rash_config.parser_feedback, "");

    if (!result.ok) {
        rash_config.parse_success = result.ok;
        strcpy(rash_config.parser_feedback, result.errmsg);
        return rash_config;
    }

    // Get SysUser
    toml_datum_t config_sysuser = toml_get(result.toptab, "sysuser");
    if(config_sysuser.type == TOML_STRING) {
        rash_config.sysuser = malloc(sizeof(char) * 256);
        strcpy(rash_config.sysuser, config_sysuser.u.s);
    } else {
        rash_config.sysuser = NULL;
    }

    // Get Whitelist
    toml_datum_t whitelist = toml_get(result.toptab, "whitelist");

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
    } else {
        rash_config.wl = NULL;
        rash_config.wl_size = 0;
    }

    // Get Allowed Env
    toml_datum_t env_specs = toml_get(result.toptab, "env");
    
    if(env_specs.type == TOML_ARRAY) {
        int ec = 0;
        char **env_names = malloc((env_specs.u.arr.size) * sizeof(char *));
        char **env_values = malloc((env_specs.u.arr.size) * sizeof(char *));
        for(int i=0;i<env_specs.u.arr.size;i++){
            toml_datum_t envvarname = env_specs.u.arr.elem[i];
            if(envvarname.type == TOML_STRING) {
                char *tmp = getenv(envvarname.u.s);
                if (tmp == NULL || strcmp(envvarname.u.s, "PATH") == 0) continue;
                env_names[ec] = malloc((strlen(envvarname.u.s)+1) * sizeof(char));
                env_values[ec] = malloc((strlen(tmp)+1) * sizeof(char));
                strcpy(env_names[ec], envvarname.u.s);
                strcpy(env_values[ec], tmp);
                ec++;
            }
        }
        rash_config.env_count = ec;
        rash_config.env_names = env_names;
        rash_config.env_values = env_values;
    } else {
        rash_config.env_count = 0;
        rash_config.env_names = NULL;
        rash_config.env_values = NULL;
    }

    toml_free(result);
    return rash_config;
}

void setenvforchild(int env_count, char **env_names, char **env_values){
    if(env_names == NULL) return;
    if(env_values == NULL) return;
    if(env_count <= 0) return;

    for(int i=0;i<env_count;i++) {
        if (env_names[i] == NULL || env_values[i] == NULL) continue;
        setenv(env_names[i], env_values[i], 1);
    }
}

void setpathforchild(char *path){
    setenv("PATH", path, 1);
}

void free_config(conf rash_config) {
    if(rash_config.sysuser != NULL) {
        free(rash_config.sysuser);
    } 

    if (rash_config.wl_size > 0) {
        for(int i=0;i<rash_config.wl_size;i++){
            free(rash_config.wl[i]);
        }
        free(rash_config.wl);
    }

    if (rash_config.env_count > 0){
        for(int i = 0; i < rash_config.env_count; i++) {
            free(rash_config.env_names[i]);
            free(rash_config.env_values[i]);
        }
        free(rash_config.env_names);
        free(rash_config.env_values);
    }
}

// Check User
int check_user(const char *sysuser) {
    if (sysuser == NULL) {
        fprintf(stderr, "please configure a user\n");        
        exit(1);
    }

    struct passwd *pw = getpwnam(sysuser);
    if(pw == NULL) {
        fprintf(stderr, "no such user exists: %s\n", sysuser);        
        exit(1);
    }
    uid_t uid = pw->pw_uid;
    gid_t gid = pw->pw_gid;

    if (uid < 1000 || gid < 1000) {
        fprintf(stderr, "cannot use this user (priviledged user): name: %s, uid: %d, gid: %d\n", sysuser, uid, gid);        
        exit(1);
    }
    if (getuid() != uid || getgid() != gid) {
        fprintf(stderr, "you can only run rash as the user its anchored to [%s]\n", sysuser);        
        exit(1);
    }
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    
    int retval = EXIT_FAILURE;
    
    if (argc < 2) {
        fprintf(stderr, "USAGE: rash <command>\n");
        goto end;
    }
    
    // Establish Working Directory
    char workdir[512];
    strcpy(workdir, "/etc/rash");
    
    // Create and Parse Config
    char configfile[512];
    strcpy(configfile, workdir);
    strcat(configfile, "/rash.toml");
    
    conf rash_config = parse_config(configfile);
    if (!rash_config.parse_success) {
        fprintf(stderr, "config failed to load: %s\n", rash_config.parser_feedback);
        goto end;
    }

    check_user(rash_config.sysuser);
    
    // Logging Init
    char logfile[512];
    strcpy(logfile, "/tmp/rash.log");
    
    FILE *log = fopen(logfile, "a");
    if (log == NULL) {
        fprintf(stderr, "coudn't open log file\n");
        fprintf(stderr, "as uid=%d gid=%d\n", getuid(), getgid());
        perror("rash");
        goto leaveafter_rashconfig;
    }
    
    // Check Whitelist
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
        goto leaveafter_log;
    }

    int exitcode = 0;

    pid_t pid = fork();

    
    if (pid == 0) {
        clearenv();
        setenvforchild(rash_config.env_count, rash_config.env_names, rash_config.env_values);
        setpathforchild("/usr/bin:/usr/local/bin");
        execvp(argv[1], &argv[1]);
        perror("rash");
        exit(errno == ENOENT ? 127 : 126);
    } else if (pid > 0) {
        int status;
        wait(&status);    
        retval = WEXITSTATUS(status);
        write_log(log, "ran %s with exit status %d", argv[1], retval);
        goto leaveafter_all;
    } else {
        goto leaveafter_all;
    }

    retval = EXIT_SUCCESS;

    leaveafter_all:
    leaveafter_log:
        fclose(log);
    
    leaveafter_rashconfig:
        free_config(rash_config);
    
    end:
        return retval;
}
