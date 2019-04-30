#include "tournamet_tree.h"
#include "defs.h"

trnmnt_tree *trnmnt_tree_alloc(int depth) {
    int *memory = (int *) kalloc();
    if (memory == 0) return 0;
    trnmnt_tree tree = {.nodes = memory};

}
