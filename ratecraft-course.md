# Build Your Own Rate Limiter — RateCraft

> A CodeCrafters-style, step-by-step course for building a multi-threaded,
> TCP-based token-bucket rate limiter in C.

## How this course works

Each **step** below is self-contained. It gives you:

- **Context** — why this step exists and how it fits the bigger picture
- **📚 Concept to review** — the one thing you should understand *before* writing code, with a short explanation inline so you're not left to go hunting
- **Your task** — precisely what to build
- **Tester expects** — how you'd know the step is done (framed as automated checks, CodeCrafters-style)
- **Hints** — pitfalls specific to that step

Steps are grouped into 4 **Stages**, matching your original roadmap. Do them in order — each one depends on the last compiling and passing.

---

## Stage 1 — Core In-Memory Token Bucket Engine

**Big picture:** by the end of this stage you'll have a single-file, thread-safe data structure that can answer "is this IP allowed to make a request right now?" — with no networking yet. Get the concurrency primitive rock-solid before you add sockets on top of it.

### Step 1.1 — Define the bucket struct

**Context:** Every rate limiter needs a compact piece of state per client: how many tokens they have left, and the max they can hold. You'll build everything else around this struct.

**📚 Concept to review — Fixed vs. floating-point counters**
Token counts are often modeled as `double` rather than `int`, because refill happens continuously ("2.5 tokens per second"), not in whole-number ticks. Using floats means you'll eventually hit rounding/drift issues — worth knowing now, not discovering later.

**Your task:**
Create `ratecraft_core.c` and define:
```c
typedef struct {
    double tokens;
    double capacity;
    pthread_mutex_t lock;
} token_bucket_t;
```

**Tester expects:** File compiles with `gcc -c ratecraft_core.c` (no `main()` needed yet — just a clean compile with the struct defined and `<pthread.h>`, `<stdint.h>` included).

**Hints:** Don't add the refill rate to this struct yet — that comes in Stage 3. Keep this step minimal.

---

### Step 1.2 — Initialize a bucket

**Context:** Uninitialized `pthread_mutex_t` values are undefined behavior. You need an explicit init function, same as you'd `malloc` + zero any other resource.

**📚 Concept to review — `pthread_mutex_init`**
A mutex must be initialized before use and destroyed when done (`pthread_mutex_destroy`), or you leak OS resources. `PTHREAD_MUTEX_INITIALIZER` is a static alternative but doesn't work well for heap-allocated or dynamically-sized bucket arrays — so use the function form here.

**Your task:**
```c
void bucket_init(token_bucket_t *bucket, double capacity) {
    bucket->tokens = capacity;
    bucket->capacity = capacity;
    pthread_mutex_init(&bucket->lock, NULL);
}
```

**Tester expects:** A `main()` that calls `bucket_init(&b, 5.0)` and asserts `b.tokens == 5.0 && b.capacity == 5.0`.

**Hints:** The bucket starts *full* — this matches real-world rate limiters, where a fresh client gets a burst allowance immediately.

---

### Step 1.3 — Implement `allow_request()` (no locking yet)

**Context:** Get the core decision logic right before you worry about thread safety. Prove it works single-threaded first.

**📚 Concept to review — Critical sections**
A "critical section" is any code that reads-then-writes shared state. `if (tokens >= 1) tokens -= 1;` is a critical section: two threads could both pass the `if` check before either decrements, over-spending the bucket. You don't need to fix this yet — just recognize *why* Step 1.4 exists.

**Your task:**
```c
bool allow_request(token_bucket_t *bucket) {
    if (bucket->tokens >= 1.0) {
        bucket->tokens -= 1.0;
        return true;
    }
    return false;
}
```

**Tester expects:** With `capacity = 3`, three sequential calls to `allow_request()` return `true`, the fourth returns `false`.

**Hints:** This version is intentionally *not* thread-safe. That's the point of the next step — don't skip ahead.

---

### Step 1.4 — Add the mutex

**Context:** Now make the critical section from 1.3 safe for concurrent callers.

