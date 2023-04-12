#ifndef __SQL_CONNECTION_POOL__
#define __SQL_CONNECTION_POOL__

#include <mysql/mysql.h>
#include <string>
#include <list>
#include "../lock/locker.h"
#include "../log/log.h"

class SqlConnectionPool
{
public:
    //懒汉单例模式获取类的实例
    static SqlConnectionPool* get_instance();
    //初始化数据库连接池
    void init(std::string url, std::string user, std::string passwd, std::string database_name, int port, int max_conn, int close_log);

    MYSQL* get_connection();               //获取一个数据库连接
    bool release_connection(MYSQL* conn);  //释放一个连接

    int get_free_conn();              //获取当前空闲的连接数
    void destroy_pool();	            //销毁数据库连接池   

public:
    std::string m_url;            //主机地址
    std::string m_port;           //数据库端口号
    std::string m_user;            //登陆数据库用户名
    std::string m_passwd;          //登陆数据库密码
    std::string m_database_name;    //使用数据库名
    int m_close_log;           //日志开关

private:
    SqlConnectionPool();
    ~SqlConnectionPool();

    int m_maxConn;        //最大连接数
    int m_curConn;         //当前已使用的连接数
    int m_freeConn;        //当前空闲的连接数
    
    std::list<MYSQL*> connList;  //数据库连接池
    Sem reserve;           //信号量
    Locker lock;           //互斥锁
};

class ConnectionRAII
{
public:
    //数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改
    ConnectionRAII(MYSQL **con, SqlConnectionPool *connPool);
    ~ConnectionRAII();

private:
    MYSQL *conRAII;
    SqlConnectionPool *poolRAII;
};



#endif