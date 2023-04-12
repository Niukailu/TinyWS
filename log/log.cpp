#include "log.h"

Log::Log() {
    m_count = 0;
    m_isAsync = false;
}

Log::~Log() {
    if(m_fp == NULL) {
        fclose(m_fp);
    }
}

Log* Log::get_instance() {
    static Log instance;
    return &instance;
}

void* Log::flush_log_thread(void* args) {
    Log::get_instance()->async_write_log();
}

//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char* file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0) {
    if(max_queue_size >= 1) {
        m_isAsync = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数, 这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //strrchr从字符串的末尾开始向前查找，直到找到第一个与指定字符匹配的位置为止，并返回该位置的指针
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    //相当于自定义日志文件名
    //若输入的文件名没有/，则直接将时间+文件名作为日志名
    if(p == NULL) {
        //这个时间为什么这样写？？
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    //追加模式打开日志文件，不存在会自动创建
    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL) {
        return false;
    }
    return true;
}

//写一行到日志文件中
void Log::write_log(int level, const char* format, ...) {
    //时间戳
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    //线程同步写？
    m_mutex.lock();
    //更新现有行数
    m_count++;
    //如果日志不是今天创建的 或 写入的日志行数是所限制最大行的倍数，就创建一个新log
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        //刷新缓冲区, 强制将缓冲区中的数据写入文件
        fflush(m_fp);
        fclose(m_fp);

        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        //如果是时间不是今天, 则创建今天的日志，更新m_today和m_count
        if(m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else {
            //超过了最大行， 就在新日志文件名加个后缀
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    
    m_mutex.unlock();

    //可变参数列表
    va_list valst;
    va_start(valst, format);

    std::string log_str;
    m_mutex.lock();

    //写入内容格式：时间 + 内容
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = snprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if(m_isAsync && !m_log_queue->full()) { //异步写就push到阻塞队列
        m_log_queue->push(log_str);
    }
    else {  //同步则加锁向文件中写
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void* Log::async_write_log() {
    std::string single_log;
    //从阻塞队列中取出一个日志string，写入文件
    while (m_log_queue->pop(single_log))
    {
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
}

//这个函数也没用到啊。。。
void Log::flush(void) {
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}

