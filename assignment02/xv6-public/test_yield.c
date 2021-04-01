#include "types.h"
#include "stat.h"
#include "user.h"

int main()
{
    int r;

    r = fork();

    if(r < 0)
    {
        printf(1, "fork() error\n");    
    }
    else if(r == 0)
    {
        while(1)
        {
            printf(1, "Child\n");
            yield();
        }
    }
    else
    {
        while(1)
        {
            printf(1, "Parent\n");
            yield();
        }
        wait();
    }
   
    exit();
}
