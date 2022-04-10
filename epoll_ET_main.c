#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>
#define MAXLNE  4096

#define EPOLL_SIZE 1024
int main() {

    int listenfd, connfd, n;

    struct sockaddr_in servaddr;
    char buff[MAXLNE];
 
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("create socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }
 
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(10000);
 
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        printf("bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }
 
    if (listen(listenfd, 10) == -1) {
        printf("listen socket error: %s(errno: %d)\n", strerror(errno), errno);
        return 0;
    }

    int epfd = epoll_create(1);

    struct epoll_event events[EPOLL_SIZE] = {0};
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listenfd;
    
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    while(1) {

        int nready = epoll_wait(epfd, events, EPOLL_SIZE, 5);//nready是返回的监控文件描述符中有响应的描述符个数
        if(nready == -1) {
            continue;
        }

        for(int i = 0; i < nready; i++) {
            int clientfd = events[i].data.fd;
            if(clientfd == listenfd) {
                struct sockaddr_in client;
                socklen_t len = sizeof(client);
                if((connfd = accept(listenfd, (struct sockaddr *)&client, &len)) == -1) {
                    printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
                    return 0;
                }
                printf("accept Success fd=  %d\n", connfd);

                int flag = 0;
                if ((flag = fcntl(clientfd, F_SETFL, O_NONBLOCK)) < 0) {//设置与客户端通信的socket为非阻塞
                    printf(" fcntl nonblocking failed\n");
                    break;
                }


                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = connfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
            }
            else if(events[i].events & EPOLLIN) {
                n = recv(clientfd, buff, MAXLNE, 0);//因为使用的ET模式，这里其实最好使用while循环将fd的数据全部读到buff中（防止数据多一次读不完的情况）
                if (n > 0) {
                    buff[n] = '\0';
                    printf("recv msg from client: %s\n", buff);
                    ev.events = EPOLLOUT;//注册EPOLLOUT事件，让服务器发送数据
                    ev.data.fd = clientfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);              
                } 
                else if (n == 0) {//当客户端调用close时，会返回n=0
                    ev.events = EPOLLIN;
                    ev.data.fd = clientfd;
                    epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, &ev);//删除EPOLL事件
                    close(clientfd);
                }

            }else if(events[i].events & EPOLLOUT) {
                send(clientfd, buff, n, 0);//将发送来的数据依次返回发送回去
                ev.events = EPOLLIN | EPOLLET;//重新注册EPOLL事件
                ev.data.fd = clientfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
            }
        }
    }
    return 0;
}
