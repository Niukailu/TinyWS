#include "locker.h"

Sem::Sem() {
    //sem_init 函数成功时返回 0，否则返回 -1 并设置 errno 错误码
    if(sem_init(&m_sem, 0, 0) != 0) {
        throw std::exception();  //没有catch?
    }
}

Sem::Sem(int num) {
    if(sem_init(&m_sem, 0, num) != 0) {
        throw std::exception();  
    }
}

bool Sem::wait() {
    //如果信号量的值大于 0，则将其减 1 并立即返回；
    //如果信号量的值为 0，则线程将被阻塞挂起，直到有其他线程调用 sem_post 函数释放信号量
    return sem_wait(&m_sem) == 0;
}

bool Sem::post() {
    return sem_post(&m_sem) == 0;
}

Sem::~Sem() {
    sem_destroy(&m_sem);
}


Locker::Locker() {
    if(pthread_mutex_init(&m_mutex, NULL) != 0) {
        throw std::exception();
    }
}

bool Locker::lock() {
    //如果互斥锁已经被其他线程所持有，则当前线程将被阻塞
    return pthread_mutex_lock(&m_mutex) == 0;
}

bool Locker::unlock() {
    return pthread_mutex_unlock(&m_mutex) == 0;
}

pthread_mutex_t* Locker::get() {
    return &m_mutex;
}

Locker::~Locker() {
    pthread_mutex_destroy(&m_mutex);
}

Cond::Cond() {
    if(pthread_cond_init(&m_cond, NULL) != 0) {
        throw std::exception();
    }
}

bool Cond::wait(pthread_mutex_t *m_mutex) {
    //在等待条件变量之前，线程必须先获取互斥锁，以保证线程安全。
    //在等待条件变量（被signal或broadcast）期间，线程将阻塞在此处，直到满足条件变量的条件为止,
    //同时，互斥锁将被释放，以允许其他线程访问共享资源
    return pthread_cond_wait(&m_cond, m_mutex) == 0;  //成功返回0
}

bool Cond::time_wait(pthread_mutex_t *m_mutex, struct timespec t) {
    //可以设置等待的超时时间
    return pthread_cond_timedwait(&m_cond, m_mutex, &t) == 0;
}

bool Cond::signal() {
    //用于唤醒一个等待条件变量的线程，条件变量的状态已经发生了改变，可以继续执行了
    return pthread_cond_signal(&m_cond) == 0;
}

bool Cond::broadcast() {
    return pthread_cond_broadcast(&m_cond) == 0;
}

Cond::~Cond() {
    pthread_cond_destroy(&m_cond);
}

