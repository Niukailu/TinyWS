#include "lst_timer.h"

//移除socket连接
void cb_func(client_data *user_data) {
    assert(user_data); 
    //删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    //关闭文件描述符
    close(user_data->sockfd);
    //减少连接数
    --HttpConn::m_user_count;
}

Sort_TimerLst::Sort_TimerLst() {
    head = nullptr;
    tail = nullptr;
}

Sort_TimerLst::~Sort_TimerLst() {
    UtilTimer *cur = head;
    while (cur != nullptr)
    {
        head = head->next;
        delete cur;
        cur = head;
    }
}

void Sort_TimerLst::add_timer(UtilTimer *timer) {
    if(timer == nullptr) return;
    if(head == nullptr) {
        head = tail = timer;
        return;
    }
    if(timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

//当定时任务发生变化, 调整对应定时器在链表中的位置
void Sort_TimerLst::adjust_timer(UtilTimer *timer) {
    if(timer == nullptr) return;
    //不需要调整
    if(timer->next == nullptr || timer->expire < timer->next->expire) return;
    //复用插入函数
    if(timer == head) {
        head = head->next;
        head->prev == nullptr;
        timer->next = nullptr;
        add_timer(timer);
    }
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

//从链表中删除某个定时器
void Sort_TimerLst::del_timer(UtilTimer *timer) {
    if(timer == nullptr) return;
    if(timer == head && head == tail) {
        delete timer;
        head = nullptr, tail = nullptr;
        return;
    }
    if(timer == head) {
        head = head->next;
        delete timer;
        head->prev = nullptr;
        return;
    }
    if(timer == tail) {
        tail = timer->prev;
        delete timer;
        tail->next = nullptr;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

//定时任务处理函数
//将此时定时器链表上的超时的socket连接移除（包括定时器等等）
void Sort_TimerLst::tick() {
    if(head == nullptr) return;

    time_t cur = time(NULL); //秒数                 
    UtilTimer *tmp = head;
    while (tmp != nullptr)
    {
        //定时器的超时时间大于当前时间（还没到超时时间）
        if(cur < tmp->expire) break;
        //已经到了超时时间的，需要被移除连接
        tmp->cb_func(tmp->user_data);
        head = head->next;
        if(head != nullptr) head->prev = nullptr;
        delete tmp;
        tmp = head;
    }
}

//按UtilTimer的expire值 从小到大 插入到队列
void Sort_TimerLst::add_timer(UtilTimer *timer, UtilTimer *lst_head) {
    UtilTimer *prev = lst_head;
    while (prev->next != nullptr)
    {
        if(timer->expire < prev->next->expire) {
            prev->next->prev = timer;
            timer->next = prev->next;
            prev->next = timer;
            timer->prev = prev;
            return;
        }
        else {
            prev = prev->next;
        }
    }
    prev->next = timer;
    timer->prev = prev;
    timer->next = nullptr;
    tail = timer;
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

int Utils::set_nonblocking(int fd) {
    //fcntl: 对一个已经打开的文件描述符进行各种操作，包括文件状态标志的获取和设置等
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;  //不需要吧？
}

void Utils::add_fd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;   //ET模式
    else event.events = EPOLLIN | EPOLLRDHUP;
    //开启EPOLLONESHOT，确保每个文件描述符只被一个线程处理
    if(one_shot) event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入（可以从 u_pipefd[0] 读取），传输字符类型，而非整型，为什么呢？
    send(u_pipefd[1], (char*)&msg, 1, 0); 
    errno = save_errno;
}

//设置信号处理函数
void Utils::add_signal(int sig, void(*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    //sa_handler：指定信号处理程序的函数指针
    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    //SA_RESTART，表示在信号处理程序被中断后，系统会自动重启被中断的系统调用
    if(restart) sa.sa_flags |= SA_RESTART;
    //在信号处理函数执行期间阻塞所有信号。
    //这样可以确保在信号处理函数执行期间不会被其他信号中断，从而提高信号处理函数的可靠性和安全性。
    sigfillset(&sa.sa_mask);
    //设置信号的处理方式（参数一：要设置的信号类型；参数二：指定新的信号处理函数和信号处理选项）
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    //每隔TIMESLOT时间触发SIGALRM信号
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info) {
    //向socket连接描述符connfd发送数据（在内核缓冲区）
    send(connfd, info, strlen(info), 0);
    //关闭这个socket套接字
    close(connfd);
}

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;