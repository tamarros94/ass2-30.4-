#define NTHREADS 16

// Per-CPU state
struct cpu {
    uchar apicid;                // Local APIC ID
    struct context *scheduler;   // swtch() here to enter scheduler
    struct taskstate ts;         // Used by x86 to find stack for interrupt
    struct segdesc gdt[NSEGS];   // x86 global descriptor table
    volatile uint started;       // Has the CPU started?
    int ncli;                    // Depth of pushcli nesting.
    int intena;                  // Were interrupts enabled before pushcli?
    struct proc *proc;           // The process running on this cpu or null
    struct thread *thread;
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
    uint edi;
    uint esi;
    uint ebx;
    uint ebp;
    uint eip;
};

enum procstate {
    UNUSED, ZOMBIE, USED
};
enum threadstate {
    UNUSED_T, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE_T, BLOCKED
};


struct thread {
    char *kstack;                // Bottom of kernel stack for this process
    enum threadstate state;        // Process state
    int killed;                  // If non-zero, have been killed
    struct trapframe *tf;        // Trap frame for current syscall
    void *chan;                  // If non-zero, sleeping on chan
    struct context *context;     // swtch() here to run process
    struct proc *p_parent;
    int tid;
};

// Per-process state
struct proc {
    int killed;                  // If non-zero, have been killed
    uint sz;                     // Size of process memory (bytes)
    pde_t *pgdir;                // Page table
    int pid;                     // Process ID
    struct proc *parent;         // Parent process
    struct file *ofile[NOFILE];  // Open files
    char name[16];               // Process name (debugging)
    struct thread threads[NTHREADS];
    enum procstate state;        // Process state
    int t_num;
    struct inode *cwd;           // Current directory

};

