#ifndef __LOG_H__
#define __LOG_H__

class Log
{
public:
    //C++11以后, 使用局部变量懒汉模式不用加锁
    static Log* get_instance();

    static void* flush_log_thread(void* args);

    ///可选参数：日志文件、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, 
                int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();

    void* async_write_log();

private:
    char dir_name[128];  //路径名
    char log_name[128];  //log文件名
    int m_split_lines;   //日志最大行数
    int m_log_buf_size;   //日志缓冲区大小
    long long m_count;    //日志行数记录
    int m_today;          //因为按天分类, 记录当前时间是哪一天

    FILE *m_fp;        //打开log的文件指针
    char *m_buf;



};




#endif
