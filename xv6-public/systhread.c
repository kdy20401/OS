#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
// #include "thread.h"

int
sys_thread_create(void)
{
    thread_t *thread;
    void * (*start_routine)(void *);
    void *arg;

    if(argptr(0, (void *)&thread, sizeof(thread[0])) < 0)
        return -1;
    if(argptr(1, (void *)&start_rouine, sizeof(start_routine)) < 0)
        return -1;
    if(argptr(2, (void *)&arg, sizeof(int)) < 0) // sizeof(int) right?
        return -1;
  
    // for debugging
    printf(1, "thread : %ld, arg : %d\n", *thread, *(int*)arg);

    return thread_create(thread, start_routine, arg);
};

int
sys_thread_exit(void)
{
    void *retval;
    
    if(argptr(0, (void *)&retval, sizeof(int)) < 0)
        return -1;

    return thread_exit(retval);
}

int
sys_thread_join(void)
{
    thread_t thread;
    void **retval;

    if(argint(0, &thread) < 0)
        return -1;
    if(argptr(1, (void *)&retval, sizeof(int) < 0)
        return -1;

    return thread_join(thread, retval);
}
