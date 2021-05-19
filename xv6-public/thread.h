enum threadstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct thread{
  int tid;                     // thread id designated by thread_create 1st argument
  struct proc *parent;         // process who spawns the thread 
  enum threadstate state;      // thread state
  struct context *context;     // swtch() here to run process
  char *kstack;                // Bottom of kernel stack for this thread
  struct trapframe *tf;        // Trap frame for current syscall
  void *retval;
};

struct thdtable{
  struct spinlock lock;
  struct thread threads[NTHREAD];
  int index;                      // index of current thread in execution
};
