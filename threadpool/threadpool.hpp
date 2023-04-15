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
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    int m_max_requests;         //请求队列中允许的最大请求数
    std::list<T*> m_workqueue;  //请求队列（存待处理的 http 请求）

    Locker m_queue_locker;      //保护请求队列的互斥锁
    Sem m_queue_stat;           //信号量

    SqlConnectionPool *m_connPool;  //数据库连接池
    int m_actor_model;          //模型切换
};

//构造函数
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
    //申请线程池数组
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) throw std::exception();

    for (int i = 0; i < m_thread_number; ++i)
    {
        //创建新线程, 回调函数为worker
        //当新线程被创建后，操作系统会将其加入到就绪队列中，等待调度器分配 CPU 时间片并开始执行
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {//this指针作为回调函数的参数
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

//向请求队列添加元素
template<class T>
bool threadpool<T>::append(T *request, int state) {
    m_queue_locker.lock();  //互斥锁保证线程安全
    if(m_workqueue.size() >= m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }
    request->m_state = state;   //state = 0 代表读 1 代表写
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

//线程的回调函数
//若线程函数为类成员函数，则this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)不能匹配，不能通过编译？？？
//而静态成员函数就没有这个问题，里面没有this指针
template<class T>
void* threadpool<T>::worker(void *arg) {//我实验明明类成员函数也不报错啊。。。
    threadpool *pool = (threadpool*)arg;  //可以用强制类型转换吧？
    pool->run();
    return pool;
}

//去请求队列取出元素处理http请求
template<class T>
void threadpool<T>::run() { //为什么不直接用这个函数呢？？
    while (true)  //线程被detach后是独立的，会一直执行run处理http请求
    {
        //信号量等待 (等待有元素被append到请求队列)
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

        //如果是 Reactor 模式（非阻塞同步）
        if(m_actor_model == 1) {
            //读请求
            if(request->m_state == 0) { 
                //如果正确读取了socket数据到用户缓冲区了
                if(request->read_once()) {
                    request->improv = 1; //这个标志有啥用？？？
                    //从连接池中取出一个数据库连接
                    ConnectionRAII mysqlcon(&request->mysql, m_connPool);
                    //处理请求
                    request->process();
                }
                else {  //read_once返回false
                    request->improv = 1;
                    //读取失败，后续会根据这个标志执行 deal_timer(timer, sockfd)
                    request->timer_flag = 1;
                }
            }
            //写请求
            else {
                if(request->write()) {
                    request->improv = 1;
                }
                else {  //write返回false
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //如果是 Proactor 模式（异步）
        else {
            ConnectionRAII mysqlcon(&request->mysql, m_connPool);
            //主线程已经处理过读写事件了，这里工作线程只做逻辑处理
            request->process();
        }
    }
}

#endif