#include <stddef.h>
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "kthread.h"
#include "tournament_tree.h"

struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;

extern void forkret(void);

extern void trapret(void);

static void wakeup1(void *chan);

void
acquire_ptable() {
    acquire(&ptable.lock);
}

void
release_ptable() {
    release(&ptable.lock);
}

struct spinlock *
get_ptable_lock() {
    return &ptable.lock;
}

void
pinit(void) {
    initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
    return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void) {
    int apicid, i;

    if (readeflags() & FL_IF)
        panic("mycpu called with interrupts enabled\n");

    apicid = lapicid();
    // APIC IDs are not guaranteed to be contiguous. Maybe we should have
    // a reverse map, or reserve a register to store &cpus[i].
    for (i = 0; i < ncpu; ++i) {
        if (cpus[i].apicid == apicid)
            return &cpus[i];
    }
    panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void) {
    struct cpu *c;
    struct proc *p;
    pushcli();
    c = mycpu();
    p = c->proc;
    popcli();
    return p;
}

struct thread *
mythread(void) {
    struct cpu *c;
    struct thread *t;
    pushcli();
    c = mycpu();
    t = c->thread;
    popcli();
    return t;
}

void exit_process();

int
allocthread(struct proc *p, int first) {
    struct thread *t = &p->threads[p->t_num];
    p->t_num++;
    char *sp;
    t->p_parent = p;
    t->tid = p->t_num;
    if (first)
        t->state = EMBRYO;
    else t->state = RUNNABLE;
    t->tid = p->t_num;
    t->killed = 0;

    // Allocate kernel stack.
    if ((t->kstack = kalloc()) == 0) {
        t->state = UNUSED_T;
        return 0;
    }
    sp = t->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof *t->tf;
    t->tf = (struct trapframe *) sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint *) sp = (uint) trapret;

    sp -= sizeof *t->context;
    t->context = (struct context *) sp;
    memset(t->context, 0, sizeof *t->context);
    t->context->eip = (uint) forkret;
    return 1;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void) {
    struct proc *p;

    acquire(&ptable.lock);

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == UNUSED)
            goto found;
    release(&ptable.lock);
    return 0;

    found:
    p->t_num = 0;
    p->pid = nextpid++;
    p->state = USED;
    int success = allocthread(p, 1);
    release(&ptable.lock);
    if (!success) {
        return 0;
    }
    return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void) {
    struct proc *p;
    extern char _binary_initcode_start[], _binary_initcode_size[];

    // 1. allocate proc structure + thread structure
    p = allocproc();
    struct thread *t = &p->threads[0];

    acquire(&ptable.lock);
    initproc = p;
    if ((p->pgdir = setupkvm()) == 0)
        panic("userinit: out of memory?");
    inituvm(p->pgdir, _binary_initcode_start, (int) _binary_initcode_size);
    p->sz = PGSIZE;
    memset(t->tf, 0, sizeof(*t->tf));
    t->tf->cs = (SEG_UCODE << 3) | DPL_USER;
    t->tf->ds = (SEG_UDATA << 3) | DPL_USER;
    t->tf->es = t->tf->ds;
    t->tf->ss = t->tf->ds;
    t->tf->eflags = FL_IF;
    t->tf->esp = PGSIZE;
    t->tf->eip = 0;  // beginning of initcode.S

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    // this assignment to p->state lets other cores
    // run this process. the acquire forces the above
    // writes to be visible, and the lock is also needed
    // because the assignment might not be atomic.

    p->state = USED;
    t->state = RUNNABLE;

    release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n) {
    uint sz;
    struct proc *curproc = myproc();
    struct thread *curthread = mythread();
    // new lock
    acquire(&ptable.lock);
    sz = curproc->sz;
    if (n > 0) {
        if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0) {
            release(&ptable.lock);
            return -1;
        }
    } else if (n < 0) {
        if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0) {
            release(&ptable.lock);
            return -1;
        }
    }
    curproc->sz = sz;
    switchuvm(curproc, curthread);
    release(&ptable.lock);
    return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void) {
    int i, pid;
    struct proc *np;
    struct proc *curproc = myproc();
    struct thread *curthread = mythread();
    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }
    struct thread *nt = &np->threads[0];
    // Copy process state from proc.

    // new lock
    acquire(&ptable.lock);

    if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {

        kfree(nt->kstack);
        nt->kstack = 0;
        np->state = UNUSED;
        nt->state = UNUSED_T;
        return -1;
    }
    np->sz = curproc->sz;
    np->parent = curproc;
    *nt->tf = *curthread->tf;

    // Clear %eax so that fork returns 0 in the child.
    nt->tf->eax = 0;

    for (i = 0; i < NOFILE; i++)
        if (curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));

    pid = np->pid;

    nt->state = RUNNABLE;

    release(&ptable.lock);

    return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void) {
    struct proc *curproc = myproc();
    struct thread *t;
    acquire(&ptable.lock);
    if (mythread()->killed == 1)
        exit_thread();
    else {
        for (int i = 0; i < curproc->t_num; ++i) {
            t = &curproc->threads[i];
            t->killed = 1;
            // Wake process from sleep if necessary.
            if (t->state == SLEEPING)
                t->state = RUNNABLE;
        }
        release(&ptable.lock);
    }
}

