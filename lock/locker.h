#ifndef __LOCKER_H__
#define __LOCKER_H__

#include <semaphore.h>
#include <pthread.h>

class Sem
{   
public:
    Sem();
    Sem(int num);

    bool wait();
    bool post();

    ~Sem();

private:
    sem_t m_sem;
};


class Locker
{  
public:
    Locker();

    bool lock();
    bool unlock();
    pthread_mutex_t* get();

    ~Locker();

private:
    pthread_mutex_t m_mutex;
};

//条件变量？
class Cond
{  
public:
    Cond();

    bool wait(pthread_mutex_t* m_mutex);
    bool timewait(pthread_mutex_t* m_mutex, struct timespec t);
    bool signal();
    bool broadcast();

    ~Cond();

private:
    pthread_cond_t m_cond;
};


#endif