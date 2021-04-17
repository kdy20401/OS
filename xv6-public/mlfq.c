#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "user.h"
#include "proc.h"

void
initmlfq(void)
{
    for(int i = 0; i < LEVEL; i++)
    {
        mlfq.queues[i].head = NULL;
        mlfq.queues[i].tail = NULL;
        mlfq.queues[i].procnum = 0;
        mlfq.queues[i].tick = 0;
    }
   
    mlfq.queues[0].timeslice = 1;
    mlfq.queues[1].timeslice = 2;
    mlfq.queues[2].timeslice = 4;

    mlfq.queues[0].timeallotment = 5;
    mlfq.queues[1].timeallotment = 10;
    mlfq.queues[2].timeallotment = 210000000;    

}

// make node and push to highest queue
void
enqueue(struct proc *p)
{
    struct queue *q;
    struct node *newnode;

    p->mlfqlev = 0;
    q = &mlfq.queues[0];
    newnode = (struct node*)malloc(sizeof(struct node));
    newnode->p = p;
    newnode->qlevel = 0;
    newnode->tick = 0;
    newnode->next = NULL;

    if(q->head == NULL){
        q->head = newnode;
        q->tail = newnode;
    } else{
        q->tail->next = newnode;
        q->tail = newnode;
    }
    q->procnum++;
}
// remove process from mlfq which is not runnable
//
void
dequeue(struct queue *q, struct node *n)
{
    n->p->mlfqlev = -1;
    q->head = q->head->next;
    if(q->head == NULL)
        q->tail = NULL;
    q->procnum--;
    free(n);
}

int
ismlfqempty(void)
{
    for(int i = 0; i < LEVEL; i++) {
        if(mlfq.queues[i].procnum != 0)
            return 0;

    return 1;
}

// move process from curlevel queue to one below queue
void
decrease(struct node *n, int curlevel)
{
    struct queue *curq, *nextq;
    struct node *newhead;

    if(curlevel == LEVEL-1)
        return;
   
    // remove process from the current level queue
    curq = &mlfq.queues[curlevel];

    if(curq->procnum == 1) {
        curq->head = NULL;
        curq->tail = NULL;
    }else {
        newhead = curq->head->next;
        curq->head = newhead;
    }
    curq->procnum--;
    
    // move process to next below queue
    nextq = &mlfq.queues[curlevel+1];
    n->qlevel = curlevel+1;
    n->p->mlfqlev = n->qlevel;
    n->tick = 0;
    n->next = NULL;
 
    if(nextq->head == NULL){
        nextq->head = n;
        nextq->tail = n;
    } else{
        nextq->tail->next = n;
        nextq->tail = n;
    }
    nextq->procnum++;   
}

// get a process of the highest priority in mlfq
struct proc*
top(void)
{
    int i, ticksum;
    struct queue *q;
    struct node *n;

    i = 0;
    
    // find a level of queue where the process of the highest priority exists
    while(i < LEVEL-1 && mlfq.queues[i].procnum == 0)
        i++;
    q = &mlfq.queues[i];
    n = q->head;
    
    // process is no longer runnable : remove from mlfq
    if(n->p->state != RUNNABLE) {
        dequeue(q, n);  
        return NULL;
    }

    q->tick++;
    n->tick++;
    
    // move process to queue one level below
    // if the process stays as much as queue's time allotment
    if(n->tick >= q->timeallotment)
        decrease(n, i);
    // move process to tail at the every queue's timeslice 
    else if(q->tick % q->timeslice == 0) {
        if(n->next) {
            q->head = n->next;
            q->tail->next = n;
            q->tail = n;
    }
    
    // move all processes to the topmost queue at the every boost time(100 tick)
    ticksum = 0;
    for(int l = 0; l < LEVEL; l++)
        ticksum += mlfq.queues[l].tick;
    if(ticksum == BOOSTTIME)
        boost();

    return n->p;
}

// to prevent starvation, move all process up to the highest queue
void boost(void)
{
    struct node *n;
    struct node *tail;
    int i;

    // modify queue information
    mlfq.queues[0].tick = 0;
    for(int i = 1; i < LEVEL; i++) {
        mlfq.queues[0].procnum += mlfq.queues[i].procnum;
        mlfq.queues[i].tick = 0;
        mlfq.queues[i].procnum = 0;
    }
    
    // modify node information
    for(i = 1; i < LEVEL; i++) {
        n = mlfq.queues[i].head;
        while(n != NULL) {
            n->p->mlfqlev = 0;
            n->qlevel = 0;
            n->tick = 0;
            n = n->next;
        }
    }
    
    // combine to queue0
    // 1. find the head of queue0
    i = 0;
    if(mlfq.queues[0].head == NULL) {
        for(i = 1; i < LEVEL; i++) {
            if(mlfq.queues[i].head != NULL) {
                mlfq.queues[0].head = mlfq.queues[i].head;
                break;
            }
        }
        // exception handling for i = level?
    }
    
    // 2. find the tail and link it to under level head
    tail = mlfq.queues[i].tail;
    for(int j = i + 1; j < LEVEL; j++) {
        if(mlfq.queues[j].head != NULL) {
            tail->next = mlfq.queues[j].head;
            tail = mlfq.queues[j].tail;
        }else {
            continue;
        }
    }
    mlfq.queues[0].tail = tail; 
}