void
exit_thread(void) {
    struct thread *t;
    struct proc *curproc = myproc();
    int running_t = 0;

    for (int i = 0; i < curproc->t_num; ++i) {
        t = &curproc->threads[i];
        if (t->state != ZOMBIE_T && t->state != UNUSED_T)
            running_t++;
    }

    if (running_t == 0) panic("No running thread!");

    if (running_t == 2) {
        wakeup1(myproc());
    }
    // change 1
    if (running_t == 1) {
//        wakeup1(mythread());
        release(&ptable.lock);
        exit_process();
    } else {
        wakeup1(mythread());
        mythread()->state = ZOMBIE_T;
        sched();
    }
}

void exit_process() {
    struct proc *curproc = myproc();
    struct proc *p;
    int fd;
    if (curproc == initproc)
        panic("init exiting");

    // Close all open files.
    for (fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable.lock);

    // Parent might be sleeping in wait().
    wakeup1(curproc->parent);

    // Pass abandoned children to init.
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->parent == curproc) {
            p->parent = initproc;
            if (p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }
    curproc->killed = 1;
    // Jump into the scheduler, never to return.
    curproc->state = ZOMBIE;
    mythread()->state = ZOMBIE_T;
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void) {
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();
    struct thread *curthread = mythread();
    struct thread *t;

    acquire(&ptable.lock);
    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if (p->parent != curproc)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE) {
                pid = p->pid;
                p->pid = 0;
                freevm(p->pgdir);
                p->parent = 0;
                p->name[0] = 0;
                p->state = UNUSED;
                for (int i = 0; i < p->t_num; ++i) {
                    t = &p->threads[i];
                    if (t->state == ZOMBIE_T) {
                        // Found one.
                        kfree(t->kstack);
                        t->kstack = 0;
                        t->tid = 0;
                        t->p_parent = 0;
                        t->killed = 0;
                        t->state = UNUSED_T;
                    }

                }
                release(&ptable.lock);
                return pid;
            }
        }


        // No point waiting if we don't have any children.
        if (!havekids || curthread->killed) {
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    }
}

//#pragma clang diagnostic push
//#pragma clang diagnostic ignored "-Wmissing-noreturn"

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void) {
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;

    for (;;) {
        // Enable interrupts on this processor.
        sti();
//        cprintf("loop\n");
        // Loop over process table looking for process to run.
        acquire(&ptable.lock);
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//            cprintf("state: %s\n",p->state);
            if (p->state != USED)
                continue;
            for (int i = 0; i < p->t_num; ++i) {
                if (p->threads[i].state != RUNNABLE)
                    continue;

                // Switch to chosen process.  It is the process's job
                // to release ptable.lock and then reacquire it
                // before jumping back to us.
                c->proc = p;
                c->thread = &p->threads[i];
                switchuvm(c->proc, c->thread);
                p->threads[i].state = RUNNING;

                swtch(&(c->scheduler), p->threads[i].context);
                switchkvm();

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
                c->thread = 0;
            }
        }
        release(&ptable.lock);
    }
}

