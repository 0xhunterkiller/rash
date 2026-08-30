#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

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
