//
// Created by rosent@wincs.cs.bgu.ac.il on 4/30/19.
//

#include "tournament_tree.h"
#include "kthread.h"
#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"

int
power(int b, int pwr) {
    int i;
    int acc = 1;
    for (i = 0; i < pwr; i++) {
        acc = acc * b;
    }
    return acc;
}

trnmnt_tree *trnmnt_tree_alloc(int depth) {
    if (depth < 1)return 0;
    trnmnt_tree *tree = malloc(sizeof(trnmnt_tree));
    tree->nodes = malloc(sizeof(int) * (power(2, depth)));
    tree->depth = depth;
    tree->nodes_num = power(2, depth);

    for (int i = 0; i < tree->nodes_num; i++) {
        int mutex_id = kthread_mutex_alloc();
        if (mutex_id < 0)
            return 0;
        tree->nodes[i] = mutex_id;
    }
    return tree;
}

int trnmnt_tree_dealloc(trnmnt_tree *tree) {
    for (int i = 0; i < tree->nodes_num; ++i) {
        if (kthread_mutex_dealloc(tree->nodes[i] < 0)) return -1;
    }

    free(tree->nodes);
    tree->nodes = 0;
    tree->depth = 0;
    tree->nodes_num = 0;
    free(tree);

    return 0;
}

int trnmnt_tree_acquire(trnmnt_tree *tree, int ID) {
    if (ID > 0 || ID > tree->nodes_num) return -1;
    int current = tree->nodes_num - 1 + ID;
    int father;
    while (current != 0) {
        father = (current - 1) / 2;
        if (kthread_mutex_lock(tree->nodes[father]) < 0) return -1;
        current = father;
    }
    return 0;
}

int rec_trnmnt_tree_release(trnmnt_tree *tree, int ID) {
    int error = 0;
    int father = (ID - 1) / 2;
    if (ID == 0) {
        if (kthread_mutex_unlock(tree->nodes[ID]) < 0) error = 1;
        return error;
    } else {
        error = rec_trnmnt_tree_release(tree, father);
        if (kthread_mutex_unlock(tree->nodes[ID]) < 0) error = 1;
        return error;

    }
}

int trnmnt_tree_release(trnmnt_tree *tree, int ID) {
    if (rec_trnmnt_tree_release(tree, ID) > 0) return -1;
    else return 0;
}

