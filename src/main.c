#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sched.h>
#include "config.h"
#include "security.h"
#include "logging.h"

// default stack size = 8MB
#define CONTAINER_STACK_SIZE (1024 * 1024 * 8)
#define RASH_EXIT_FAILURE 125

extern char **environ;

int rashproc(void *arg)
{
    conf *rash_config = (conf *)arg;

    environ = NULL;
    setenvforchild(rash_config->env_count, rash_config->env_names, rash_config->env_values);
    if (rash_config->path != NULL)
    {
        setpathforchild(rash_config->path);
        execvp(rash_config->command, rash_config->command_args);
    }
    else
    {
        execv(rash_config->command, rash_config->command_args);
    }
    perror("rash");
    exit(errno == ENOENT ? 127 : 126);
}

int main(int argc, char *argv[])
{

    int retval = RASH_EXIT_FAILURE;
    char workdir[512], configfile[512];

    if (argc < 2)
    {
        fprintf(stderr, "USAGE: rash <command>\n");
        goto clear_0;
    }

    // set working directory
    strcpy(workdir, "/etc/rash");

    // set config file path
    strcpy(configfile, workdir);
    strcat(configfile, "/rash.toml");

    // parse the config file
    conf rash_config = parse_config(configfile);
    if (!rash_config.parse_success)
    {
        fprintf(stderr, "config failed to load: %s\n", rash_config.parser_feedback);
        goto clear_1;
    }

    // log: open log file securely
    int log_fd = open(rash_config.logfilepath, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (log_fd < 0)
    {
        fprintf(stderr, "coudn't open log file\n");
        fprintf(stderr, "as uid=%d gid=%d\n", getuid(), getgid());
        perror("rash");
        goto clear_1;
    }
    FILE *log = fdopen(log_fd, "ae");
    if (log == NULL)
    {
        fprintf(stderr, "coudn't open log file\n");
        fprintf(stderr, "as uid=%d gid=%d\n", getuid(), getgid());
        perror("rash");
        goto clear_1;
    }

    // security: check if user === user named in config
    if (check_user(rash_config.sysuser) < 0)
    {
        write_log(log, "check user failed");
        goto clear_2;
    }

    // security: check whitelist to see if the command is allowed
    if (check_whitelist(&rash_config, argv[1]))
    {
        fprintf(stderr, "this command is not allowed: %s\n", argv[1]);
        write_log(log, "tried to run an unknown command: %s", argv[1]);
        goto clear_2;
    }

    // clone and run the command
    rash_config.command = argv[1];
    rash_config.command_args = &argv[1];

    if (rash_config.path == NULL && rash_config.command[0] != '/')
    {
        write_log(log, "PATH was NULL, non-absolute path command was rejected -- please check config!");
        fprintf(stderr, "PATH is NULL, and command is not an absolute path\n");
        goto clear_2;
    }

    char *c_stack = malloc(CONTAINER_STACK_SIZE);
    int status;
    if (c_stack == NULL)
        goto clear_all;

    pid_t childpid = clone(rashproc, c_stack + CONTAINER_STACK_SIZE, SIGCHLD, (void *)&rash_config);

    if (childpid == -1)
    {
        write_log(log, "failed to clone a new process");
        perror("rash");
        goto clear_all;
    }

    // handle exit
    if (waitpid(childpid, &status, 0) == -1)
    {
        write_log(log, "waitpid failed");
        perror("rash");
        fprintf(stderr, "waitpid failed\n");
        goto clear_all;
    }

    if (WIFEXITED(status))
    {
        retval = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        retval = 128 + WTERMSIG(status);
    }
    else
    {
        retval = EXIT_FAILURE; // catch all
    }

    write_log(log, "ran %s with exit status %d", argv[1], retval);

clear_all:
    free(c_stack);
clear_2:
    fclose(log);
    close(log_fd);
clear_1:
    free_config(rash_config);
clear_0:
    return retval;
}
