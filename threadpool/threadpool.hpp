#ifndef __THREADPOOL_HPP__
#define __THREADPOOL_HPP__

#include <list>
#include <pthread.h>
#include "../mysql/sql_connection_pool.h"

template<class T>
class threadpool
{
public:
    //thread_number: 线程池中线程的数量; max_requests: 请求队列中最多允许的、等待处理的请求的数量
    threadpool(int actor_model, SqlConnectionPool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append(T *request);

private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue;  //请求队列
    Locker m_queue_locker;      //保护请求队列的互斥锁
    Sem m_queue_stat;           //是否有任务需要处理（信号量）
    SqlConnectionPool *m_connPool;  //数据库连接池
    int m_actor_model;          //模型切换
};

template<class T>
threadpool<T>::threadpool(int actor_model, SqlConnectionPool *connPool, int thread_number, int max_request)
{
    m_actor_model = actor_model;
    m_thread_number = thread_number;
    m_max_requests = max_request;
    m_threads = nullptr;
    m_connPool = connPool;

    if(thread_number <= 0 || max_request <= 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) throw std::exception();

    for (int i = 0; i < m_thread_number; ++i)
    {
        //创建新线程, 调用worker函数
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {//this指针？？
            delete[] m_threads;
            throw std::exception();
        }
        //分离线程，它的资源会在它退出时被自动回收
        if(pthread_detach(m_threads[i]) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<class T>
threadpool<T>::~threadpool()
{
    if(m_threads != nullptr) delete[] m_threads;
}

template<class T>
bool threadpool<T>::append(T *request, int state) {
    m_queue_locker.lock();
    if(m_workqueue.size() >= m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queue_locker.unlock();
    //信号量提醒有任务要处理
    m_queue_stat.post();
    return true;
}

template<class T>
bool threadpool<T>::append(T *request) { //改写成重载了
    m_queue_locker.lock();
    if(m_workqueue.size() >= m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queue_locker.unlock();
    m_queue_stat.post();
    return true;
}

template<class T>
void* threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool*)arg;  //为啥不用强制类型转换？
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run() {
    while (true)  //什么时候退出循环？
    {
        //信号量等待
        m_queue_stat.wait();
        //被唤醒后先加互斥锁
        m_queue_locker.lock();
        if(m_workqueue.empty()) {
            m_queue_locker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queue_locker.unlock();
        if(!request) continue;

        if(m_actor_model == 1) {
            if(request->m_state == 0) {
                if(request->read_once()) {
                    request->improv = 1; //??
                    //从连接池中取出一个数据库连接?
                    ConnectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else {
                if(request->write()) {
                    request->improv = 1;
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else {
            ConnectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif