#include <stdio.h>
#include <stdlib.h>


struct node {
    int val;
    struct node *next;
}node;

int arr[] = {
[1] 1,
[2] 2,
[3] 3,
};

int main(int argc, char *argv[])
{
    for(int i = 1; i <= 3; i++)
    {
        printf("%d\n", arr[i]);
    }
    return 0;
}
