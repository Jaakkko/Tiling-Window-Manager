//
// Created by jaakko on 29.4.2020.
//

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <string.h>

#include "bar.h"
#include "../instance.h"
#include "../config.h"
#include "../logger.h"

XftFont* font;
XftColor textUnselectedColor;
XftColor textSelectedColor;
XftDraw* draw;

GC gc;

static void generate_color(unsigned color, XftColor* dest) {
    XRenderColor c;
    c.alpha   = (color >> 16U) & 0xFF00U;
    c.red     = (color >> 8U) & 0xFF00U;
    c.green   = color & 0xFF00U;
    c.blue    = (color << 8) & 0xFF00U;
    XftColorAllocValue(wmDisplay, wmVisual, wmColormap, &c, dest);
}

static void drawWorkspaces() {
    XClearArea(wmDisplay, wmBarWindow, 0, 0, wmBarHeight * WORKSPACE_COUNT, wmBarHeight, 0);

    int x = 0;
    int ty = (wmBarHeight - (font->ascent + font->descent)) / 2 + font->ascent;
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        const int selected = i == wmActiveWorkspace;
        if (!selected && !wmWorkspaces[i].layout) {
            continue;
        }

        XSetForeground(wmDisplay, gc, selected ? barBackgroundSelected : barBackgroundUnselected);
        XFillRectangle(wmDisplay, wmBarWindow, gc, x, 0, wmBarHeight, wmBarHeight);

        char s[2];
        XGlyphInfo ext;
        XftTextExtentsUtf8(wmDisplay, font, (const FcChar8*)s, 1, &ext);
        sprintf(s, "%i", i + 1);
        int tx = x + (wmBarHeight - ext.width) / 2;
        XftDrawStringUtf8(draw, selected ? &textSelectedColor : &textUnselectedColor, font, tx, ty, (const FcChar8 *)s, 1);

        x += wmBarHeight;
    }
}

void wmCreateBar() {
    font = XftFontOpenName(wmDisplay, DefaultScreen(wmDisplay), barFont);
    if (!font) {
        logmsg("Couldn't load font");
    }
    wmBarHeight = 2 * barPadding + font->ascent + font->descent;

    XSetWindowAttributes attrs;
    attrs.colormap = wmColormap;
    attrs.border_pixel = 0;
    attrs.background_pixel = barBackground;
    attrs.override_redirect = 1;

    wmBarWindow = XCreateWindow(
            wmDisplay, wmRoot,
            0, bottomBar ? (wmScreenHeight - wmBarHeight) : 0,
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
    XRaiseWindow(wmDisplay, wmBarWindow);

    generate_color(barForegroundSelected, &textSelectedColor);
    generate_color(barForegroundUnselected, &textUnselectedColor);
    draw = XftDrawCreate(wmDisplay, wmBarWindow, wmVisual, wmColormap);

    gc = XCreateGC(wmDisplay, wmBarWindow, 0, NULL);
}

void wmUpdateBar() {
    drawWorkspaces();
}

void wmDestroyBar() {
    XftColorFree(wmDisplay, wmVisual, wmColormap, &textUnselectedColor);
    XftColorFree(wmDisplay, wmVisual, wmColormap, &textSelectedColor);
    XftDrawDestroy(draw);
    XftFontClose(wmDisplay, font);
    XFreeGC(wmDisplay, gc);
    XDestroyWindow(wmDisplay, wmBarWindow);
}
