#include <stdbool.h>
#include <stdio.h>
#include <tomlc17.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"

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