**📚 Concept to review — `pthread_mutex_lock` / `unlock`**
Locking around the *entire* check-and-decrement (not just the decrement) is what makes this atomic from the perspective of other threads — the whole "check, then act" sequence becomes indivisible. Always unlock on every return path, including early returns, or you'll deadlock the next caller.

**Your task:**
```c
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
```

**Tester expects:** Identical single-threaded output to Step 1.3 (locking shouldn't change behavior with one thread — only under contention, which you test next).

**Hints:** A common bug: returning early *inside* the locked section without unlocking first. Structure the function so there's exactly one unlock path, like above.

---

### Step 1.5 — The 10-thread competition test

**Context:** This is where you *prove* the mutex works, rather than assume it.

**📚 Concept to review — `pthread_create` / `pthread_join`**
`pthread_create` spawns a thread running a given function; `pthread_join` blocks the caller until that thread finishes. You need to join all 10 threads before checking results, or you'll read `success_count` before every thread has finished writing to it.

**Your task:**
- One `token_bucket_t` with `capacity = 5`, no refill running.
- Spawn 10 threads; each calls `allow_request()` 1000 times in a loop, incrementing a shared `success_count` (behind a lock, or via `atomic_int`) each time it returns `true`.
- Join all threads.

**Tester expects:** `success_count == 5` exactly, every run, and `bucket.tokens` prints as `0.000000` — never negative, never above 0.

**Hints:** If `success_count` sometimes reads 6 or 7, your `allow_request()` isn't actually locking (double check step 1.4). If it deadlocks, check for an unlock path you missed.

---

## Stage 2 — Multi-Worker TCP Server

**Big picture:** wrap Stage 1's bucket in a network-facing server so real clients can ask "am I allowed?" over a socket, handled concurrently by a fixed pool of workers rather than one thread per connection (which doesn't scale).

### Step 2.1 — Listen on a TCP port

**Context:** Before any concurrency, get a bare server that accepts one connection at a time and echoes back a canned response.

**📚 Concept to review — Sockets on Windows vs. POSIX**
On native Windows/MinGW you need `<winsock2.h>` and must call `WSAStartup()` before any socket call, then `WSACleanup()` at exit — this trips up almost everyone porting POSIX socket code. On WSL, standard `<sys/socket.h>` works unmodified. Decide now which environment you're targeting, since the includes and a few call signatures differ.

**Your task:** `socket()` → `bind()` → `listen()` on port `8080`, then a single blocking `accept()` in a loop, writing back `ALLOWED\n` unconditionally (bucket logic comes later).

**Tester expects:** `nc localhost 8080` (or `Test-NetConnection` on Windows) connects and receives `ALLOWED\n`.

**Hints:** Set `SO_REUSEADDR` so you can restart your server quickly during development without "address already in use" errors.

---

### Step 2.2 — Wire in `allow_request()`

**Context:** Connect Stage 1's engine to Stage 2's network layer — still single-threaded, still one global bucket (per-IP buckets come in Stage 3).

**Your task:** On each accepted connection, call `allow_request()` against one shared `token_bucket_t`, and write back `ALLOWED\n` or `DENIED\n` based on the result, then close the connection.

**Tester expects:** Hammering the server with `capacity = 5` and no refill: exactly the first 5 connections get `ALLOWED`, the rest get `DENIED`.

---

### Step 2.3 — Build the shared work queue

**Context:** A single `accept()` loop that also does the work is a bottleneck — the next client can't even connect until the current one is fully handled. You need to separate "accepting connections" from "processing them."

**📚 Concept to review — Producer/consumer via ring buffer**
A ring buffer (circular array with `head`/`tail` indices) lets one thread (the acceptor) push finished client sockets in, while worker threads pop them out, without reshuffling memory. It's a small, fixed-size FIFO — you don't need a dynamic queue library for this.

**Your task:** Implement a fixed-size ring buffer of `int` (socket file descriptors), with `queue_push()` and `queue_pop()` functions. Don't wire up threads yet — just unit-test the queue logic itself (push until full, pop until empty).

