#ifndef __SQL_CONNECTION_POOL__
#define __SQL_CONNECTION_POOL__

#include <mysql/mysql.h>

class ConnectionPool
{
public:
    MYSQL* get_connection();               //获取数据库连接
    bool release_connection(MYSQL* conn);  //释放连接

    int get_free_conn();              //获取连接
    void destroy_pool();	            //销毁所有连接

    //懒汉单例模式
    static ConnectionPool* get_instance();

    void init();

private:
    ConnectionPool();
    ~ConnectionPool();




};






#endif