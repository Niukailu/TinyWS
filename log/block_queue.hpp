/*
循环数组实现的阻塞队列 (也可以用C++的queue)，m_back = (m_back + 1) % m_max_size; 
线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
*/

#ifndef __BLOCK_QUEUE_HPP__
#define __BLOCK_QUEUE_HPP__

#include <sys/time.h>
#include "../lock/locker.h"

template<class T>
class block_queue
{
public:
    block_queue(int max_size = 1000);
    ~block_queue();
    //清空队列
    void clear();
    //判断队列是否满了
    bool full();
    //判断队列是否为空
    bool empty();
    //返回队首元素
    bool front(T& value);
    //返回队尾元素
    bool back(T& value);

    int size();
    int max_size();

    bool push(const T& item);
    bool pop(T& item);
    bool pop(T& item, int ms_timeout)   

private:
    Locker m_mutex;  //互斥锁
    Cond m_cond;     //条件变量

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

template<class T>
block_queue<T>::block_queue(int max_size = 1000) {
    if(max_size <= 0) {
        exit(-1);
    }
    m_max_size = max_size;
    m_array = new T[max_size];
    m_size = 0;
    m_front = -1;
    m_back = -1;
}

template<class T>
void block_queue<T>::clear() {
    m_mutex.lock();
    m_size = 0;
    m_front = -1; //指向有效索引的前一个位置
    m_back = -1;
    m_mutex.unlock();
}

template<class T>
bool block_queue<T>::full() {
    m_mutex.lock();
    if(m_size >= m_max_size) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<class T>
bool block_queue<T>::empty() {
    m_mutex.lock();
    if(m_size == 0) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<class T>
bool block_queue<T>::front(T &value) {
    m_mutex.lock();
    if(m_size == 0) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
}

template<class T>
bool block_queue<T>::back(T &value) {
    m_mutex.lock();
    if(m_size == 0) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_back];
    m_mutex.unlock();
    return true;
}

template<class T>
int block_queue<T>::size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_size;
    m_mutex.unlock();
    return tmp;
}

template<class T>
int block_queue<T>::max_size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_max_size;
    m_mutex.unlock();
    return tmp;
}

//往队列添加元素，需要将所有使用队列的线程先唤醒; 当有元素push进队列,相当于生产者生产了一个元素
//若当前没有线程等待条件变量,则唤醒无意义
template<class T>
bool block_queue<T>::push(const T &item) {
    m_mutex.lock(); //在对条件变量进行操作前需先获得互斥锁
    if(m_size >= m_max_size) {
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    ++m_size;
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}

template<class T>
bool block_queue<T>::pop(T &item) {
    m_mutex.lock();
    while (m_size <= 0)
    {
        if(!m_cond.wait(m_mutex.get())) {
            m_mutex.unlock();
            return false;
        }
    }
    
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    --m_size;
    m_mutex.unlock();
    return true;
}

template<class T>
bool block_queue<T>::pop(T &item, int ms_timeout) { //超时处理，ms_timeout为毫秒
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL); //这句不放到lock下面？
    
    m_mutex.lock();
    if(m_size <= 0) {
        t.tv_sec = now.tv_sec + ms_timeout / 1000;  //秒
        t.tv_nsec = (ms_timeout % 1000) * 1000;  //纳秒
        if(!m_cond.time_wait(m_mutex.get(), t)) {
            m_mutex.unlock();
            return false;
        }
    }

    if(m_size <= 0) { //为什么是这个判断条件？
        m_mutex.unlock();
        return false;
    }
    
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    --m_size;
    m_mutex.unlock();
    return true;
}

template<class T>
block_queue<T>::~block_queue() {
    m_mutex.lock();
    if (m_array != NULL) delete[] m_array;
    m_mutex.unlock();
}


#endif