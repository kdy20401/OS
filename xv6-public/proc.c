#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
// #include "thread.h"
#include "proc.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct mlfq mlfq;
struct strideq strideq;

int mlfqshare = 20;
int strideqshare = 80;

// select process from mlfq
// if runnable process is in the mlfq, return that process.
// if there is no runnable process, return null.
struct proc*
selectmlfqp(void)
{
    struct proc *p;
    struct queue *q;
    int head, tail;
    
    // tracking mlfq from level 0 to level 1, find runnable process
    for(int level = 0; level < LEVEL; level++) {
        q = &mlfq.queues[level];

        if(q->procnum == 0)
            continue;
    
        head = q->head;
        tail = q->tail;
        
        while(head != tail) {
            p = q->arr[head].p;
            
            // process has been terminated or moved to stride queue
            if(p->mlfqlev == -1 || p->mlfqlev == STRIDELEV) {
                dequeue(q);
            // states of process who should stay in mlfq 
            }else if(p->curthd->state == SLEEPING || p->curthd->state == EMBRYO) { // right??
                enqueue(dequeue(q), level, q->arr[q->head].tick);
            // found runnable process
            }else{
                return p;
            } 
            head++;
            head = head % NPROC;
        }

        p = q->arr[head].p;
        if(p->mlfqlev == -1 || p->mlfqlev == STRIDELEV) {
            dequeue(q);
        }else if(p->curthd->state == SLEEPING || p->curthd->state == EMBRYO) { // right??
            enqueue(dequeue(q), level, q->arr[q->head].tick);
        }else{
            return p;
        }
            
    }

    // there is no runnable process in mlfq
    return NULL;
}

void
updatemlfq(struct proc *p)
{
    struct queue *q;
    int level;

    level = p->mlfqlev;
    q = &mlfq.queues[level];
    mlfq.tick++;
    q->tick++;
    q->arr[q->head].tick++;

    // move process to queue one level below
    // if the process stays as much as queue's time allotment
    if(level < LEVEL-1 && q->arr[q->head].tick >= q->timeallotment) 
        enqueue(dequeue(q), level+1, 0);
    // move process to tail at the every queue's timeslice
    else if(q->tick % q->timeslice == 0)
        enqueue(dequeue(q), level, q->arr[q->head].tick);
    
    // move all processes to the topmost queue at the every boost time(100 tick)
    if(mlfq.tick == BOOSTTIME)
        boost();
   
    // increase mlfq pass value
    mlfq.pass += mlfq.stride;
}

void
min_heapify(struct strideqnode a[], int i, int size)
{
    int j;

    if (2*i > size) {
        return;
    }else if (2*i == size) {
        if (a[i].pass > a[2*i].pass) {
            SWAP(a[i], a[2*i]);
        }
    }else {
        if (a[2*i].pass < a[2*i+1].pass)
            j = 2*i;
        else
            j = 2*i+1;

        if (a[i].pass > a[j].pass) {
            SWAP(a[i], a[j]);
            min_heapify(a, j, size);
        }
    }
}

// push a process into stride queue
int
push(struct proc *p, double pass)
{
    int i;
    
    if(strideq.size >= NPROC) {
        // cprintf("push() : stride queue full\n");
        return -1;
    }

    i = ++strideq.size;
    strideq.arr[i].p = p;
    strideq.arr[i].pass = pass;

    while(i > 1) {
        if(strideq.arr[i].pass < strideq.arr[i/2].pass) {
            SWAP(strideq.arr[i], strideq.arr[i/2]);
            i /= 2;
        }else {
            break;
        }
    }

    return 0;
}

// pop a process from stride queue
struct proc*
pop(void)
{
    struct proc *p;
    
    p = NULL;

    if(strideq.size > 0) {
        p = strideq.arr[1].p;
        SWAP(strideq.arr[1], strideq.arr[strideq.size]);
        min_heapify(strideq.arr, 1, --strideq.size);
    }

    return p;
}

// select runnable process in stride queue
// if there is no runnable process in stride queue, return null.
// if runnable process is found, compare pass value of the process and mlfq
// if the process pass value is smaller, return that process.
// if the mlfq pass value is smaller, return null.
struct proc*
selectstrideqp(void)
{
    struct proc *p;
    double pass;
    
    // select process from stride queue
    while(strideq.size > 0) {
        p = strideq.arr[1].p;
        pass = strideq.arr[1].pass;
        
        // process has already been terminated
        if(p->mlfqlev == -1) {
            p = pop();

            // initialize pass value of mlfq to 0
            if(strideq.size == 0) {
                mlfq.pass = 0;
            }
        }else if(pass < mlfq.pass) {
            return p;
        }else {
            return NULL;
        }
    }

    // there is no runnable process in stride queue
    return NULL;
}
    