**Tester expects:** Pushing `N` items then popping `N` items returns them in FIFO order; pushing past capacity is handled (either blocks or rejects, your choice — document which).

---

### Step 2.4 — Spawn the worker thread pool

**Context:** Now put real threads on the consuming end of the queue.

**📚 Concept to review — Why a *pool*, not one thread per connection**
Spawning a new OS thread per connection works at small scale but costs real memory (default stack size is often 1-8MB per thread) and OS scheduling overhead at high concurrency. A fixed pool of 4-8 long-lived threads that pull work from a queue caps your resource usage regardless of client count — this is the same pattern behind most production web servers and DB connection pools.

**Your task:** At startup, spawn 4-8 worker threads. Each runs an infinite loop: pop a socket from the queue, read the request, call `allow_request()`, write the response, close the socket. Your `accept()` loop now only accepts and pushes — it never processes.

**Tester expects:** Server correctly serves 50 sequential connections using only the fixed pool (verify via logging which thread ID handled which connection — you should see all 4-8 thread IDs appear, never more).

**Hints:** Workers will busy-loop wastefully if the queue is empty and you don't block them — that's what Step 2.5 fixes.

---

### Step 2.5 — Condition variable wakeups

**Context:** Right now your workers either busy-spin (wasting CPU) or you haven't handled the empty-queue case at all. Fix that properly.

**📚 Concept to review — `pthread_cond_t` + mutex pairing**
A condition variable lets a thread sleep until *signaled*, instead of polling. It's always paired with a mutex: you lock, check a condition, and if false, call `pthread_cond_wait()` — which atomically unlocks and sleeps, then re-locks on wakeup. Watch for **spurious wakeups**: always re-check your condition in a `while` loop, never an `if`, after `cond_wait` returns.

**Your task:** Workers block on `pthread_cond_wait()` when the queue is empty. The acceptor thread calls `pthread_cond_signal()` after every `queue_push()`.

**Tester expects:** With no connections arriving, worker threads show ~0% CPU usage (verify with `top`/Task Manager) instead of spinning — and connections are still served with no added latency once one arrives.

---

## Stage 3 — Asynchronous Refiller & Janitor

**Big picture:** buckets need to regain tokens over time, and idle client entries need to be cleaned up so memory doesn't grow forever. Both happen on a background thread, without ever blocking client-facing requests for long.

### Step 3.1 — The ticker thread skeleton

**📚 Concept to review — `usleep` and drift**
`usleep(100000)` sleeps ~100ms, but OS scheduling means it's never exact — over thousands of iterations this drifts. For this stage, approximate timing is fine (it's a rate limiter, not a hard-real-time system), but note the assumption so you don't get surprised later if you add tests that assume exact timing.

**Your task:** Spawn one dedicated thread at startup that loops forever: sleep 100ms, then (for now) just print `"tick"`.

