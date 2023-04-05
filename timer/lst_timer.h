#ifndef __LST_TIMER_H__
#define __LST_TIMER_H__


class UtilTimer;

struct client_data
{
    sockaddr a;              
    sockaddr_in address;
    int sockfd;
    UtilTimer *timer;
};


class Utils
{
public:
    Utils();
    ~Utils();


    
};




#endif