void
updatestrideq(void)
{
    struct proc *p;
    double pass;
    
    pass = strideq.arr[1].pass;
    p = pop();
    pass += p->stride;
    push(p, pass);
}

struct proc*
selectproc(void)
{
    struct proc *mp;
    struct proc *sp;
    
    // select process from mlfq
    mp = selectmlfqp();

    // select process from stride queue
    sp = selectstrideqp();
    
    if(sp != NULL) {
        updatestrideq();
        return sp;
    }else {
        if(mp != NULL) {
            updatemlfq(mp);
            return mp;
        }else {
            return NULL;
        }
    }
}

void
initmlfq(void)
{
    for(int i = 0; i < LEVEL; i++){
        mlfq.queues[i].head = 0;
        mlfq.queues[i].tail = -1;
        mlfq.queues[i].level = i;
        mlfq.queues[i].procnum = 0;
        mlfq.queues[i].tick = 0;
    }

    mlfq.queues[0].timeslice = LEV0_TS;
    mlfq.queues[1].timeslice = LEV1_TS;
    mlfq.queues[2].timeslice = LEV2_TS;

    mlfq.queues[0].timeallotment = LEV0_TA;
    mlfq.queues[1].timeallotment = LEV1_TA;
    mlfq.queues[2].timeallotment = LEV2_TA;
    
    mlfq.tick = 0;
    mlfq.size = 0;
    mlfq.pass = 0;
    mlfq.stride = 100.0/mlfqshare;
}


// this function is used when a process initially gets into mlfq
void 
enqueue1(struct proc *p)
{
    struct queue *q;
    
    q = &mlfq.queues[0];
    if(q->procnum == NPROC) {
        // cprintf("level 0 queue is full\n");
        return;
    }

    p->mlfqlev = 0;
    q->tail++;
    q->tail %= NPROC;
    q->arr[q->tail].p = p;
    q->arr[q->tail].tick = 0; 
    q->procnum++;
    mlfq.size++;
}

// add a process to queue of specific level
// tick value is set to 0 when the process moves to different queue 
void
enqueue(struct proc *p, int level, int tick)
{
    struct queue *q;
    
    q = &mlfq.queues[level];

    if(q->procnum == NPROC)
        return;

    p->mlfqlev = level;
    q->tail++;
    q->tail %= NPROC;
    q->arr[q->tail].p = p;
    q->arr[q->tail].tick = tick;
    q->procnum++;
    mlfq.size++;
}

// remove a process from queue of specific level
// and return the process
struct proc*
dequeue(struct queue *q)
{
    struct proc *p;

    if(q->procnum <= 0) {
        return NULL;
    }

    p = q->arr[q->head].p;
    q->head++;
    q->head %= NPROC;
    q->procnum--;
    mlfq.size--;    
    return p;
}

// to prevent starvation, move all process up to the highest queue
void boost(void)
{
    struct queue *q;

    for(int level = 1; level < LEVEL; level++) {
        q = &mlfq.queues[level];
            
        // initialize tick value to 0 and move to level 0 queue
        while(q->procnum != 0)
            enqueue(dequeue(q), 0, 0);
        q->tick = 0;
    }

    mlfq.queues[0].tick = 0; // needed?
    mlfq.tick = 0;
}   

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initmlfq();
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
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
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  struct thdtable *thdtable;

  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 414);

  // find unused process from process table
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
 
  // allocate master thread
  p->curthd = &p->thdtable.threads[0];
  p->curthd->parent = p;

  // all process are pushed into mlfq first
  enqueue1(p);  

  release(&ptable.lock);

  // initialize thread table lock
  thdtable = &p->thdtable;
  initlock(&thdtable->lock, "thdtable");
    
  // Allocate kernel stack.
  if((p->curthd->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->curthd->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->curthd->tf;
  p->curthd->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->curthd->context;
  p->curthd->context = (struct context*)sp;
  memset(p->curthd->context, 0, sizeof *p->curthd->context);
  p->curthd->context->eip = (uint)forkret;
    
  acquire(&thdtable->lock);
  
  p->curthd->state = RUNNABLE;
  
  release(&thdtable->lock);

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->curthd->tf, 0, sizeof(*p->curthd->tf));
  p->curthd->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->curthd->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->curthd->tf->es = p->curthd->tf->ds;
  p->curthd->tf->ss = p->curthd->tf->ds;
  p->curthd->tf->eflags = FL_IF;
  p->curthd->tf->esp = PGSIZE;
  p->curthd->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  // acquire(&ptable.lock);

  wrap_acquire(&ptable.lock, 504);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // allocate process, spawn master thread
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->curthd->kstack);
    np->curthd->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->curthd->tf = *curproc->curthd->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->curthd->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  
  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 571);
  np->state = RUNNABLE;

  release(&ptable.lock);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
    
  if(curproc == initproc)
    panic("init exiting");
  
  
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 606);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  if(curproc->mlfqlev == STRIDELEV) {
      strideqshare += curproc->share;
  }
  curproc->mlfqlev = -1;
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 640);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->curthd->kstack);
        p->curthd->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  // struct thdtable *thdtable = &myproc()->thdtable;
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    // acquire(&ptable.lock);
    wrap_acquire(&ptable.lock, 699);
    while(1) {

      // select runnable process from mlfq or stride queue
      p = selectproc();

      if(p == NULL)
          break;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->curthd->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->curthd->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}



