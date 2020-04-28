//
// Created by jaakko on 27.4.2020.
//

#ifndef WM_TREE_H
#define WM_TREE_H

#include <X11/X.h>

#include "types.h"

/*
 * Rules:
 *  If node is not a window it must have two or more children.
 *  If node is a window it does not have any children.
 */

wmNode newNode(wmWindow* window);
wmNode* addNode(wmNode* parent, wmNode child, wmSplitMode splitOrientation);
wmNode* removeNode(wmNode* layout, wmNode* node);

wmNode* findNode(wmNode* layout, wmWindow* window);
wmNode* findParent(wmNode* layout, wmNode* child);

int indexOf(wmNode* layout, wmNode* child, wmNode** parent, int* index);

void swap(wmNode* parent, int aIndex, int bIndex);

void freeTree(wmNode* layout);

#endif //WM_TREE_H
