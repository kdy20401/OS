#include "types.h"
#include "stat.h"
#include "user.h"

#define THREAD_NUM 1

void*
thread_routine(void *arg)
{
    printf(1, "thread_routine starts\n");
    thread_exit();
}

int
main(int argc, char *argv[])
{
    int thread_arr[THREAD_NUM]; 
    int thread_arg[THREAD_NUM];

    for(int i = 0; i < THREAD_NUM; i++) {
        thread_arr[i] = i;
    }
    for(int i = 0; i < THREAD_NUM; i++) {
        thread_arr[i] = i+1;
    }
    for(int i = 0; i < THREAD_NUM; i++) {
        thread_create(&thread_arr[i], thread_routine, (void*)&thread_arg[i]);
    }

    exit();
}