// Give up the CPU for one scheduling round.
void
yield(void)
{
  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 760);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
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
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    // acquire(&ptable.lock);  //DOC: sleeplock1
    wrap_acquire(&ptable.lock, 808);
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 845);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  // acquire(&ptable.lock);
  wrap_acquire(&ptable.lock, 859);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
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
procdump(void)
{
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
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->curthd->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

/* system call */
int
getppid(void)
{
    return myproc()->parent->pid;
}

// return level(0, 1, 2) of mlfq the current process belongs to.
// return -2 if it belongs to stride queue
int
getlev(void)
{
    return myproc()->mlfqlev;
}

// guarantee the requested cpu share to process
// if success, return 0
// else, return -1
int
set_cpu_share(int share)
{
    struct proc *p, *sp;
    double minpass;

    if(strideq.size == NPROC) {
        // cprintf("stride queue full\n");
        return -1;
    }

    if(share > strideqshare) {
        // cprintf("stride queue share cannot exceed 80%\n");
        return -1;
    }

    strideqshare -= share;
    p = myproc();
    p->mlfqlev = STRIDELEV;
    p->share = share;
    p->stride = 100.0/share;

    // get the minimum pass value of mlfq and stride queue
    minpass = 2100000000.0;
    if(mlfq.pass < minpass) {
        minpass = mlfq.pass;
    } 

    // determine minpass by comparing with the top process of stride queue 
    for(int i = 1; i <= strideq.size; i++) {
        sp = strideq.arr[i].p;
        if(sp->mlfqlev == STRIDELEV) {
            if(strideq.arr[i].pass < minpass) {
                minpass = strideq.arr[i].pass;
                break;
            }
        }
    }

    // push into stride queue
    push(p, minpass);
    return 0;
}


/* light weight process(thread) */

extern void thrcreateret(void);

struct thread*
allocthread(void)
{ 
    struct proc *curproc;
    struct thread *t;
    struct thdtable *thdtable;
    char *sp;

    curproc = myproc();
    thdtable = &curproc->thdtable;
    
    acquire(&thdtable->lock);
    
    // find unused thread in thread table
    for(t = thdtable->threads; t < &thdtable->threads[NTHREAD]; t++) {
        if(t->state == UNUSED)
            goto found;
    }
    
    cprintf("thread not found\n");

    for(t = thdtable->threads; t < &thdtable->threads[NTHREAD]; t++) {
        cprintf("%d ", t->state);
    }
    cprintf("\n");

    release(&thdtable->lock);
    return 0;

found:
    t->state = EMBRYO;
    t->parent = curproc;
    t->tid = ++thdtable->nexttid; 
    release(&thdtable->lock);

    // allocate kernel stack
    if((t->kstack = kalloc()) == 0) {
        cprintf("kalloc err\n");
        t->state = UNUSED;
        return 0;
    }
    sp = t->kstack + KSTACKSIZE;

    // leave a room for trap frame
    sp -= sizeof *t->tf;
    t->tf = (struct trapframe*)sp;

    // set up new context to start executing at thrcreateret
    // which returns to trapret
    sp -= 4;
    *(uint*)sp = (uint)trapret;
    
    sp -= sizeof *t->context;
    t->context = (struct context*)sp;
    memset(t->context, 0, sizeof *t->context);
    t->context->eip = (uint)thrcreateret;

    return t;
}

void
thrcreateret(void)
{
    // before creating a new thread, thread table lock is acquired in yield2.
    // release lock so that new thread can call yield2 after timer interrupt.
    release(&myproc()->thdtable.lock);
    return;
}

int
thread_create(thread_t *thread, void * (*start_routine)(void *), void *arg)
{
    struct proc *curproc;
    struct thdtable *thdtable;
    struct thread *t;
    pde_t *pgdir;
    uint sz, sp, ustack[2];

    curproc = myproc();
    thdtable = &curproc->thdtable;

    // spawn new thread
    if((t = allocthread()) == 0) {
        cprintf("allocthread err\n");
        return -1;
    }
    
    // copy trapframe
    *thread = t->tid;
    *t->tf = *curproc->curthd->tf;
    
    // allocate user stack
    sz = curproc->sz;
    sz = PGROUNDUP(sz);
    pgdir = curproc->pgdir;

    // one for stack, the other for guard page
    if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0) {
        freevm(pgdir); // right?
        return -1;
    }
    clearpteu(pgdir, (char*)(sz - 2*PGSIZE));

    sp = sz;
    sp -= 2 * 4;

    ustack[0] = 0xffffffff; // after thread_exit, thread never returns
    ustack[1] = (uint)arg; // right?
    if(copyout(pgdir, sp, ustack, 2 * 4) < 0) {
        cprintf("copyout err\n");
        kfree(t->kstack);
        t->kstack = 0;    
        t->state = UNUSED;  // right?
        return -1;
    }

    curproc->sz = sz;

    // change execution flow by changing pc and stack pointer
    t->tf->eip = (uint)start_routine;
    // cprintf("start_routine address in kernel = %d\n", t->tf->eip);
    t->tf->esp = sp;
    
    // new execution flow is ready
    acquire(&thdtable->lock);

    t->state = RUNNABLE;

    release(&thdtable->lock);
    
    // t->tf->eax = 0;

    // cprintf("thread_create fin. tid = %d\n", t->tid);
    return 0;
}

