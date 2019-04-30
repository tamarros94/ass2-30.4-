#include "kthread.h"

typedef struct trnmnt_trees {
    int *nodes;
} trnmnt_tree;

trnmnt_tree* trnmnt_tree_alloc(int depth);

int trnmnmt_tree_dealloc(trnmnt_tree *tree);

int trnmnt_tree_acquire(trnmnt_tree *tree, int ID);

int trnmnt_tree_release(trnmnt_tree *, int ID);