**Tester expects:** Log shows roughly 10 ticks per second, and the main server still answers client connections normally (proves the ticker doesn't block anything).

---

### Step 3.2 — Refill math

**Context:** Turn each tick into actual token replenishment.

**Your task:** Add a `refill_rate` field to `token_bucket_t` (tokens/sec). On each tick, for each bucket: `tokens = min(capacity, tokens + refill_rate * (elapsed_ms / 1000.0))`, under that bucket's lock.

**Tester expects:** Drain a bucket to 0, wait 2 seconds with `refill_rate = 1`, confirm `allow_request()` succeeds again (roughly 2 tokens should be available, ±1 for timing slop).

**Hints:** Use a monotonic clock (`clock_gettime(CLOCK_MONOTONIC, ...)`) rather than wall-clock time to compute elapsed time — wall-clock can jump backward (NTP adjustments, DST) and silently break your refill math.

---

### Step 3.3 — Multiple buckets, one lock each

**Context:** Right now you likely have one global bucket. Real rate limiting is per-IP — and per-IP means per-bucket locking, not one giant lock for everything.

**📚 Concept to review — Lock granularity**
A single global mutex protecting *all* buckets is simple but serializes every client against every other client, even ones the ticker isn't touching — a needless bottleneck. Locking each bucket individually (fine-grained locking) means the ticker refilling "IP-B" never blocks a client hitting "IP-A." The tradeoff: more locks to manage correctly, and you must never hold two bucket locks at once in a way that could deadlock (not a concern here since operations never touch two buckets at once).

**Your task:** Replace the single bucket with a simple hash table (or fixed-size array keyed by a hash of the IP string) of `token_bucket_t`, each with its own `pthread_mutex_t`. The ticker iterates all entries, locking only the one it's currently refilling.

**Tester expects:** Two different client IPs get independent token counts — draining IP-A's bucket doesn't affect IP-B's.

---

### Step 3.4 — Idle eviction (the "Janitor" half)

**Context:** Long-running servers will otherwise accumulate one bucket per IP forever. Clean up stale entries.

**Your task:** Track `last_seen` (timestamp) per bucket, updated on every `allow_request()` call. On the same ticker thread (or a second one), every few seconds, remove entries not seen in, say, 5 minutes.

**Tester expects:** After simulating an IP going idle past the threshold, its entry is gone from the table (verify via a debug `bucket_table_size()` count before/after).

---

## Stage 4 — Stress Testing & Chaos Verification

**Big picture:** deliberately try to break your own server — race conditions and deadlocks that hide at low concurrency often surface only under load.

### Step 4.1 — The chaos client

**📚 Concept to review — Load generation via client-side threads**
This mirrors Stage 2's server-side thread pool, but as a *client*: many threads, each opening real TCP connections and firing requests as fast as possible, to simulate many real users hitting you concurrently.

**Your task:** A separate program spawning 50+ threads, each opening hundreds of connections to your server in a tight loop, logging `ALLOWED`/`DENIED` counts per thread.

**Tester expects:** Server stays responsive throughout — no hung connections, no crashes — for a run of at least 10,000 total requests.

---

### Step 4.2 — Sanitizer build

**📚 Concept to review — ThreadSanitizer**
`-fsanitize=thread` instruments your binary to detect actual data races at runtime — not just deadlocks, but the subtler bug where two threads touch the same memory without synchronization, even if it "happens to work" most runs. This catches bugs your eyes will never spot in a code review.

**Your task:**
```
gcc -O0 -g -fsanitize=thread -std=c11 ratecraft.c -o ratecraft_tsan -pthread
```
Run your Stage 4.1 chaos client against this build.

**Tester expects:** Zero `WARNING: ThreadSanitizer: data race` output. Any race here means revisit Stage 1.4 or 3.3 — you're missing a lock somewhere.

---

### Step 4.3 — Invariant assertions under load

**Context:** Prove correctness numerically, not just "it didn't crash."

**Your task:** Add `assert(bucket->tokens >= 0.0);` inside `allow_request()` and the refill function. Run the full chaos client against an optimized build:
```
gcc -O2 -std=c11 ratecraft.c -o ratecraft -pthread
```

**Tester expects:** No assertion failures across a run of 50,000+ requests; total `ALLOWED` count across all IPs never exceeds what capacity + refill over the test duration mathematically allows.

---

### Step 4.4 (Boss level) — Kill and restart workers mid-flight

**Context:** Optional, if you want to go further than the original roadmap.

**Your task:** Have the chaos client randomly close its own sockets mid-request (simulating flaky clients disconnecting). Confirm the server doesn't leak file descriptors or crash on a `write()` to a closed socket (handle `SIGPIPE` / `EPIPE`).

**Tester expects:** Server's open file descriptor count (`lsof`-equivalent) returns to baseline after the chaotic client run ends.

---

## What's next after this course

Natural extensions once all 4 stages pass: persisting bucket state to survive restarts, exposing a `/stats` endpoint, or swapping the fixed IP hash table for a proper concurrent hash map if you outgrow the array-based version.
