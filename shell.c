#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>

int main(int argc, char *argv[]) {
    
    FILE *f = fopen("rash.conf", "r");
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
            exit(1);
        }
    }
    
    fclose(f);

    pid_t pid = fork();

    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("rash");
        exit(errno == ENOENT ? 127 : 126);
    } else if (pid > 0) {
        int status;
        wait(&status);
        exit(WEXITSTATUS(status));
    } else {
        exit(1);
    }
    return 0;
}
