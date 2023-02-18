#include "lst_timer.h"
#include "../http/http_conn.h"

// 定时器链表构造函数
sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

// 定时器链表析构函数
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head)  // 如果头结点为空，就直接加入
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)  // 按序插入到正确的链表位置，链表的时间是升序
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);   // 递归调用，遍历链表插入到合适的位置
}

// 这里是调整链表节点，应该是链表中某个节点的时间变了，因此也需要调整该节点在链表中的位置，也就是首先将该节点从链表删除，再调用add_timer函数将该节点插入到合适的位置
// 该函数会首先检测这个改变时间的节点是否仍在正确的位置，也就是检查该节点的时间和它后继节点的时间关系，如果依然符合大小关系，就不用调整节点的位置
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

// 删除链表中的指定结点，分为链表中只有一个节点，删除的是头结点，删除的是尾结点，删除的是中间节点几种情况讨论
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 感觉这个函数应该是删除所有超时的链接
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while (tmp)
    {
        if (cur < tmp->expire)
        {
            break;
        }
        tmp->cb_func(tmp->user_data);  // 关闭链表节点tmp的对应文件描述符，
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}


// 在计时器链表的正确位置插入新加的时间节点，计时器链表时间从小到大
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp)
    {
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);  // fcntl是Linux系统调用，用于设置打开的文件描述符的属性
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
// 这个函数应该是设置epoll监听信号
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数，在下面的addsig函数中会调用这个函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno（也就是保存这个操作之前的errno）
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);  // 通过管道传递信号 send的作用是将信息传输到另一个socket在，这里另一个socket是u_pipefd，传递的信息是msg，也就是信号sig
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;   // The sigaction() system call is used to change the action taken by a process on receipt of a specific signal.  
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;  // 信号处理函数
    if (restart)
        sa.sa_flags |= SA_RESTART;  // 使某些系统调用跨信号可以重新启动
    sigfillset(&sa.sa_mask);   //这个函数的作用是初始化并填充信号集，也就是把所有信号加入到信号集中
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
// 定时处理任务：删除所有超时的链接，并重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();  // 这个对象是上面定义的排序链表的定时器，这一句应该就是启动定时器
    alarm(m_TIMESLOT);  //  Linux系统调用：安排SIGALRM信号被传递到以秒为单位的调用进程，在任何情况下，之前设置的alarm()都将被取消，或者说是被覆盖
}

// 发送错误消息
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);  // 这个send应该也是Linux系统调用，作用是将信号传输到另外一个socket，但是不明白为什么要将信号传输到另一个socket
    close(connfd);  // 关闭传入参数的文件描述符
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);  // 删除文件描述符user_data->sockfd
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;   
}
