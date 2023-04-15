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
        con = mysql_init(con); //初始化一个MySQL连接句柄（MYSQL结构体）,分配内存

        if(con == NULL) {
            LOG_ERROR("MySQL ERROR!");
            exit(1);
        }
        //连接到MySQL服务器
        con = mysql_real_connect(con, url.c_str(), user.c_str(), passwd.c_str(), database_name.c_str(), port, NULL, 0);

        if(con == NULL) {
            LOG_ERROR("MySQL ERROR!");
            exit(1);
        }
        connList.push_back(con); //放到数据库连接池 (链表结构)
        ++m_freeConn;
    }
    
    reserve = Sem(m_freeConn);
    m_maxConn = m_freeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接（取链表头部），更新使用和空闲连接数
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

    lock.lock(); //加锁

    connList.push_back(con);
    ++m_freeConn;
    --m_curConn;

    lock.unlock();
    reserve.post();  //使信号量加一

    return true;
}

//获取当前空闲数据库连接的数量
int SqlConnectionPool::get_free_conn() {
    return this->m_freeConn;
}

//销毁整个数据库连接池（关闭所有数据库连接）
void SqlConnectionPool::destroy_pool() {
    lock.lock();

    if(connList.size() > 0) {
        auto it = connList.begin();
        for(; it != connList.end(); ++it) { //迭代，关闭
            mysql_close(*it);
        }
        m_curConn = 0;
        m_freeConn = 0;
        //清空list
        connList.clear();
    }

    lock.unlock();
}

//析构函数释放连接池
SqlConnectionPool::~SqlConnectionPool() {
    destroy_pool();
}

//RAII: 资源获取即初始化。在构造函数中申请分配资源，在析构函数中释放资源
//因为C++的语言机制保证了，当一个对象创建的时候，自动调用构造函数，当对象超出作用域的时候会自动调用析构函数
//所以，RAII的核心思想是将资源或者状态与对象的生命周期绑定
//数据库连接的获取与释放：不直接调用获取和释放连接的接口，而是通过RAII机制封装，避免手动释放
ConnectionRAII::ConnectionRAII(MYSQL **con, SqlConnectionPool *connPool) {
    *con = connPool->get_connection();
    conRAII = *con;
    poolRAII = connPool;
}

ConnectionRAII::~ConnectionRAII() {
    poolRAII->release_connection(conRAII);
}
