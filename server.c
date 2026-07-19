
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "token_bucket.h"
#include "queue.h"

#define PORT 8080
#define BACKLOG 5
#define NUM_WORKERS 4

// Bundles what every worker thread needs — pthread_create only takes one void* arg
typedef struct {
    queue_t *q;
    token_bucket_t *bucket;
} worker_ctx_t;

void *worker_fn(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;

    while (1) {
        int client_fd = queue_pop(ctx->q); // see note below on blocking vs polling

        if (client_fd < 0) {
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 }; // 1,000,000 ns = 1ms
              nanosleep(&ts, NULL); // temporary: queue was empty, avoid a hot spin loop
            continue;
        }

        printf("[thread %lu] handling fd %d\n", (unsigned long)pthread_self(), client_fd);

        if (allow_request(ctx->bucket)) {
            send(client_fd, "ALLOWED\n", 8, 0);
        } else {
            send(client_fd, "DENIED\n", 7, 0);
        }
        close(client_fd);
    }
    return NULL;
}

int main(void) {
    token_bucket_t shared_bucket;
    bucket_init(&shared_bucket, 5.0);

    queue_t q;
    queue_init(&q);

    worker_ctx_t ctx = { .q = &q, .bucket = &shared_bucket };

    pthread_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
            int rc = pthread_create(&workers[i], NULL, worker_fn, &ctx);
            if (rc != 0) {
                fprintf(stderr, "pthread_create failed: %d\n", rc);  // ADD THIS
            }
    }

    int server_fd;
    struct sockaddr_in address;
    struct sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("Socket creation failed"); exit(EXIT_FAILURE); }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); close(server_fd); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("Listen failed"); close(server_fd); exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d with %d workers\n", PORT, NUM_WORKERS);

    // Accept loop now does ONLY this — no bucket logic, no send, no close
    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_addr_len);
        if (client_fd < 0) continue;
        queue_push(&q, client_fd);
    }

    close(server_fd);
    pthread_mutex_destroy(&shared_bucket.lock);
    return 0;
}