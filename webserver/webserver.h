#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

#include "../includes.h"
#include "../timer/lst_timer.h"
#include "../http/http_conn.h"
#include "../mysql/sql_connection_pool.h"
#include "../threadpool/threadpool.hpp"
#include "../log/log.h"

const int MAX_FD = 65536;         //最大文件描述符个数
const int MAX_EVENT_NUM = 10000;  //最大事件数
const int TIMESLOT = 5;           //超时单位

class WebServer
{   
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passwd, string database_name, 
               int log_write , int opt_linger, int trigmode, int sql_num,
               int thread_num, int close_log, int actor_model);
    
    void log_write();
    void sql_pool();
    void thread_pool();
    void trig_mode();
    void eventListen();
    void eventLoop();

    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(UtilTimer *timer);
    void deal_timer(UtilTimer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:

    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    int m_pipefd[2];
    int m_epollfd;

    HttpConn *users;

    //数据库相关
    ConnectionPool *m_connPool;
    string m_user;           //登录数据库用户名
    string m_passwd;         //登录数据库密码
    string m_database_name;  //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<HttpConn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUM];
    
    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    //定时器相关
    client_data *users_timer;
    Utils utils;
};



#endif