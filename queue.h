#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

#define QUEUE_CAPACITY 64

typedef struct {
    int items[QUEUE_CAPACITY];
    int head;             // index to pop from next
    int tail;              // index to push into next
    int count;             // how many items currently in the queue
    pthread_mutex_t lock;  // protects head, tail, count, items
} queue_t;

void queue_init(queue_t *q);
int  queue_push(queue_t *q, int fd);   // returns 0 on success, -1 if queue is full
int  queue_pop(queue_t *q);            // returns fd on success, -1 if queue is empty
void queue_print(queue_t *q);          // debug helper
void queue_destroy(queue_t *q);

#endif