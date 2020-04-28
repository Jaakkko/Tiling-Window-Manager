//
// Created by jaakko on 27.4.2020.
//

#ifndef WM_TYPES_H
#define WM_TYPES_H

typedef struct wmWindow wmWindow;
typedef struct wmNode wmNode;
typedef struct wmDialog wmDialog;

typedef struct {
    wmWindow* activeWindow;
    wmNode* layout;
    wmNode* splitNode;
    unsigned showSplitBorder;
} wmWorkspace;

struct wmDialog {
    wmWindow* window;
    int x;
    int y;
    unsigned width;
    unsigned height;
    wmDialog* next;
};

typedef enum { HORIZONTAL, VERTICAL, NONE } wmSplitMode;

struct wmWindow {
    wmWindow* next;
    wmWindow* previous;
    Window window;
    Window frame;
    unsigned workspaces;
    wmDialog* dialog;
};

struct wmNode {
    unsigned numChildren;
    wmNode* nodes;
    wmWindow* window;
    wmSplitMode orientation;
    float weight;
    int x;
    int y;
    unsigned width;
    unsigned height;
};

#endif //WM_TYPES_H
