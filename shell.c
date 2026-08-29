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

int main(int argc, char *argv[]) {

    // check access and drop if required
    // this prevents accessing resoruces with  
    // sudo or sudo su <higher priviledge user>    
    char *sudo_uid_str = getenv("SUDO_UID");
    char *sudo_gid_str = getenv("SUDO_GID");
    if (sudo_gid_str != NULL && setgid((gid_t)atoi(sudo_gid_str)) < 0) {
        fprintf(stderr, "cannot sudo into higher priviledge, unable to drop priviledges\n");        
        return 1;
    }
    if (sudo_uid_str != NULL && setuid((uid_t)atoi(sudo_uid_str)) < 0) {
        fprintf(stderr, "cannot sudo into higher priviledge, unable to drop priviledges\n");        
        return 1;
    } 
    
    char *home = getenv("HOME");
    if(home == NULL) return 1;

    char workdir[512];
    strcpy(workdir, home);
    strcat(workdir, "/.local/state/rash");   

    char configfile[512];
    strcpy(configfile, workdir);
    strcat(configfile, "/rash.conf");
    
    char logfile[512];
    strcpy(logfile, workdir);
    strcat(logfile, "/rash.log");

    char buffer[512];    
    
    if (argc < 2) {
        fprintf(stderr, "USAGE: rash <command>\n");
        return 1;
    }    
    
    FILE *f = fopen(configfile, "r");
    FILE *log = fopen(logfile, "a");
    
    if (log == NULL) {
        fprintf(stderr, "config path doesn't exist\n");
        perror("rash");
        return 1;
    }

    if (f == NULL) {
        fprintf(stderr, "rash.conf not found, please create it\n");
        perror("rash");
        fclose(log);
        return 1;
    } 

    bool found = false;    
    while(fgets(buffer, 512, f) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
        if (strcmp(buffer, argv[1]) == 0) {
            found = true;
            break;        
        }
    }
    if(!found){
        fprintf(stderr, "This command is not allowed: %s\n", argv[1]);
        write_log(log, "tried to run an unknown command: %s", argv[1]);
        fclose(f);
        fclose(log);
        return 1;
    }
    
    fclose(f);

    int exitcode = 0;

    pid_t pid = fork();

    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("rash");
        exit(errno == ENOENT ? 127 : 126);
    } else if (pid > 0) {
        int status;
        wait(&status);    
        exitcode = WEXITSTATUS(status);
        write_log(log, "ran %s with exit status %d", argv[1], exitcode);
        fclose(log);
        exit(exitcode);
    } else {
        fclose(log);
        exit(1);
    }
    return 0;
}
