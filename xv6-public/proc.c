#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
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

struct {
    struct queue queues[LEVEL];
    int tick;
    int size;
    double pass;
    double stride;
} mlfq;

struct {
    struct strideqnode arr[NPROC+1];
    int size;
}strideq;

int mlfqticket = 20;
int strideqticket = 80;

int
topqlev(void)
{
    int level;

    level = 0;
    while(level < LEVEL-1 && mlfq.queues[level].procnum == 0)
        level++;

    return level;
}

struct proc*
mlfqtop(void)
{
    struct queue *q;

    if(mlfq.size == 0)
        return NULL;

    q = &mlfq.queues[topqlev()];

    return q->arr[q->head].p;
}

struct proc*
selectmlfqp(void)
{
    struct proc *p;
    struct queue *q;
    int head, tail;

    for(int level = 0; level < LEVEL; level++) {
        q = &mlfq.queues[level];
        if(q->procnum == 0) {
            continue;
        }
        head = q->head;
        tail = q->tail;
        
        while(head != tail) {
            p = q->arr[head].p;
            if(p->state == ZOMBIE || p->state == UNUSED || p->mlfqlev == -2) {
                dequeue(q);
            }else if(p->state == SLEEPING || p->state == EMBRYO) {
                enqueue(dequeue(q), level, q->arr[q->head].tick);
            }else{ // RUNNABLE
                return p;
            } 
            
            head++;
            head = head % NPROC;
        }

        p = q->arr[head].p;
        if(p->state == ZOMBIE || p->state == UNUSED  || p->mlfqlev == -2) {
            dequeue(q);
        }else if(p->state == SLEEPING || p->state == EMBRYO) {
            enqueue(dequeue(q), level, q->arr[q->head].tick);
        }else{ // RUNNABLE
            return p;
        }
            
    }
    return NULL;
}

