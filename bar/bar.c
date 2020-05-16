//
// Created by jaakko on 29.4.2020.
//

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <zconf.h>

#include "bar.h"
#include "../instance.h"
#include "../config.h"
#include "../logger.h"
#include "../util.h"

Window wmBarWindow;

unsigned wmBarHeight;

static XftFont* font;
static XftColor textUnselectedColor;
static XftColor textSelectedColor;
static XftColor textBlockColor;
static XftDraw* draw;

static GC gc;

static int textY;

static pthread_mutex_t blocksLock;
static pthread_t blocksThread;
static unsigned running = 1;
static char* tmpBlock = NULL;

static inline int textWidth(const char* text, unsigned len) {
    XGlyphInfo ext;
    XftTextExtentsUtf8(wmDisplay, font, (const FcChar8*)text, len, &ext);
    return ext.xOff;
}

static void generate_color(unsigned color, XftColor* dest) {
    XRenderColor c;
    c.alpha   = (color >> 16U) & 0xFF00U;
    c.red     = (color >> 8U) & 0xFF00U;
    c.green   = color & 0xFF00U;
    c.blue    = (color << 8) & 0xFF00U;
    XftColorAllocValue(wmDisplay, wmVisual, wmColormap, &c, dest);
}

static void* blocksUpdater() {
    while (running) {
        clock_t start = clock();

        int changed = 0;
        for (int i = 0; i < LENGTH(blocks); i++) {
            wmBlock* block = blocks + i;
            unsigned bytes = block->source(tmpBlock, block->bufferLength);

            if (strcmp(tmpBlock, block->text)) {
                changed = 1;
                pthread_mutex_lock(&blocksLock);
                strcpy(block->text, tmpBlock);
                block->lenBytes = bytes;
                block->dirty = 1;
                pthread_mutex_unlock(&blocksLock);
            }
        }

        if (changed) {
            XEvent event;
            event.type = Expose;
            event.xexpose.window = wmBarWindow;

            XLockDisplay(wmDisplay);
            XSendEvent(wmDisplay, wmBarWindow, True, ExposureMask, &event);
            XFlush(wmDisplay);
            XUnlockDisplay(wmDisplay);
        }

        clock_t end = clock();
        float seconds = (float)(end - start) / CLOCKS_PER_SEC;
        if (seconds < 1) {
            usleep((int)((1 - seconds) * 1E6));
        }
    }

    return NULL;
}

static void drawWorkspaces() {
    XClearArea(wmDisplay, wmBarWindow, 0, 0, wmBarHeight * WORKSPACE_COUNT, wmBarHeight, 0);

    int x = 0;
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        const int selected = i == wmActiveWorkspace;
        if (!selected && !wmWorkspaces[i].layout) {
            continue;
        }

        XSetForeground(wmDisplay, gc, selected ? barBackgroundSelected : barBackgroundUnselected);
        XFillRectangle(wmDisplay, wmBarWindow, gc, x, 0, wmBarHeight, wmBarHeight);

        char s[2];
        sprintf(s, "%i", i + 1);
        int tx = x + (wmBarHeight - textWidth(s, 1)) / 2;
        XftDrawStringUtf8(draw, selected ? &textSelectedColor : &textUnselectedColor, font, tx, textY, (const FcChar8 *)s, 1);

        x += wmBarHeight;
    }
}

static void drawBlocks() {
    int x = wmScreenWidth;
    int i;
    wmBlock* block;
    pthread_mutex_lock(&blocksLock);
    for (i = 0; i < LENGTH(blocks); i++) {
        block = &blocks[i];
        if (!block->dirty) {
            x -= 2 * (blockPadding + blockMargin) + block->width;
            continue;
        }
        block->dirty = 0;
        x -= blockPadding;
        x -= block->width;
        int tw = textWidth(block->text, block->lenBytes);
        int tx = x + (block->width - tw) / 2;
        x -= blockPadding;
        int blockWidth = block->width + 2 * blockPadding;
        XClearArea(wmDisplay, wmBarWindow, x, 0, blockWidth, wmBarHeight, 0);
#ifdef blockBackround
        XSetForeground(wmDisplay, gc, blockBackgroundColor);
        XFillRectangle(wmDisplay, wmBarWindow, gc, x, 0, blockWidth, wmBarHeight);
#endif
        XftDrawStringUtf8(draw, &textBlockColor, font, tx, textY, (const FcChar8*)block->text, block->lenBytes);
        x -= blockMargin;
    }
    pthread_mutex_unlock(&blocksLock);
}

