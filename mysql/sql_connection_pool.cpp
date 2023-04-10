#include "sql_connection_pool.h"

SqlConnectionPool::SqlConnectionPool() {
    m_curConn = 0;
    m_freeConn = 0;
}

SqlConnectionPool* SqlConnectionPool::get_instance() {
    static SqlConnectionPool connPool;
    return &connPool;
}

void SqlConnectionPool::init(std::string url, std::string user, std::string passwd, std::string database_name, int port, int max_conn, int close_log) {
    m_url = url;
    m_user = user;
    m_port = port;
    m_passwd = passwd;
    m_database_name = database_name;
    m_close_log = close_log;

    for (int i = 0; i < max_conn; i++)
    {
        MYSQL* con = nullptr;
        con = mysql_init(con);

        if(con == NULL) {
            LOG_ERROR("MySQL ERROR!");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), user.c_str(), passwd.c_str(), database_name.c_str(), port, NULL, 0);

        if(con == NULL) {
            LOG_ERROR("MySQL ERROR!");
            exit(1);
        }
        connList.push_back(con);
        ++m_freeConn;
    }
    
    reserve = Sem(m_freeConn);
    m_maxConn = m_freeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* SqlConnectionPool::get_connection() {
    MYSQL* con = nullptr;
    
    if(connList.size() == 0) return nullptr;
    //信号量<=0阻塞，等待: 信号量的值增加到大于零为止，再减一
    reserve.wait(); //不应该判断一下返回值？
    //互斥来保证线程同步
    lock.lock();  //不应该判断一下返回值？

    con = connList.front();
    connList.pop_front();

    --m_freeConn;
    ++m_curConn;

    lock.unlock();
    return con;
}

//释放当前使用的连接
bool SqlConnectionPool::release_connection(MYSQL* con) {
    if(con == nullptr) return false;

    lock.lock();

    connList.push_back(con);
    ++m_freeConn;
    --m_curConn;

    lock.unlock();
    reserve.post();  //使信号量加一

    return true;
}

int SqlConnectionPool::get_free_conn() {
    return this->m_freeConn;
}

void SqlConnectionPool::destroy_pool() {
    lock.lock();

    if(connList.size() > 0) {
        auto it = connList.begin();
        for(; it != connList.end(); ++it) {
            mysql_close(*it);
        }
        m_curConn = 0;
        m_freeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

SqlConnectionPool::~SqlConnectionPool() {
    destroy_pool();
}


ConnectionRAII::ConnectionRAII(MYSQL **con, SqlConnectionPool *connPool) {
    *con = connPool->get_connection();
    conRAII = *con;
    poolRAII = connPool;
}

ConnectionRAII::~ConnectionRAII() {
    poolRAII->release_connection(conRAII);
}
