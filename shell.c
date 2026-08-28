#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

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

    
    FILE *f = fopen(configfile, "r");
    FILE *log = fopen(logfile, "a");

    char buffer[512];

    if (f == NULL) {
        fprintf(stderr, "rash.conf not found, please create it\n");
        perror("rash");
        return 1;
    } 

    if (argc < 2) {
        fprintf(stderr, "USAGE: rash <command>\n");
        exit(1);
    }
    
    while(fgets(buffer, 512, f) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
        if (strcmp(buffer, argv[1]) == 0) {
            fprintf(stderr, "This command has been blacklisted: %s\n", buffer);
            write_log(log, "tried to run a blacklisted command: %s", buffer);
            exit(1);
        }
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
