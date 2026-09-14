#ifndef RASH_CONFIG_H
#define RASH_CONFIG_H

#include <stdbool.h>

typedef struct Configuration {
    bool parse_success;
    char parser_feedback[200];

    char *command;
    char **command_args;

    int wl_size;
    char **wl;

    int env_count;
    char **env_names;
    char **env_values;

    char *path;
    char *logfilepath;

    char *sysuser;
} conf;

conf parse_config(char *configfilepath);

void free_config(conf rash_config);

#endif // RASH_CONFIG_H