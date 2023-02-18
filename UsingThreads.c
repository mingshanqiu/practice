// ph.c
pthread_mutex_t lock;

/*
    粗粒度加锁
*/

int 
main(int argc, char *argv[]) {
    pthread_t *tha;
    void *value;
    double t1, t0;

    pthread_mutex_init(&lock, NULL);

    // ...
}

static 
void 
put(int key, int value) {
    NBUCKET;

    pthread_mutex_lock(&lock);

    // ......

    pthread_mutex_unlock(&lock);
}

/*
    注意只需要对put加锁（其实是对里面的insert加锁），get是读数据操作，不会发生由于竞态条件而数据错误问题，是不需要加锁的

    get() 函数主要是遍历 bucket 链表找寻对应的 entry, 并不会对 bucket 链表进行修改, 实际上只是读操作, 因此无需加锁.
*/

/*
    优化：减小锁粒度，提升多线程的速度
    在哈西表中，不同bucket是互不影响的，一个bucket处于修改未完全状态并不影响put对其他bucket的操作。
    实际上只需要确保两个线程不会同时操作同一个bucket即可，不需要确保不会同时操作整个哈希表
*/

// ph.c
int main(int argc, char *argv[]) {
    pthread_t *tha;
    void *value;
    double t1, t0;

    for (int i = 0; i < NBUCKET; ++i)
        pthread_mutex_init(&locks[i], NULL);

    // ......
}

static void
put(int key, int value) {
    int i = key % NBUCKET;

    pthread_mutex_lock(&lock[i]);

    // ......

    pthread_mutex_unlock(&lock[i]);
}