//#pragma clang diagnostic pop

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void) {
    int intena;
    struct thread *t = mythread();

    if (!holding(&ptable.lock))
        panic("sched ptable.lock");
    if (mycpu()->ncli != 1)
        panic("sched locks");
    if (t->state == RUNNING)
        panic("sched running");
    if (readeflags() & FL_IF)
        panic("sched interruptible");
    intena = mycpu()->intena;
    swtch(&t->context, mycpu()->scheduler);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void) {
    acquire(&ptable.lock);  //DOC: yieldlock
    mythread()->state = RUNNABLE;
    sched();
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void) {
    static int first = 1;
    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first) {
        // Some initialization functions must be run in the context
        // of a regular process (e.g., they call sleep), and thus cannot
        // be run from main().
        first = 0;
        iinit(ROOTDEV);
        initlog(ROOTDEV);
    }

    // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk) {
    struct proc *p = myproc();
    struct thread *t = mythread();

    if (p == 0)
        panic("sleep");

    if (lk == 0)
        panic("sleep without lk");

    // Must acquire ptable.lock in order to
    // change p->state and then call sched.
    // Once we hold ptable.lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup runs with ptable.lock locked),
    // so it's okay to release lk.
    if (lk != &ptable.lock) {  //DOC: sleeplock0
        acquire(&ptable.lock);  //DOC: sleeplock1
        release(lk);
    }
    // Go to sleep.
    t->chan = chan;
    t->state = SLEEPING;

    sched();

    // Tidy up.
    t->chan = 0;

    // Reacquire original lock.
    if (lk != &ptable.lock) {  //DOC: sleeplock2
        release(&ptable.lock);
        acquire(lk);
    }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan) {
    struct proc *p;
    struct thread *t;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        for (int i = 0; i < p->t_num; ++i) {
            t = &p->threads[i];
            if (t->state == SLEEPING && t->chan == chan)
                t->state = RUNNABLE;
        }

}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan) {
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid) {
    struct proc *p;
    struct thread *t;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == pid) {
            p->killed = 1;
            for (int i = 0; i < p->t_num; i++) {
                t = &p->threads[i];
                t->killed = 1;
                // Wake process from sleep if necessary.
                if (t->state == SLEEPING)
                    t->state = RUNNABLE;
            }
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void) {
    static char *states[] = {
            [UNUSED]    "unused",
            [EMBRYO]    "embryo",
            [SLEEPING]  "sleep ",
            [RUNNABLE]  "runble",
            [RUNNING]   "run   ",
            [ZOMBIE]    "zombie"
    };
    int i;
    struct proc *p;
    struct thread *t;
    char *state;
    uint pc[10];

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        for (int j = 0; j < p->t_num; ++j) {
            t = &p->threads[j];
            if (t->state >= 0 && t->state < NELEM(states) && states[t->state])
                state = states[t->state];
            else
                state = "???";
            cprintf("pid:%d tid:%d %s %s", p->pid, t->tid, state, p->name);
            if (t->state == SLEEPING) {
                getcallerpcs((uint *) t->context->ebp + 2, pc);
                for (i = 0; i < 10 && pc[i] != 0; i++)
                    cprintf(" %p", pc[i]);
            }
            cprintf("\n");
        }
    }
}

int
kthread_create(void (*start_func)(), void *stack) {
    struct thread *t;
    acquire(&ptable.lock);
    if (allocthread(myproc(), 0) == 0)
        return 0;
    t = &myproc()->threads[myproc()->t_num - 1];
    *t->tf = *mythread()->tf;
    // set entry point
    t->tf->eip = (uint) start_func;  // main
    // set stack pointer - assume user sent stack with MAX_SIZE addition.
    t->tf->esp = (uint) stack;
    release(&ptable.lock);
    return t->tid;
}

//int
//kthread_id() {
//    return mythread()->tid;
//}

void
kthread_exit() {
    acquire(&ptable.lock);
    exit_thread();
}

int
kthread_join(int thread_id) {
    struct thread *t = 0;
    acquire(&ptable.lock);
    for (int i = 0; i < myproc()->t_num; ++i) {
        if (myproc()->threads[i].tid == thread_id) t = &myproc()->threads[i];
    }
    if (t == 0) {
        cprintf("kthread_join: no thread matches the id");
        return -1;
    }
    while (1) {
        if (t->state == ZOMBIE_T) {
            break;
        }
        else if (t->state != UNUSED_T)
            sleep(t, &ptable.lock);
    }
    release(&ptable.lock);
    return 0;
}


static struct kthread_mutex_t mutexes[MAX_MUTEXES];
static int mutex_num = 0;
static struct spinlock mutexes_lock;