void
wakeup2(void *chan)
{
    struct thread *t;
    struct thdtable *thdtable;

    thdtable = &myproc()->thdtable;
    
    for(t = thdtable->threads; t < &thdtable->threads[NTHREAD]; t++) {
        if(t->state == SLEEPING && t->chan == chan) {
            t->state = RUNNABLE;
        }
    }
}

void
wrap_sched2(void)
{
    struct thdtable *thdtable;
    struct thread *curthd, *nextthd;
    struct proc *curproc;
    int i, j;
    int intena;

    curproc = myproc();
    thdtable = &curproc->thdtable;
    curthd = curproc->curthd;

    if(!holding(&thdtable->lock))
        panic("sched2 thdtable.lock");
    if(mycpu()->ncli != 1)
        panic("sched2 locks");
    if(curthd->state == RUNNING)  // main thread ??
        panic("sched2 running");
    if(readeflags()&FL_IF)
        panic("sched2 interruptible");
   
    i = thdtable->index;
    j = (i + 1) % NTHREAD;

    // find next runnable thread
    while(j != i) {
        nextthd = &thdtable->threads[j];
        if(nextthd->state == RUNNABLE) {
            break;
        }

        j++;
        j = j % NTHREAD;
    }
    
    // if only the current thread is ready, just change state and return
    // no context switch between threads is needed
    if(j == i) {
        curthd->state = RUNNING;
        return;
    }

    // switch TSS. right??
    pushcli();

    mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts, sizeof(mycpu()->ts)-1, 0);
    mycpu()->gdt[SEG_TSS].s = 0;
    mycpu()->ts.ss0 = SEG_KDATA << 3;
    mycpu()->ts.esp0 = (uint)nextthd->kstack + KSTACKSIZE;
    mycpu()->ts.iomb = (ushort) 0xFFFF;
    ltr(SEG_TSS << 3);

    popcli();
   
    intena = mycpu()->intena;
    thdtable->index = j;
    curproc->curthd = nextthd;
    curproc->curthd->state = RUNNING;
    // cprintf("swtch\n");
    swtch(&curthd->context, nextthd->context);
    mycpu()->intena = intena;
}

// before sleep2 call, thdtable.lock is already held
void
sleep2(struct spinlock *lk)
{
    struct proc *curproc;
    struct thread *curthd;

    curproc = myproc();

    if(curproc == 0)
        panic("sleep2");
    if(lk == 0)
        panic("sleep2 without lk");

    curthd = curproc->curthd;
    curthd->state = SLEEPING;
    // cprintf("sched2\n");
    wrap_sched2();
}

