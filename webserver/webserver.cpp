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

void WebServer::init(int port, string user, string passwd, string database_name, int log_write , 
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
    m_actormodel = actor_model;
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
    
}
