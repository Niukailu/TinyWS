#ifndef __BLOCK_QUEUE_HPP__
#define __BLOCK_QUEUE_HPP__

#include "../lock/locker.h"

template<class T>
class block_queue
{
public:
    block_queue(int max_size = 1000);

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

    ~block_queue();

private:
    Locker m_mutex;
    Cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};




#endif