void
handlemlfq(int level)
{
    struct queue *q;
    
    q = &mlfq.queues[level];
    // cprintf("in handle : qlevel = %d, procnum = %d\n", level, q->procnum);
    mlfq.tick++;
    q->tick++;
    q->arr[q->head].tick++;

    // if the process stays as much queue's time allotment
    // move process to queue one level below
    if(level < LEVEL-1 && q->arr[q->head].tick >= q->timeallotment) {
        enqueue(dequeue(q), level+1, 0);
    }
    // move process to tail at the every queue's timeslice
    else if(q->tick % q->timeslice == 0) {
        enqueue(dequeue(q), level, q->arr[q->head].tick);
    }
    // move all processes to the topmost queue at the every boost time(100 tick)
    if(mlfq.tick == BOOSTTIME)
        boost();

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
        panic("push() : stride queue full\n");
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

struct proc*
strideqtop(void)
{
    if(strideq.size == 0)
        return NULL;
    else
        return strideq.arr[1].p;
}


struct proc*
selectstrideqp(void)
{
    struct proc *p;
    double pass;
    
    // select process from stride queue
    while(strideq.size > 0) {
        p = strideq.arr[1].p;
        pass = strideq.arr[1].pass;

        if(p->state == ZOMBIE || p->state == UNUSED) {
            // cprintf("state : %d, mlfqlev : %d\n", p->state, p->mlfqlev);
            p = pop();
            strideqticket += p->share;
            if(strideq.size == 0) {
                mlfq.pass = 0;
            }
        }else {
            if(pass < mlfq.pass) {
                return p;
            }else {
                return NULL;
            }
        }
    }
    return NULL;
}
    
void
handlestrideq(void)
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
    
    // select process from mlfq and stride queue
    // mp == NULL -> no runnable process in mlfq
    // mp != NULL -> runnable process in mlfq
    mp = selectmlfqp();
    // sp == NULL -> no process in stride queue
    // sp == NULL -> mlfq's process should be selected
    // sp != NULL -> stride queue's process should be seleted
    sp = selectstrideqp();

    if(sp != NULL) {
        handlestrideq();
        return sp;
    }else {
        if(mp != NULL) {
            handlemlfq(mp->mlfqlev);
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
    mlfq.stride = 100.0/mlfqticket;
}


// this function is used when a process initially gets into mlfq
void 
enqueue1(struct proc *p)
{
    struct queue *q;
    
    q = &mlfq.queues[0];
    if(q->procnum == NPROC) {
        panic("level 0 queue is full\n");
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

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
 
  // push into mlfq
  enqueue1(p);  

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

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
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

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
  
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

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

  acquire(&ptable.lock);

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
  
  acquire(&ptable.lock);
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
        kfree(p->kstack);
        p->kstack = 0;
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
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    while(mlfq.size > 0) {
      p = selectproc();
      if(p == NULL) {
          // cprintf("no process!\n");
          break;
      }

      
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
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
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
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
    acquire(&ptable.lock);  //DOC: sleeplock1
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
  acquire(&ptable.lock);
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

  acquire(&ptable.lock);
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
      getcallerpcs((uint*)p->context->ebp+2, pc);
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

int
getlev(void)
{
    return myproc()->mlfqlev;
}

// int
// set_cpu_share(int share)
// {
//     struct proc *p;
//     int mlfqnum;
//
//     // CPU share of stride queue cannot exceed 80%
//     if(strideqshare + share > STRIDEMAX || strideq.size >= NPROC)
//         return -1;
//
//     // push into stride queue
//     strideqshare += share;
//     p = myproc();
//     p->mlfqlev = -2;
//     p->share = share;
//     p->stride = 100.0/share;
//     cprintf("set_cpu_share(%d%), stride : %d, pass : %d\n", share, (int)(p->stride), (int)(p->pass));
//     push(p);
//
//     // share value may be double?
//     // how about mlfq information? (e.g., mlfq.size, q.procnum)
//
//     mlfqnum = 0;
//     cprintf("strideqshare : %d\n", strideqshare);
//     mlfqshare = 100-strideqshare;
//
//     // change share and stride of process in mlfq
//     acquire(&ptable.lock);
//
//     cprintf("curproc's pid : %d\n", p->pid);
//     cprintf("curproc's state : %d\n", p->state);
//     // find the number of runnable process in mlfq
//     for(struct proc *i = ptable.proc; i < &ptable.proc[NPROC]; i++) {
//         int state;
//         if(i->mlfqlev >= 0) {
//             state = i->state;
//             if(state != UNUSED && state != ZOMBIE && state != RUNNING) {
//                 mlfqnum++;
//             }
//         }
//     }
//     cprintf("mlfqshare : %d mlfqnum : %d\n", mlfqshare, mlfqnum);
//     if(mlfqnum > 0) {
//         share = mlfqshare/mlfqnum;
//
//         for(struct proc *i = ptable.proc; i < &ptable.proc[NPROC]; i++) {
//             int state;
//             if(i->mlfqlev >= 0) {
//                 state = i->state;
//                 if(state != UNUSED && state != ZOMBIE && state != RUNNING) {
//                     i->share = share;
//                     i->stride = 100.0/share;
//                 }
//             }
//         }
//     }
//
//     release(&ptable.lock);
//     cprintf("set_cpu_share(%d%) fin\n", share);
//     return 0;
// }

int
set_cpu_share(int share)
{
    struct proc *p, *sp;
    double minpass;

    if(strideq.size == NPROC) {
        panic("stride queue full\n");
        return -1;
    }

    if(share > strideqticket) {
        panic("no ticket left\n");
        return -1;
    }

    strideqticket -= share;
    p = myproc();
    p->mlfqlev = -2;
    p->share = share;
    p->stride = 100.0/share;

    minpass = 10000000.0;
    if(mlfq.pass < minpass) {
        minpass = mlfq.pass;
    } 

    for(int i = 1; i <= strideq.size; i++) {
        sp = strideq.arr[i].p;
        if(sp->mlfqlev == -2 && sp->state != ZOMBIE && sp->state != UNUSED) {
            if(strideq.arr[i].pass < minpass) {
                minpass = strideq.arr[i].pass;
            }
        }
    }
    // cprintf("minpass : %d\n", (int)minpass);
    push(p, minpass);
    return 0;
}
