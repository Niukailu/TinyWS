#include "config/config.h"
#include "webserver/webserver.h"
#include "includes.h"


int main(int argc, char *argv[]) {
    
    //自定义数据库信息：用户名,密码,数据库名
    string user = "root";
    string passwd = "123456";
    string database_name = "mywsdb";

    //命令行参数解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, database_name, config.LOGWrite, 
                config.OPT_LINGER,config.TRIGMode, config.sql_num, config.thread_num, 
                config.close_log, config.actor_model);

    //日志
    server.log_write();

    

    



    return 0;
}