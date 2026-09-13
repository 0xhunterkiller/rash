#ifndef RASH_SECURITY_H
#define RASH_SECURITY_H

#include "config.h"

void setenvforchild(int env_count, char **env_names, char **env_values);
void setpathforchild(char *path);
int check_user(const char *sysuser);
int check_whitelist(conf *config, char *command);

#endif // RASH_SECURITY_H