void wmCreateBar() {
    font = XftFontOpenName(wmDisplay, DefaultScreen(wmDisplay), barFont);
    if (!font) {
        logmsg("Couldn't load font");
    }
    wmBarHeight = 2 * barPadding + font->ascent + font->descent;
    textY = (wmBarHeight - (font->ascent + font->descent)) / 2 + font->ascent;

    XSetWindowAttributes attrs;
    attrs.colormap = wmColormap;
    attrs.border_pixel = 0;
    attrs.background_pixel = barBackground;
    attrs.override_redirect = 1;

    wmBarWindow = XCreateWindow(
            wmDisplay, wmRoot, 0,
#ifdef bottomBar
            wmScreenHeight - wmBarHeight,
#else
            0,
#endif
            wmScreenWidth, wmBarHeight,
            0,
            wmDepth,
            InputOutput,
            wmVisual,
            CWColormap | CWBackPixel | CWOverrideRedirect | CWBorderPixel,
            &attrs
    );
    XSelectInput(wmDisplay, wmBarWindow, ExposureMask);
    XMapWindow(wmDisplay, wmBarWindow);

    generate_color(barForegroundSelected, &textSelectedColor);
    generate_color(barForegroundUnselected, &textUnselectedColor);
    generate_color(blockForegroundColor, &textBlockColor);
    draw = XftDrawCreate(wmDisplay, wmBarWindow, wmVisual, wmColormap);

    gc = XCreateGC(wmDisplay, wmBarWindow, 0, NULL);

    int longest;
    int i;
    for (i = 0; i < LENGTH(blocks); i++) {
        wmBlock* block = &blocks[i];
        unsigned maxlen = strlen(block->longest);
        longest = MAX(maxlen, longest);
        block->bufferLength = maxlen + 1;
        block->text = malloc(block->bufferLength);
        block->text[0] = '\0';
        block->lenBytes = 0;
        block->width = blockPadding + textWidth(block->longest, maxlen);
        block->dirty = 1;
    }

    tmpBlock = malloc(longest + 1);

    pthread_create(&blocksThread, NULL, blocksUpdater, NULL);
}

void wmUpdateBar() {
    drawWorkspaces();
    drawBlocks();
}

void wmExposeBar() {
    pthread_mutex_lock(&blocksLock);
    for (int i = 0; i < LENGTH(blocks); i++) {
        wmBlock* block = &blocks[i];
        block->dirty = 1;
    }
    pthread_mutex_unlock(&blocksLock);
}

void wmUpdateBarBounds() {
    wmBarHeight = 2 * barPadding + font->ascent + font->descent;
    textY = (wmBarHeight - (font->ascent + font->descent)) / 2 + font->ascent;

    XMoveResizeWindow(
            wmDisplay, wmBarWindow, 0,
#ifdef bottomBar
            wmScreenHeight - wmBarHeight,
#else
            0,
#endif
            wmScreenWidth, wmBarHeight);
}

void wmDestroyBar() {
    running = 0;
    pthread_join(blocksThread, NULL);

    XftColorFree(wmDisplay, wmVisual, wmColormap, &textUnselectedColor);
    XftColorFree(wmDisplay, wmVisual, wmColormap, &textSelectedColor);
    XftDrawDestroy(draw);
    XftFontClose(wmDisplay, font);
    XFreeGC(wmDisplay, gc);
    XDestroyWindow(wmDisplay, wmBarWindow);

    int i;
    wmBlock* block;
    for (i = 0; i < LENGTH(blocks); i++) {
        block = &blocks[i];
        free(block->text);
    }

    free(tmpBlock);
}
