#include <stdio.h>
#include "queue.h"

void queue_init(queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
}

int queue_push(queue_t *q, int fd) {
    int result = -1;

    pthread_mutex_lock(&q->lock);
    if (q->count < QUEUE_CAPACITY) {
        q->items[q->tail] = fd;
        q->tail = (q->tail + 1) % QUEUE_CAPACITY;
        q->count++;
        result = 0;
    }
    pthread_mutex_unlock(&q->lock);

    return result;
}

int queue_pop(queue_t *q) {
    int fd = -1;

    pthread_mutex_lock(&q->lock);
    if (q->count > 0) {
        fd = q->items[q->head];
        q->head = (q->head + 1) % QUEUE_CAPACITY;
        q->count--;
    }
    pthread_mutex_unlock(&q->lock);

    return fd;
}

void queue_print(queue_t *q) {
    pthread_mutex_lock(&q->lock);
    printf("queue: count=%d head=%d tail=%d [", q->count, q->head, q->tail);
    for (int i = 0; i < q->count; i++) {
        int idx = (q->head + i) % QUEUE_CAPACITY;
        printf("%d ", q->items[idx]);
    }
    printf("]\n");
    pthread_mutex_unlock(&q->lock);
}

void queue_destroy(queue_t *q) {
    pthread_mutex_destroy(&q->lock);
}