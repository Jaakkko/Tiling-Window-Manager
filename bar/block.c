//
// Created by jaakko on 29.4.2020.
//

#include "block.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

static unsigned runcmd(char* dst, unsigned len, char* cmd) {
    unsigned read = 0;
    FILE *fp;
    fp = popen(cmd, "r");
    if (fgets(dst, len, fp)) {
        read = strlen(dst);
    }
    pclose(fp);
    return read;
}

unsigned datetime(char* buffer, unsigned bufferLength) {
    time_t timer;
    struct tm* tm_info;

    timer = time(NULL);
    tm_info = localtime(&timer);

    return strftime(buffer, bufferLength, "%d.%m.%Y %H.%M", tm_info);
}

unsigned disk(char* buffer, unsigned bufferLength) {
    return runcmd(buffer, bufferLength, "df -h -P / | awk '/\\/.*/{printf gensub(/([0-9]+)/,\"\\\\1 \",\"g\",$4);}'");
}

unsigned temperature(char* buffer, unsigned bufferLength) {
    return runcmd(buffer, bufferLength, "export POSIXLY_CORRECT=1; sensors -u | awk '/temp1_input/{printf(\"%0.1f °C\", $2)}'");
}

unsigned cpu(char* buffer, unsigned bufferLength) {
    return runcmd(buffer, bufferLength, "export POSIXLY_CORRECT=1; mpstat 1 1 | awk '/\\.*:/{printf(\"%s %\",$3)}'");
}

unsigned ram(char* buffer, unsigned bufferLength) {
    return runcmd(buffer, bufferLength, "export POSIXLY_CORRECT=1; awk '/^MemTotal:/{mt=$2}/^MemAvailable:/{ma=$2}END{used=(mt-ma)/1048576;printf(\"%.1f G\",used)}' /proc/meminfo");
}
