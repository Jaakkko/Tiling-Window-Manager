//
// Created by jaakko on 29.4.2020.
//

#ifndef WM_BLOCK_H
#define WM_BLOCK_H

typedef struct wmBlock wmBlock;
struct wmBlock {
    unsigned (*source)(char* buffer, unsigned bufferSize);
    const char* longest; // Calculate block width with this
    unsigned width;
    unsigned lenBytes; // Current length. Does not include \0
    char* text;
    unsigned bufferLength;
    unsigned dirty;
};

unsigned datetime(char*, unsigned);
unsigned disk(char*, unsigned);
unsigned temperature(char*, unsigned);
unsigned cpu(char*, unsigned);
unsigned ram(char*, unsigned);

#endif //WM_BLOCK_H
