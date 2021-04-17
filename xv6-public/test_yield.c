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
        printf(1, "Child Process Created!\n");
        for(int i = 0; i < 3; i++)
        {
            printf(1, "Child\n");
            // yield();
        }
    }
    else
    {
        printf(1, "Parent Process Created!\n");
        for(int i = 0; i < 3; i++)
        {
            printf(1, "Parent\n");
            // yield();
        }
        wait();
    }
   
    exit();
}
