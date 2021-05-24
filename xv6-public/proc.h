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

enum thdstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

/* light weight process(thread) */
struct thread{
  int tid;                     // thread id designated by thread_create 1st argument
  struct proc *parent;         // process who spawns the thread 
  enum thdstate state;         // thread state
  struct context *context;     // swtch() here to run process
  char *kstack;                // Bottom of kernel stack for this thread
  struct trapframe *tf;        // Trap frame for current syscall
  void *chan;
  void *retval;
};

struct thdtable{
  struct spinlock lock;
  int nexttid;     
  struct thread threads[NTHREAD];
};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int mlfqlev;                 // level of queue in mlfq
  int share;
  double stride;
  struct thread *masterthd;    // master thread of process
  struct thread *curthd;       // current execution flow
  struct thdtable thdtable;    // thread table

  /* fields moved to thread struct */
  // enum procstate state;        // Process state
  // void *chan;                  // If non-zero, sleeping on chan
  // struct context *context;     // swtch() here to run process
  // struct trapframe *tf;        // Trap frame for current syscall
  // char *kstack;                // Bottom of kernel stack for this process
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

// mlfq structure
#define NULL 0
#define LEVEL 3
#define STRIDELEV -2
#define LEV0_TS 5
#define LEV1_TS 10
#define LEV2_TS 20
#define LEV0_TA 20
#define LEV1_TA 40
#define LEV2_TA 2100000000
#define BOOSTTIME 200
#define STRIDEMAX 80

#define SWAP(X, Y) { \
        struct strideqnode tmp = X; \
        X = Y;       \
        Y = tmp;     \
};

struct mlfqnode {
    struct proc *p;
    int tick;
};

struct queue{
    struct mlfqnode arr[NPROC];
    int head;
    int tail;
    int level;
    int procnum;
    int tick;
    int timeslice;
    int timeallotment;
};

struct mlfq{ 
    struct queue queues[LEVEL];
    int tick;
    int size;
    double pass;
    double stride;
};

struct strideqnode {
    struct proc *p;
    double pass;
};

struct strideq{
    struct strideqnode arr[NPROC+1];
    int size;
};
