#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

#include <assert.h>
#include <arpa/inet.h>
#include "../timer/lst_timer.h"
#include "../http/http_conn.h"
#include "../mysql/sql_connection_pool.h"
#include "../threadpool/threadpool.hpp"
#include "../log/log.h"

const int MAX_FD = 65536;         //最大文件描述符个数
const int MAX_EVENT_NUM = 10000;  //最大事件数
const int TIMESLOT = 5;           //超时单位 秒

class WebServer
{   
public:
    WebServer();
    ~WebServer();

    //初始化
    void init(int port, std::string user, std::string passwd, std::string database_name, 
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
    bool deal_clinet_data();
    bool deal_with_signal(bool& timeout, bool& stop_server);
    void deal_with_read(int sockfd);
    void deal_with_write(int sockfd);

public:

    int m_port;     //开放的端口号
    char *m_root;   //被请求文件存放的路径，本项目中是：./root/
    int m_log_write;    //日志写入方式
    int m_close_log;    //是否关闭日志
    int m_actor_model; //并发模型选择，默认是proactor

    int m_pipefd[2];    //
    int m_epollfd;      //epoll文件描述符

    HttpConn *users; //Http连接的客户

    //数据库相关
    SqlConnectionPool *m_connPool; //数据库连接池对象指针
    std::string m_user;           //登录数据库用户名
    std::string m_passwd;         //登录数据库密码
    std::string m_database_name;  //使用数据库名
    int m_sql_num;              //数据库连接池数量

    //线程池（处理Http连接）相关
    threadpool<HttpConn> *m_pool;   //线程池对象指针
    int m_thread_num;               //线程池内的线程数量

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUM]; //就绪事件数组
    
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