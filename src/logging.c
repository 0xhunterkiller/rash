#include <unistd.h>
#include <stdio.h>
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