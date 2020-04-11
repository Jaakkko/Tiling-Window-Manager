//
// Created by jaakko on 10.4.2020.
//

#include <stdio.h>
#include <stdarg.h>

void logmsg(const char* text, ...) {
    va_list args;
    va_start(args, text);

    FILE* f = fopen("/tmp/wm.log", "a+");
    vfprintf(f, text, args);
    fprintf(f, "\n");
    fclose(f);

    va_end(args);
}
