#ifndef __LST_TIMER_H__
#define __LST_TIMER_H__

#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <assert.h>
#include <fcntl.h>

#include "../http/http_conn.h"

class UtilTimer;

struct client_data
{
    sockaddr_in address;   //客户端socket地址
    int sockfd;
    UtilTimer *timer;
};

//定时器回调函数
void cb_func(client_data *user_data);

//定时器类
class UtilTimer
{ 
public:
    UtilTimer(): prev(nullptr), next(nullptr) {};
    ~UtilTimer() {};

public:
    time_t expire;             //超时时间
    void (* cb_func)(client_data*); //回调函数 (函数类型的指针)
    client_data *user_data;
    UtilTimer *prev, *next;
};

class Sort_TimerLst
{
public:
    Sort_TimerLst();
    ~Sort_TimerLst();

    void add_timer(UtilTimer *timer);
    void adjust_timer(UtilTimer *timer);
    void del_timer(UtilTimer *timer);
    void tick();

private:
    UtilTimer *head, *tail;

    void add_timer(UtilTimer *timer, UtilTimer *lst_head);
};

class Utils
{
public:
    Utils() {};
    ~Utils() {};

    void init(int timeslot);

    //将文件描述符设置为非阻塞
    int set_nonblocking(int fd);

    //在内核事件表中注册读事件，ET模式，选择开启EPOLLONESHOT
    void add_fd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void add_signal(int sig, void(*handler)(int), bool restart = true); 

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    Sort_TimerLst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};


#endif