#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"
#include "thread.h"

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

    for(t = thrtable->threads; t < &thrtable->threads[NTHREAD]; t++) {
        if(t->state == UNUSED)
            goto found;
    }
    
    release(&thdtable->lock);
    return 0;

found:
    t->state = EMBRYO;
    t->parent = curproc;
    
    release(&thdtable->lock);

    // allocate kernel stack
    if((t->kstack = kalloc()) == 0) {
        t->state = UNUSED;
        return 0;
    }
    sp = t->kstack + KSTACKSIZE;

    // leave a room for trap frame
    sp -= sizeof *curproc->thread->tf;
    t->thread->tf = (struct trapframe*)sp;

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
    // things to do?
    return;
}

int
thread_create(thread_t *thread, void * (*start_routine)(void *), void *arg)
{
    struct proc *curproc;
    struct thread *t;
    struct thrtable thrtable;
    pde_t *pgdir;
    uint sz, sp, ustack[2], org_retaddr;

    curproc = myproc();

    // allocate kernel stack
    if((t = allocthread()) == 0)
        return -1;
    
    t->tid = *thread;
    curproc->curtid = *thread; // id of current thread in execution
    *t->tf = *curproc->thread->tf;
    org_retaddr = t->tf->eip; // address of instruction after thread_create
    
    // allocate user stack
    sz = curproc->sz;
    sz = PGROUNDUP(sz);
    pgdir = curproc->pgidr;

    // one for stack, the other for guard page
    if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
        goto bad;
    clearpteu(pgdir, (char*)(sz - 2*PGSIZE));

    sp = sz;
    sp -= 2 * 4;
 
    ustack[0] = org_retaddr; // address of instruction after thread_create
    ustack[1] = (uint)arg; // right?
    if(copyout(pgdir, sp, ustack, 2 * 4) < 0)
        goto bad;

    curproc->sz = sz;

    // change execution flow by changing pc and stack pointer
    t->tf->eip = (uint)start_routine;
    t->tf->esp = sp;
    
    return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  // if(ip){
  //   iunlockput(ip);
  //   end_op();
  // }
  return -1;
}


void
thread_exit(void *retval)
{
    struct proc *curproc;
    struct thdtable *thdtable;
    struct thread *t;
    int tid;
    int flag;

    curproc = myproc(); // returns parent process??
    tid = curproc->curtid; // where designate curtid?
    thdtable = &curproc->thdtable;

    acquire(&thdtable.lock);
    
    // find current thread in execution
    for(t = thdtable->threads; t < thdtable->threads[NTHREAD]; t++) {
        if(t->tid == tid) {
            flag = 1;
            break;
        }
    }
    
    // for debug
    if(!flag) {
        panic("thread_exit");
    }
    
    // wake up process who might be waiting 
    wakeup1(curproc);

    // change state of thread
    t->state = ZOMBIE;
    t->retval = retval;
    sched();
    panic("zombie thread");
}

int
thread_join(thread_t thread, void **retval)
{
    struct proc *curproc;
    struct thrtable *thdtable;
    struct thread *t;
    int havekids, pid, tid;
    
    curproc = myproc();
    thdtable = &curproc->thrtable;
    
    acquire(&thdtable->lock);
    for(;;) {
        havekids = 0;
        for(t = thdtable->threads; t < thdtable->threads[NTHREAD]; t++) {
            if(t->tid != thread)
                continue;
            havekids = 1;
            if(t->state == ZOMBIE) {
                // found one
                kfree(t->stack);
                t->stack = 0;
                t->tid = 0;
                t->parent = 0;
                t->state = UNUSED;
                *retval = t->retval; // possible?
                release(&thdtable->lock);
                return 0;
            }
        }

        if(!havekids || curproc->killed) {
            release(&thdtable->lock);
            return -1;
        }
        
        sleep(curproc, &thdtable->lock);
    }
}


void
sched2(struct thread *curthd, int i)
{
    struct proc *curproc;
    struct thdtable *thdtable;
    struct thread *t;
    int j;

    curproc = myproc();
    thdtable = &curproc->thdtable;

    // find next thread
    j = (i + 1) % NTHREAD;
  
    while(j != i) {
        t = &thdtable->threads[j]; 
        if(t->state == RUNNABLE) {
            break;
        }
        j++;
        j = j % NTHREAD;
    }
    
    if(j == i) {
        t = &thdtable->threads[j];
    }

    t->state = RUNNING;

    // copy the context of thread to proc
    // deadlock?
    acquire(&ptable.lock);
    curproc->thread->context = t->context;
    curproc->state = t->state;
    curproc->thread->kstack = t->kstack;
    curproc->thread->tf = t->tf;
    release(&ptable.lock);

    swtch(&curthd->context, &t->context);
}


void
yield2(void)
{
    struct proc *curproc;
    struct thdtable *thdtable;
    struct thread *curthd;
    int i;

    curproc = myproc();
    thdtable = &curproc->thdtable;
    
    acquire(&thdtable->lock);
    
    // find current working thread
    for(i = 0; i < NTHREAD; i++) {
        curthd = &thdtable->threads[i];
        if(curthd->tid == curproc->tid) {
            curthd->state = RUNNABLE;
            break;
        }
    }
    
    sched2(curthd, i);
    release(&thdtable->lock);
}
