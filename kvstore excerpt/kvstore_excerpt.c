/* Excerpt from kvstore.c
   Shows: table init, worker queue, GET with read lock, SET with write lock, DEL with write lock
   Context: global hashtable_t* ht initialised in main, capacity 256 buckets
*/

#define HT_CAPACITY 256

// init hashtable and worker pool
hashtable_t *init_hashtable(void) {
    hashtable_t *ht = malloc(sizeof(hashtable_t));
    if (!ht) return NULL;

    ht->capacity = HT_CAPACITY;
    ht->items = calloc(HT_CAPACITY, sizeof(hash_item_t*));
    if (!ht->items) { free(ht); return NULL; }

    ht->user = malloc(sizeof(struct user_ht));
    if (!ht->user) { free(ht->items); free(ht); return NULL; }

    ht->user->bucket_locks = malloc(HT_CAPACITY * sizeof(pthread_mutex_t));
    for (int i = 0; i < HT_CAPACITY; i++) pthread_mutex_init(&ht->user->bucket_locks[i], NULL);

    pthread_mutex_init(&ht->user->queue_lock, NULL);
    pthread_cond_init(&ht->user->queue_cond, NULL);
    ht->user->job_queue = calloc(MAX_QUEUE_SIZE, sizeof(struct conn_info*));
    ht->user->queue_head = ht->user->queue_tail = ht->user->queue_size = 0;
    ht->user->shutdown = 0;
    atomic_init(&ht->user->request_count, 0);
    atomic_init(&ht->user->start_time, time(NULL));

    ht->user->worker_threads = malloc(THREAD_POOL_SIZE * sizeof(pthread_t));
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&ht->user->worker_threads[i], NULL, worker_thread, ht);

    return ht;
}

// worker loop with bounded queue
void *worker_thread(void *arg) {
    hashtable_t *ht = (hashtable_t*)arg;
    for (;;) {
        struct conn_info *conn = NULL;

        pthread_mutex_lock(&ht->user->queue_lock);
        while (ht->user->queue_size == 0 && !ht->user->shutdown)
            pthread_cond_wait(&ht->user->queue_cond, &ht->user->queue_lock);

        if (ht->user->shutdown && ht->user->queue_size == 0) {
            pthread_mutex_unlock(&ht->user->queue_lock);
            break;
        }

        conn = ht->user->job_queue[ht->user->queue_head];
        ht->user->queue_head = (ht->user->queue_head + 1) % MAX_QUEUE_SIZE;
        ht->user->queue_size--;
        pthread_mutex_unlock(&ht->user->queue_lock);

        if (conn) main_job(conn);
    }
    return NULL;
}

// safe GET with read lock
int get_request(int socket, struct request *request) {
    unsigned int bucket = hash(request->key) % ht->capacity;

    pthread_mutex_lock(&ht->user->bucket_locks[bucket]);
    hash_item_t *item = ht->items[bucket];
    while (item && strcmp(item->key, request->key) != 0) item = item->next;
    if (!item) { pthread_mutex_unlock(&ht->user->bucket_locks[bucket]); send_response(socket, KEY_ERROR, 0, NULL); return -1; }

    // take reader lock then release bucket mutex
    pthread_rwlock_rdlock(&item->user->rwlock);
    pthread_mutex_unlock(&ht->user->bucket_locks[bucket]);

    char *value = NULL; size_t n = 0;
    if (item->value && item->value_size > 0) {
        value = malloc(item->value_size);
        if (!value) { pthread_rwlock_unlock(&item->user->rwlock); send_response(socket, STORE_ERROR, 0, NULL); return -1; }
        memcpy(value, item->value, item->value_size);
        n = item->value_size;
    }
    pthread_rwlock_unlock(&item->user->rwlock);

    atomic_fetch_add(&ht->user->request_count, 1);
    send_response(socket, OK, n, value);
    free(value);
    return 0;
}

// safe SET with write lock and refind
int set_request(int socket, struct request *request) {
    unsigned int bucket = hash(request->key) % ht->capacity;
    size_t expected = request->msg_len;

    // read payload first
    char *buf = expected ? malloc(expected) : NULL;
    if (expected && !buf) { send_response(socket, STORE_ERROR, 0, NULL); return -1; }
    size_t got = 0;
    while (got < expected) {
        ssize_t r = read_payload(socket, request, expected - got, buf + got);
        if (r <= 0) { free(buf); request->connection_close = 1; return -1; }
        got += (size_t)r;
    }
    if (check_payload(socket, request, expected) < 0) { free(buf); request->connection_close = 1; return -1; }

    pthread_mutex_lock(&ht->user->bucket_locks[bucket]);

    // refind or create item while holding the bucket
    hash_item_t *item = ht->items[bucket];
    while (item && strcmp(item->key, request->key) != 0) item = item->next;
    if (!item) {
        item = calloc(1, sizeof(hash_item_t));
        if (!item) { pthread_mutex_unlock(&ht->user->bucket_locks[bucket]); free(buf); send_response(socket, STORE_ERROR, 0, NULL); return -1; }
        item->key = strdup(request->key);
        pthread_rwlock_init(&item->user->rwlock, NULL);
        item->next = ht->items[bucket];
        if (item->next) item->next->prev = item;
        ht->items[bucket] = item;
    }

    // take writer lock for value swap
    pthread_rwlock_wrlock(&item->user->rwlock);
    char *old = item->value;
    item->value = buf;
    item->value_size = got;
    pthread_rwlock_unlock(&item->user->rwlock);

    pthread_mutex_unlock(&ht->user->bucket_locks[bucket]);
    free(old);

    atomic_fetch_add(&ht->user->request_count, 1);
    send_response(socket, OK, 0, NULL);
    return (int)got;
}

// DEL with write lock
int del_request(int socket, struct request *request) {
    unsigned int bucket = hash(request->key) % ht->capacity;

    pthread_mutex_lock(&ht->user->bucket_locks[bucket]);
    hash_item_t *item = ht->items[bucket];
    while (item && strcmp(item->key, request->key) != 0) item = item->next;
    if (!item) { pthread_mutex_unlock(&ht->user->bucket_locks[bucket]); send_response(socket, KEY_ERROR, 0, NULL); return -1; }

    pthread_rwlock_wrlock(&item->user->rwlock);

    if (item->prev) item->prev->next = item->next;
    else ht->items[bucket] = item->next;
    if (item->next) item->next->prev = item->prev;

    pthread_rwlock_unlock(&item->user->rwlock);
    pthread_mutex_unlock(&ht->user->bucket_locks[bucket]);

    pthread_rwlock_destroy(&item->user->rwlock);
    free(item->user); free(item->key); free(item->value); free(item);

    atomic_fetch_add(&ht->user->request_count, 1);
    send_response(socket, OK, 0, NULL);
    return 0;
}
