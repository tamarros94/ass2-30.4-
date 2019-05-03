#include "types.h"

#include "user.h"
#include "kthread.h"
#include "tournament_tree.h"

#define STACK_SIZE 500

void get_to_work() {
    sleep(15);
    printf(1, "working ... \n");
    kthread_exit();
}

void give_me_your_id() {
    sleep(15);
    int res = kthread_id();
    if (res == -1) printf(1, "kthread_id FAIL..\n");
    else printf(1, "kthread_id SUCCESS ..... my id is: %d......\n", res);
    kthread_exit();

}
void test_kthread_create() {
    printf(1, "TESTING KTHREAD_CREATE\n");
    printf(1, "**************************************************************\n");
    void *stack = ((char *) malloc(STACK_SIZE * sizeof(char))) + STACK_SIZE;

    int tid = kthread_create(get_to_work, stack);

    if (tid == -1) printf(1, "kthread_create FAIL\n");
    else printf(1, "kthread_create SUCCESS\n");
}

void test_kthread_join() {
    printf(1, "TESTING KTHREAD_JOIN\n");
    printf(1, "**************************************************************\n");
    void *stack = ((char *) malloc(STACK_SIZE * sizeof(char))) + STACK_SIZE;

    int tid = kthread_create(get_to_work, stack);

    int joined = kthread_join(tid);


    if (joined == -1) printf(1, "kthread_join FAIL\n");
    else printf(1, "kthread_join SUCCESS\n");


}

void test_kthread_id() {
    printf(1, "TESTING KTHREAD_ID\n");
    printf(1, "**************************************************************\n");
    void *stack = ((char *) malloc(STACK_SIZE * sizeof(char))) + STACK_SIZE;

    for (int i = 0; i < 5; i++) {
        int tid = kthread_create(give_me_your_id, stack);
        kthread_join(tid);
    }

}


void test_mutex_alloc() {
    printf(1, "TESTING MUTEX_ALLOC\n");
    printf(1, "**************************************************************\n");
    int ret = kthread_mutex_alloc();

    if (ret == -1) printf(1, "kthread_mutex_alloc FAIL\n");
    else printf(1, "kthread_mutex_alloc SUCCESS\n");
}

void test_mutex_dealloc() {
    printf(1, "TESTING MUTEX_ALLOC\n");
    printf(1, "**************************************************************\n");
    int m_id = kthread_mutex_alloc();
    int ret = kthread_mutex_dealloc(m_id);

    if (ret == 0) printf(1, "dealloc SUCCESS\n");
    else printf(1, "dealloc FAIL\n");
}

void test_mutex_lock_unlock() {
    printf(1, "TESTING MUTEX LOCK UNLOCK\n");
    printf(1, "**************************************************************\n");
    int lock_me = kthread_mutex_alloc();

    int ret = kthread_mutex_lock(lock_me);
    if (ret == 0) printf(1, "lock SUCCESS\n");
    else printf(1, "lock FAIL\n");

    ret = kthread_mutex_unlock(lock_me);
    if (ret == 0) printf(1, "unlock SUCCESS\n");
    else printf(1, "unlock FAIL\n");
}

void test_trnmnt_alloc() {
    int depth = 4;
    trnmnt_tree *tree = trnmnt_tree_alloc(depth);

    int nodes_num = tree->nodes_num;
    int expected_size = 15;

    if (nodes_num == expected_size) {
        if (tree->depth == depth) printf(1, "trnmnt_tree_alloc SUCCESS\n");
        else printf(1, "trnmnt_tree_alloc FAIL\n");
    } else printf(1, "trnmnt_tree_alloc FAIL\n");


}

void test_trnmnt_dealloc() {
    int depth = 4;
    trnmnt_tree *tree = trnmnt_tree_alloc(depth);

    int ret = trnmnt_tree_dealloc(tree);
    if (ret == 0) {
        ret = trnmnt_tree_dealloc(tree);
        if (ret == -1) printf(1, "trnmnt_tree_dealloc SUCCESS\n");
        else printf(1, "trnmnt_tree_dealloc FAIL\n");
    } else printf(1, "trnmnt_tree_dealloc FAIL\n");


}

int acc = 1;
trnmnt_tree *tree;

void increase_func(){
    sleep(15);
    trnmnt_tree_acquire(tree, 4);
    acc++;
    trnmnt_tree_release(tree, 4);
    kthread_exit();
    }

void test_trnmnt_acquire_release() {
    int depth = 3;
    tree = trnmnt_tree_alloc(depth);

    void *stack = ((char *) malloc(STACK_SIZE * sizeof(char))) + STACK_SIZE;
    kthread_create(increase_func, stack);

    trnmnt_tree_acquire(tree, 7);

    acc++;

    trnmnt_tree_release(tree, 7);

    sleep(150);
    if (acc == 3) printf(1, "trnmnt_acquire SUCCESS\n");
    else printf(1, "trnmnbt acquire FAIL\n");

}


void t2() {
    test_kthread_create();
    test_kthread_join();
    test_kthread_id();
}


void t3_1() {
    test_mutex_alloc();
    test_mutex_dealloc();
    test_mutex_lock_unlock();
}

void t3_2() {
    test_trnmnt_alloc();
    test_trnmnt_dealloc();
    test_trnmnt_acquire_release();
}


int main() {
    printf(1, "     ---------------- SANITY ----------------     \n");
    t2();
    t3_1();
    t3_2();

    exit();
}