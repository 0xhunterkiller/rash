#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "config.h"
#include "security.h"
#include "logging.h"

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
