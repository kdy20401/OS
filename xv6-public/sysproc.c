#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// wrapper function for getppid()
int
sys_getppid()
{
    return getppid();
}

// wrapper function for yield()
void
sys_yield()
{
    return yield();
}

// wrapper function for getlev()
int
sys_getlev()
{
    return getlev();
}

// wrapper function for set_cpu_share(int share)
int
sys_set_cpu_share(void)
{
    int p;

    if(argint(0, &p) < 0)
        return -1;

    return set_cpu_share(p);
}

// wrapper function for
// thread_create(thread_t *thread, void * (*start_routine)(void *), void *arg)
int
sys_thread_create(void)
{
    thread_t *thread;
    void * (*start_routine)(void *);
    void *arg;
    
    if(argptr(0, (void *)&thread, sizeof(thread_t*)) < 0)
        return -1;
    if(argptr(1, (void *)&start_routine, sizeof(start_routine)) < 0)
        return -1;
    if(argptr(2, (void *)&arg, sizeof(void*)) < 0)
        return -1;

    return thread_create(thread, start_routine, arg);
};

// wrapper function for
// thread_exit(void *retval)
void
sys_thread_exit(void)
{
    void *retval;
    
    if(argptr(0, (void *)&retval, sizeof(void*) < 0))
        return;
    
    return thread_exit(retval);
}

// wrapper function for
// thread_join(thread_t thread, void **retval)
int
sys_thread_join(void)
{
    thread_t thread;
    void **retval;

    if(argint(0, &thread) < 0)
        return -1;
    if(argptr(1, (void *)&retval, sizeof(void*) < 0))
        return -1;
    
    return thread_join(thread, retval);
}