void
thread_exit(void *retval)
{
    struct proc *curproc;
    struct thdtable *thdtable;
    struct thread *curthd, *masterthd;

    curproc = myproc();
    thdtable = &curproc->thdtable;
    curthd = curproc->curthd;

    // cprintf("thread_exit starts. tid = %d\n", curthd->tid);

    acquire(&thdtable->lock);

    // wake up master thread who might be waiting
    // make thread state to RUNNABLE
    // wakeup2(&thdtable->threads[0]);
    masterthd = thdtable->threads;

    if(masterthd->state == SLEEPING)
        masterthd->state = RUNNABLE;

    // change state of thread
    curthd->state = ZOMBIE;
    curthd->retval = retval;
    thdtable->nexttid--;

    // cprintf("thread_exit fin. tid = %d\n", curthd->tid);
    sched2();
    panic("zombie thread");
}

int
thread_join(thread_t thread, void **retval)
{
    struct proc *curproc;
    struct thdtable *thdtable;
    struct thread *t;
    int havekids;
    // int tmp;

    curproc = myproc();
    thdtable = &curproc->thdtable;

    // cprintf("thread_join starts. tid = %d\n", thread);
    
    acquire(&thdtable->lock);
    for(;;) {
        havekids = 0;
        for(t = thdtable->threads; t < &thdtable->threads[NTHREAD]; t++) {
            if(t->tid != thread)
                continue;
            havekids = 1;

            // found thread
            if(t->state == ZOMBIE) {
                // tmp = t->tid;
                kfree(t->kstack);
                t->kstack = 0;
                t->tid = 0;
                t->parent = 0;
                t->state = UNUSED;
                *retval = t->retval;
                release(&thdtable->lock);
                // cprintf("thread_join fin. tid = %d\n", tmp);
                return 0;
            // if thread already terminates, return
            }else if(t->state == UNUSED) {
                release(&thdtable->lock);
                return 0;
            }

        }

        if(!havekids || curproc->killed) {
            release(&thdtable->lock);
            return -1;
        }
        
        // wait until target thread calls thread_exit
        // cprintf("thread %d going to sleep,,\n", curproc->curthd->tid);
        sleep2(&thdtable->lock);
        // cprintf("thread %d wakes up!\n", curproc->curthd->tid);
    }
}

void
sched2(void)
{
    struct thdtable *thdtable;
    struct thread *curthd, *nextthd;
    struct proc *curproc;
    int i, j;
    int intena;

    curproc = myproc();
    thdtable = &curproc->thdtable;
    curthd = curproc->curthd;

    if(!holding(&thdtable->lock))
        panic("sched2 thdtable.lock");
    if(mycpu()->ncli != 1)
        panic("sched2 locks");
    if(curthd->state == RUNNING)  // main thread ??
        panic("sched2 running");
    if(readeflags()&FL_IF)
        panic("sched2 interruptible");
   
    i = thdtable->index;
    j = (i + 1) % NTHREAD;

    // find next runnable thread
    while(j != i) {
        nextthd = &thdtable->threads[j];
        if(nextthd->state == RUNNABLE) {
            break;
        }

        j++;
        j = j % NTHREAD;
    }
    
    // if only the current thread is ready, just change state and return
    // no context switch between threads is needed
    if(j == i) {
        curthd->state = RUNNING;
        return;
    }

    // switch TSS. right??
    pushcli();

    mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts, sizeof(mycpu()->ts)-1, 0);
    mycpu()->gdt[SEG_TSS].s = 0;
    mycpu()->ts.ss0 = SEG_KDATA << 3;
    mycpu()->ts.esp0 = (uint)nextthd->kstack + KSTACKSIZE;
    mycpu()->ts.iomb = (ushort) 0xFFFF;
    ltr(SEG_TSS << 3);

    popcli();
   
    intena = mycpu()->intena;
    thdtable->index = j;
    curproc->curthd = nextthd;
    curproc->curthd->state = RUNNING;
    swtch(&curthd->context, nextthd->context);
    mycpu()->intena = intena;
}

void
yield2(void)
{
    struct thdtable *thdtable;
    struct thread *curthd;
    struct proc *curproc;

    curproc = myproc();
    thdtable = &curproc->thdtable;
    curthd = curproc->curthd;
    
    acquire(&thdtable->lock); 
    
    curthd->state = RUNNABLE; 
    sched2(); 
    
    // if(holding(&ptable.lock)) {
    //     release(&ptable.lock);
    // }

    release(&thdtable->lock);
}
