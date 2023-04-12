#include "webserver.h"

WebServer::WebServer() {
    //初始化 http_conn 类对象
    users = new HttpConn[MAX_FD];

    //获取root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    //释放文件描述符
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);

    delete[] users;
    delete[] users_timer;
    delete m_pool;   // ?
}

void WebServer::init(int port, std::string user, std::string passwd, std::string database_name, int log_write , 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {

    m_port = port;
    m_user = user;
    m_passwd = passwd;
    m_database_name = database_name;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actor_model = actor_model;
}

void WebServer::log_write() {
    if(m_close_log == 0) { //若不关闭日志
        //初始化日志
        if(m_log_write == 1) {  //若写入方式是异步
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        }
        else {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}

void WebServer::sql_pool() {
    //初始化数据库连接池
    m_connPool = SqlConnectionPool::get_instance();
    m_connPool->init("localhost", m_user, m_passwd, m_database_name, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->init_mysqlResult(m_connPool);
}

void WebServer::thread_pool() {
    m_pool = new threadpool<HttpConn>(m_actor_model, m_connPool, m_thread_num);
}

void WebServer::trig_mode() {
    //LT + LT
    if(m_TRIGMode == 0) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (m_TRIGMode == 1)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (m_TRIGMode == 2)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (m_TRIGMode == 3)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::eventListen() {
    //创建监听socket，参数：Ipv4, TCP, 默认协议
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    
    if(m_OPT_LINGER == 0) { //如果是短连接
        struct linger tmp = {0, 0};
        //设置 Socket 的选项: 传入tmp结构体作为 SO_LINGER 选项的值
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(m_OPT_LINGER == 1) {
        struct linger tmp = {1, 1};  //其中，延迟关闭的时间单位为秒
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    struct sockaddr_in address;
    bzero(&address, sizeof(address));  //清零 (为啥不用memset?)   
    address.sin_family = AF_INET;          
    address.sin_port = htons(m_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY); //设置为本地任意可用 IP 地址

    int flag = 1;
    //用于控制在关闭一个 socket 连接后，是否允许立即重用该 socket 的本地地址和端口号
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    int ret = 0;
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);   //如果条件不满足，则会产生一个错误，并终止程序的执行
    //创建监听队列以存放待处理的客户连接，在这些客户连接被accept()之前
    ret = listen(m_listenfd, 5); //为啥是5？
    assert(ret >= 0); 

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    m_epollfd = epoll_create(5);    //参数表示 epoll 实例中所能容纳的最大文件描述符数
    assert(m_epollfd != -1);
    //注册监听socket事件, 当listen到新的客户连接时，listenfd变为就绪事件
    utils.add_fd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    HttpConn::m_epollfd = m_epollfd;
    
    //创建一对相互连接的套接字(本地)
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    utils.set_nonblocking(m_pipefd[1]);
    utils.add_fd(m_epollfd, m_pipefd[0], false, 0);

    //当进程向一个已经关闭了写端的管道中写入数据时，内核会发送 SIGPIPE 信号给该进程，
    //以提示该进程已经试图向一个不可写的管道中写入数据；SIG_IGN，表示忽略对该信号的处理
    utils.add_signal(SIGPIPE, SIG_IGN);
    //SIGALRM表示定时器到期
    utils.add_signal(SIGALRM, utils.sig_handler, false);
    //SIGTERM表示终止进程
    utils.add_signal(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        //将当前所有就绪的epoll_event复制到events数组中 
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUM, -1);
        if(number < 0 && errno != EINTR) {  //由于被一个信号（如SIGALRM）中断而失败, 这种情况应重新调用该系统调用
            LOG_ERROR("%s", "epoll wait failure");
            break;
        }
        //遍历这一数组以处理这些已经就绪的事件
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //当listen到新的用户连接，listenfd上则产生就绪事件
            if(sockfd == m_listenfd) { 
                bool flag = deal_clinet_data();
                if(flag == false) continue;
            }
            //EPOLLRDHUP: TCP连接断开; EPOLLHUP: 描述符被挂起; EPOLLERR: 描述符发生错误
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //服务器端关闭连接，移除对应的定时器
                UtilTimer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if((events[i].events & EPOLLIN) && sockfd == m_pipefd[0]) {
                bool flag = deal_with_signal(timeout, stop_server);
                if(!flag) LOG_ERROR("%s", "deal with client data failure");
            }
            //读事件
            else if(events[i].events & EPOLLIN) {

            }
            //写事件
            else if(events[i].events & EPOLLOUT) {

            }
        }

        if(timeout) {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}

//将connfd注册到内核事件表中
void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passwd, m_database_name);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    UtilTimer *timer = new UtilTimer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

void WebServer::deal_timer(UtilTimer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if(timer)   //还要判断？？
    {
        utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(UtilTimer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s", "adjust timer once");
}

bool WebServer::deal_clinet_data() {
    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);

    if(m_LISTENTrigmode == 0) { //触发模式: LT
        //accept()返回一个新的socket文件描述符用于send()和recv() 
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlen);
        //-1 连接失败
        if(connfd < 0) {
            LOG_ERROR("%s: errno is: %d", "accept error", errno);
            return false;
        }
        if(HttpConn::m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else {  //触发模式: ET
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlen);
            if(connfd < 0) {
                LOG_ERROR("%s: errno is: %d", "accept error", errno);
                break;
            }
            if(HttpConn::m_user_count >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false; 
    }
    return true;
}

bool WebServer::deal_with_signal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);

    if(ret == -1) return false;
    else if(ret == 0) return false;
    else {
        for (int i = 0; i < ret; i++)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::deal_with_read(int sockfd) {
    UtilTimer *timer = users_timer[sockfd].timer;
    //reactor
    if(m_actor_model == 1) {
        //时间变了吗？？
        if(timer != nullptr) adjust_timer(timer);
        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);
        while (true)    //???
        {
            if(users[sockfd].improv == 1) {
                if(users[sockfd].timer_flag = 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //proactor
    else {
        if(users[sockfd].read_once()) {
            //inet_ntoa将一个 IPv4 地址从网络字节序的二进制形式转换为点分十进制的字符串形式
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            //若监测到读事件，将该事件放入请求队列
            m_pool->append(users + sockfd);

            if(timer != nullptr) adjust_timer(timer);
        }
        else deal_timer(timer, sockfd);
    }
}

void WebServer::deal_with_write(int sockfd) {
    UtilTimer *timer = users_timer[sockfd].timer;
    //reactor
    if(m_actor_model == 1) {
        if(timer != nullptr) adjust_timer(timer);
        //若监测到写事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 1);
        while (true)    //???
        {
            if(users[sockfd].improv == 1) {
                if(users[sockfd].timer_flag = 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //proactor
    else {
        if(users[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if(timer != nullptr) adjust_timer(timer);
        }
        else deal_timer(timer, sockfd);
    }
}