#ifndef TOKEN_BUCKET_H
#define TOKEN_BUCKET_H

#include <stdbool.h>
#include <pthread.h>

typedef struct {
    double tokens;
    double capacity;
    pthread_mutex_t lock;
} token_bucket_t;

void bucket_init(token_bucket_t *bucket, double capacity);

bool allow_request(token_bucket_t *bucket);

#endif