int kthread_mutex_alloc() {
    if (mutex_num == 0) initlock(&mutexes_lock, "mutex lock");
    if (mutex_num == MAX_MUTEXES) return -1;
    acquire(&mutexes_lock);
    mutex_num++;
    release(&mutexes_lock);

    struct kthread_mutex_t *mutex = &mutexes[mutex_num - 1];
    initlock(&mutex->lk, "mutex lock");
    acquire(&mutex->lk);
    mutex->locked = 0;
    mutex->id = mutex_num;
    release(&mutex->lk);

    acquire(&mutexes_lock);
    mutexes[mutex_num - 1] = *mutex;
    release(&mutexes_lock);

    return mutex->id;
}

int kthread_mutex_dealloc(int mutex_id) {
    if (0 > mutex_id || mutex_id > MAX_MUTEXES)
        return -1;
    struct kthread_mutex_t mutex;
    mutex = mutexes[mutex_id - 1];
    if (mutex.locked) {
        return -1;
    }
    acquire(&mutex.lk);
    mutex.locked = 0;
    mutex.id = 0;
    wakeup(&mutex);
    release(&mutex.lk);

    acquire(&mutexes_lock);
    mutex_num--;
    release(&mutexes_lock);
    return 0;
}

int kthread_mutex_lock(int mutex_id) {
    if (0 > mutex_id || mutex_id > MAX_MUTEXES)
        return -1;
    struct kthread_mutex_t mutex;
    mutex = mutexes[mutex_id - 1];
    acquire(&mutex.lk);

    while (mutex.locked) {
        sleep(&mutex, &mutex.lk);
    }
    mutex.locked = 1;
    release(&mutex.lk);
    return 0;
}

int kthread_mutex_unlock(int mutex_id) {
    if (0 > mutex_id || mutex_id > MAX_MUTEXES)
        return -1;
    struct kthread_mutex_t mutex;
    mutex = mutexes[mutex_id - 1];

    acquire(&mutex.lk);
    mutex.locked = 0;
    mutex.id = 0;
    wakeup(&mutex);
    release(&mutex.lk);
    return 0;
}
//
//int
//power(int b, int pwr) {
//    int i;
//    int acc = 1;
//    for (i = 0; i < pwr; i++) {
//        acc = acc * b;
//    }
//    return acc;
//}
//
//trnmnt_tree *trnmnt_tree_alloc(int depth) {
//    if (depth < 1)return 0;
//    trnmnt_tree *tree = (trnmnt_tree *) kalloc();
//    tree->nodes = (int *) kalloc();
//    tree->depth = depth;
//    tree->nodes_num = power(2, depth);
//
//    for (int i = 0; i < tree->nodes_num; i++) {
//        int mutex_id = kthread_mutex_alloc();
//        if (mutex_id < 0)
//            return 0;
//        tree->nodes[i] = mutex_id;
//    }
//    return tree;
//}
//
//int trnmnt_tree_dealloc(trnmnt_tree *tree) {
//    for (int i = 0; i < tree->nodes_num; ++i) {
//        if (kthread_mutex_dealloc(tree->nodes[i] < 0)) return -1;
//    }
//    return 0;
//}
//
//int trnmnt_tree_acquire(trnmnt_tree *tree, int ID) {
//    if (ID > 0 || ID > tree->nodes_num) return -1;
//    int current = tree->nodes_num - 1 + ID;
//    int father;
//    while (current != 0) {
//        father = (current - 1) / 2;
//        if (kthread_mutex_lock(tree->nodes[father]) < 0) return -1;
//        current = father;
//    }
//    return 0;
//}
//
//int rec_trnmnt_tree_release(trnmnt_tree *tree, int ID) {
//    int error = 0;
//    int father = (ID - 1) / 2;
//    if (ID == 0) {
//        if (kthread_mutex_unlock(tree->nodes[ID]) < 0) error = 1;
//        return error;
//    } else {
//        error = rec_trnmnt_tree_release(tree, father);
//        if (kthread_mutex_unlock(tree->nodes[ID]) < 0) error = 1;
//        return error;
//
//    }
//}
//
//int trnmnt_tree_release(trnmnt_tree *tree, int ID) {
//    if (rec_trnmnt_tree_release(tree, ID) > 0) return -1;
//    else return 0;
//}
//
