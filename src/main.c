#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#include "config.h"
#include "security.h"
#include "logging.h"

extern char **environ;

int main(int argc, char *argv[]) {
    
    int retval = EXIT_FAILURE;
    char workdir[512], configfile[512], logfile[512];

    if (argc < 2) {
        fprintf(stderr, "USAGE: rash <command>\n");
        goto clear_0;
    }
    
    // set working directory
    strcpy(workdir, "/etc/rash");

    // set config file path
    strcpy(configfile, workdir);
    strcat(configfile, "/rash.toml");

    // set log file path
    strcpy(logfile, "/tmp/rash.log");

    // parse the config file
    conf rash_config = parse_config(configfile);
    if (!rash_config.parse_success) {
        fprintf(stderr, "config failed to load: %s\n", rash_config.parser_feedback);
        goto clear_1;
    }
    
    // log: open log file securely
    int fd = open(logfile, O_WRONLY | O_CREAT | O_NOFOLLOW, 0644);
    if (fd < 0) {
        fprintf(stderr, "coudn't open log file\n");
        fprintf(stderr, "as uid=%d gid=%d\n", getuid(), getgid());
        perror("rash");
        goto clear_1;
    }
    FILE *log = fdopen(fd, "a");
    if (log == NULL) {
        fprintf(stderr, "coudn't open log file\n");
        fprintf(stderr, "as uid=%d gid=%d\n", getuid(), getgid());
        perror("rash");
        goto clear_1;
    }
    
    // security: check if user === user named in config
    if(check_user(rash_config.sysuser) < 0)
    {
        write_log(log, "check user failed");
        goto clear_2;
    }
    
    // security: check whitelist to see if the command is allowed
    if(check_whitelist(&rash_config, argv[1]))
    {
        fprintf(stderr, "this command is not allowed: %s\n", argv[1]);
        write_log(log, "tried to run an unknown command: %s", argv[1]);
        goto clear_2;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        environ = NULL;
        setenvforchild(rash_config.env_count, rash_config.env_names, rash_config.env_values);
        setpathforchild("/usr/bin:/usr/local/bin");
        execvp(argv[1], &argv[1]);
        perror("rash");
        exit(errno == ENOENT ? 127 : 126);
    } 
    else if (pid > 0)
    {
        int status;
        waitpid(pid, &status, 0);   

        if(WIFEXITED(status)) {
            retval = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            retval = 128 + WTERMSIG(status);
        } else {
            retval = EXIT_FAILURE; // catch all
        } 

        write_log(log, "ran %s with exit status %d", argv[1], retval);
        goto clear_all;
    }
    else
    {
        goto clear_all;
    }

    clear_all:
    clear_2:
        fclose(log);
    clear_1:
        free_config(rash_config);
    
    clear_0:
        return retval;
}
