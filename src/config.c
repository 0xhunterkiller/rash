#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <tomlc17.h>
#include <string.h>
#include <stdlib.h>

void _rashconf_pop_sysuser(conf *config, toml_result_t result)
{
    toml_datum_t config_sysuser = toml_get(result.toptab, "sysuser");

    if (config_sysuser.type == TOML_STRING)
    {
        config->sysuser = malloc(sizeof(char) * (strlen(config_sysuser.u.s) + 1));
        strcpy(config->sysuser, config_sysuser.u.s);
    }
    else
    {
        config->sysuser = NULL;
    }
}

void _rashconf_pop_whitelist(conf *config, toml_result_t result)
{
    toml_datum_t whitelist = toml_get(result.toptab, "whitelist");

    if (whitelist.type == TOML_ARRAY)
    {
        config->wl = malloc(whitelist.u.arr.size * sizeof(char *));
        
        int wlc = 0;
        for (int i = 0; i < whitelist.u.arr.size; i++)
        {
            toml_datum_t command = whitelist.u.arr.elem[i];
            if (command.type == TOML_STRING)
            {
                config->wl[wlc] = malloc((strlen(command.u.s) + 1) * sizeof(char));
                strcpy(config->wl[wlc], command.u.s);
                wlc+=1;
            }
        }
        config->wl_size = wlc;
    }
    else
    {
        config->wl = NULL;
        config->wl_size = 0;
    }
}

void _rashconf_pop_allowedenv(conf *config, toml_result_t result)
{
    toml_datum_t env_specs = toml_get(result.toptab, "env");

    if (env_specs.type == TOML_ARRAY)
    {

        char **env_names = malloc((env_specs.u.arr.size) * sizeof(char *));
        char **env_values = malloc((env_specs.u.arr.size) * sizeof(char *));

        int ec = 0;

        for (int i = 0; i < env_specs.u.arr.size; i++)
        {
            toml_datum_t envvarname = env_specs.u.arr.elem[i];
            if (envvarname.type == TOML_STRING)
            {
                char *tmp = getenv(envvarname.u.s);
                if (tmp == NULL || strcmp(envvarname.u.s, "PATH") == 0)
                    continue;
                env_names[ec] = malloc((strlen(envvarname.u.s) + 1) * sizeof(char));
                env_values[ec] = malloc((strlen(tmp) + 1) * sizeof(char));
                strcpy(env_names[ec], envvarname.u.s);
                strcpy(env_values[ec], tmp);
                ec += 1;
            }
        }
        config->env_count = ec;
        config->env_names = env_names;
        config->env_values = env_values;
    }
    else
    {
        config->env_count = 0;
        config->env_names = NULL;
        config->env_values = NULL;
    }
}

void _rashconf_pop_path(conf *config, toml_result_t result)
{
    toml_datum_t path_spec = toml_get(result.toptab, "syspath");
    if (path_spec.type == TOML_STRING)
    {
        config->path = malloc(sizeof(char) * (strlen(path_spec.u.s) + 1));
        strcpy(config->path, path_spec.u.s);
    }
    else
    {
        config->path = NULL;
    }
}

conf parse_config(char *configfilepath)
{
    conf rash_config = {0};

    FILE *fp = fopen(configfilepath, "r");
    toml_result_t result = toml_parse_file(fp);
    if (fp != NULL)
        fclose(fp);

    rash_config.parse_success = true;
    strcpy(rash_config.parser_feedback, "");

    if (!result.ok)
    {
        rash_config.parse_success = result.ok;
        strcpy(rash_config.parser_feedback, result.errmsg);
        toml_free(result);
        return rash_config;
    }

    // Get SysUser
    _rashconf_pop_sysuser(&rash_config, result);

    // Get Whitelist
    _rashconf_pop_whitelist(&rash_config, result);

    // Get Allowed Env
    _rashconf_pop_allowedenv(&rash_config, result);

    // Get Path
    _rashconf_pop_path(&rash_config, result);

    toml_free(result);
    return rash_config;
}

void free_config(conf rash_config)
{
    if (rash_config.sysuser != NULL)
    {
        free(rash_config.sysuser);
    }

    if (rash_config.path != NULL)
    {
        free(rash_config.path);
    }

    for (int i = 0; i < rash_config.wl_size; i++)
    {
        free(rash_config.wl[i]);
    }
    free(rash_config.wl);
    
    for (int i = 0; i < rash_config.env_count; i++)
    {
        free(rash_config.env_names[i]);
        free(rash_config.env_values[i]);
    }
    free(rash_config.env_names);
    free(rash_config.env_values);
}