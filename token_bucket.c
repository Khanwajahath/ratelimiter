#include "token_bucket.h"

void bucket_init(token_bucket_t *bucket, double capacity) {
    bucket->tokens = capacity;
    bucket->capacity = capacity;
    pthread_mutex_init(&bucket->lock, NULL);
}

bool allow_request(token_bucket_t *bucket) {
    bool allowed = false;

    pthread_mutex_lock(&bucket->lock);

    if (bucket->tokens >= 1.0) {
        bucket->tokens -= 1.0;
        allowed = true;
    }

    pthread_mutex_unlock(&bucket->lock);

    return allowed;
}