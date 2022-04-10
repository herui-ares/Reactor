# Reactor
本项目对比了epoll和采用Reactor模型的epoll并发程序，分析了程序的不同以及各自的特点。



**程序使用演示：**

- **epoll的使用**

[![pthread](https://github.com/herui-ares/select-poll-epoll/raw/main/picture/pthread.png)](https://github.com/herui-ares/select-poll-epoll/blob/main/picture/pthread.png)

- **Reactor程序使用**

[![select](https://github.com/herui-ares/select-poll-epoll/raw/main/picture/select.png)](https://github.com/herui-ares/select-poll-epoll/blob/main/picture/select.png)







## epoll

网络IO操作一般会涉及两个系统对象，一个是用户空间调用 IO 的进程或者线程，另一个是内核空间的内核系统，比如发生 IO 操作 **read** 时，它会经历两个阶段：

1. 等待数据准备就绪

2. 将数据从内核拷贝到进程或者线程中。

不同情况地处理这两个阶段上，可以分为不同的服务器模型，最常用的如**Reactor** **与** **Proactor** ，本文主要记录自己对epoll到reactor模型的演变及分析。

下面是利用**epoll**多路复用和**非阻塞IO**+**ET边缘触发**设计的并发服务器程序：（详细了epoll原理分析可查看该文章链接: [link](https://blog.csdn.net/weixin_44477424/article/details/124049788?spm=1001.2014.3001.5502).）

```c++
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


```

**epoll监控事件描述图：**

![在这里插入图片描述](https://img-blog.csdnimg.cn/b3b9780700fc4789ba56820bc6cea079.png?x-oss-process=image/watermark,type_d3F5LXplbmhlaQ,shadow_50,text_Q1NETiBAQXJpZXPkuI3kvJpDKys=,size_20,color_FFFFFF,t_70,g_se,x_16)

## Reactor服务器模型


**epoll的Reactor模型监控事件描述图：**
![在这里插入图片描述](https://img-blog.csdnimg.cn/fc778f59b9f24ebdbc3f472dc512ff69.png?x-oss-process=image/watermark,type_d3F5LXplbmhlaQ,shadow_50,text_Q1NETiBAQXJpZXPkuI3kvJpDKys=,size_20,color_FFFFFF,t_70,g_se,x_16)
从图中可以看出，reactor多了一个结构体。服务进程处理epoll事件也不是单纯的逻辑操作，而是通过对应的事件数组中事件对应的fd指向的回调函数来处理接下来的逻辑操作。

回想一下普通函数调用的机制：程序调用某函数，函数执行，程序等待，函数将结果和控制权返回给程序，程序继续处理。Reactor 释义“反应堆”，是一种事件驱动机制。和普通函数调用的不同之处在于：应用程序不是主动的调用某个 API 完成处理，而是恰恰相反，Reactor 逆置了事件处理流程(不再是主动地等事件就绪，而是它提前注册好的回调函数，当有对应事件发生时就调用回调函数)，应用程序需要提供相应的接口并注册到 Reactor 上，如果相应的事件发生，Reactor 将主动调用应用程序注册的接口，这些接口又称为“回调函
数”。

**Reactor程序如下：**

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>


#define BUFFER_LENGTH		4096
#define MAX_EPOLL_EVENTS	1024
#define SERVER_PORT			10000

typedef int NCALLBACK(int ,int, void*);

struct ntyevent {
	int fd;//文件描述符
	int events;//事件
	void *arg;//回调函数的参数
	int (*callback)(int fd, int events, void *arg);
	
	int status;
	char buffer[BUFFER_LENGTH];//缓存区
	int length;//
	long last_active;
};



struct ntyreactor {//结构体包含一个文件描述符和一个事件指针
	int epfd;
	struct ntyevent *events;
};


int recv_cb(int fd, int events, void *arg);
int send_cb(int fd, int events, void *arg);


void nty_event_set(struct ntyevent *ev, int fd, NCALLBACK callback, void *arg) {

	ev->fd = fd;
	ev->callback = callback;
	ev->events = 0;
	ev->arg = arg;
	ev->last_active = time(NULL);

	return ;
	
}


int nty_event_add(int epfd, int events, struct ntyevent *ev) {

	struct epoll_event ep_ev = {0, {0}};
	ep_ev.data.ptr = ev;//设置要处理的事件的指针，含有文件描述符等信息
	ep_ev.events = ev->events = events;//设置要处理的事件类型

	int op;
	if (ev->status == 1) {
		op = EPOLL_CTL_MOD;
	} else {
		op = EPOLL_CTL_ADD;
		ev->status = 1;
	}

	if (epoll_ctl(epfd, op, ev->fd, &ep_ev) < 0) {//注册epoll事件
		printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
		return -1;
	}

	return 0;
}

int nty_event_del(int epfd, struct ntyevent *ev) {

	struct epoll_event ep_ev = {0, {0}};

	if (ev->status != 1) {
		return -1;
	}

	ep_ev.data.ptr = ev;
	ev->status = 0;
	epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, &ep_ev);//删除注册epoll事件

	return 0;
}

int recv_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	struct ntyevent *ev = reactor->events+fd;

	int len = recv(fd, ev->buffer, 16, 0); //BUFFER_LENGTH   //读取fd的数据到相应的buffer 中，16是指定buffer长度（可以理解乘读取数据最大长度）
	nty_event_del(reactor->epfd, ev);

	if (len > 0) {
		
		ev->length = len;
		ev->buffer[len] = '\0';

		printf("C[%d]:%s\n", fd, ev->buffer);

		nty_event_set(ev, fd, send_cb, reactor);//设置send_cb回调函数
		nty_event_add(reactor->epfd, EPOLLOUT, ev);//注册EPOLLOUT事件
		
		
	} else if (len == 0) {

		close(ev->fd);
		printf("[fd=%d] pos[%ld], closed\n", fd, ev-reactor->events);
		 
	} else {

		close(ev->fd);
		printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
		
	}

	return len;
}


int send_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	struct ntyevent *ev = reactor->events+fd;

	int len = send(fd, ev->buffer, ev->length, 0);//发送数据
	if (len > 0) {
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

		nty_event_del(reactor->epfd, ev);//删除epoll注册事件
		nty_event_set(ev, fd, recv_cb, reactor);//设置回调函数
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, ev);//条件epoll注册事件
		
	} else {

		close(ev->fd);

		nty_event_del(reactor->epfd, ev);
		printf("send[fd=%d] error %s\n", fd, strerror(errno));

	}

	return len;
}

int accept_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	if (reactor == NULL) return -1;

	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);

	int clientfd;

	if ((clientfd = accept(fd, (struct sockaddr*)&client_addr, &len)) == -1) {//accept绑定已连接队列中的socket
		if (errno != EAGAIN && errno != EINTR) {
			
		}
		printf("accept: %s\n", strerror(errno));
		return -1;
	}

	int i = 0;
	do {
		
		for (i = 3;i < MAX_EPOLL_EVENTS;i ++) {//0，1，2是系统固定的文件描述符
			if (reactor->events[i].status == 0) {//
				break;
			}
		}
		if (i == MAX_EPOLL_EVENTS) {
			printf("%s: max connect limit[%d]\n", __func__, MAX_EPOLL_EVENTS);
			break;
		}

		int flag = 0;
		if ((flag = fcntl(clientfd, F_SETFL, O_NONBLOCK)) < 0) {//设置与客户端通信的socket为非阻塞
			printf("%s: fcntl nonblocking failed, %d\n", __func__, MAX_EPOLL_EVENTS);
			break;
		}

		nty_event_set(&reactor->events[clientfd], clientfd, recv_cb, reactor);  //设置当前fd的回调函数等信息
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, &reactor->events[clientfd]);//添加当前fd的触发信号EPOLLIN | EPOLLET

	} while (0);

	printf("new connect [%s:%d][time:%ld], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), reactor->events[i].last_active, i);//输出连接的地址：端口：时间：fd

	return 0;

}

int init_sock(short port) {

	int fd = socket(AF_INET, SOCK_STREAM, 0);//创建socket文件描述符
	fcntl(fd, F_SETFL, O_NONBLOCK);//设置非阻塞fd

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));//绑定端口和IP地址

	if (listen(fd, 20) < 0) {//监听端口
		printf("listen failed : %s\n", strerror(errno));
	}

	return fd;
}


int ntyreactor_init(struct ntyreactor *reactor) {

	if (reactor == NULL) return -1;
	memset(reactor, 0, sizeof(struct ntyreactor));//memset清0

	reactor->epfd = epoll_create(1); //创建epoll，返回文件描述符根节点（epoll是以红黑树存储描述符）
	if (reactor->epfd <= 0) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		return -2;
	}

	reactor->events = (struct ntyevent*)malloc((MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));//分配存储事件的空间
	if (reactor->events == NULL) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		close(reactor->epfd);
		return -3;
	}
}

int ntyreactor_destory(struct ntyreactor *reactor) {

	close(reactor->epfd);
	free(reactor->events);

}



int ntyreactor_addlistener(struct ntyreactor *reactor, int sockfd, NCALLBACK *acceptor) {

	if (reactor == NULL) return -1;
	if (reactor->events == NULL) return -1;

	nty_event_set(&reactor->events[sockfd], sockfd, acceptor, reactor);//设置回调函数
	nty_event_add(reactor->epfd, EPOLLIN, &reactor->events[sockfd]);//添加epoll注册事件

	return 0;
}



int ntyreactor_run(struct ntyreactor *reactor) {
	if (reactor == NULL) return -1;
	if (reactor->epfd < 0) return -1;
	if (reactor->events == NULL) return -1;
	
	struct epoll_event events[MAX_EPOLL_EVENTS+1];
	
	int checkpos = 0, i;

	while (1) {//此处循环是看连接是否超时，采用轮询验证

		long now = time(NULL);
		for (i = 0;i < 100;i ++, checkpos ++) {
			if (checkpos == MAX_EPOLL_EVENTS) {
				checkpos = 0;
			}

			if (reactor->events[checkpos].status != 1) {
				continue;
			}

			long duration = now - reactor->events[checkpos].last_active;//延时时间

			if (duration >= 60) {//时间大于60，关闭文件描述符
				close(reactor->events[checkpos].fd);//关闭文件描述符
				printf("[fd=%d] timeout\n", reactor->events[checkpos].fd);
				nty_event_del(reactor->epfd, &reactor->events[checkpos]);//删除epoll注册事件
			}
		}


		int nready = epoll_wait(reactor->epfd, events, MAX_EPOLL_EVENTS, 1000);//events用于回传要处理的事件
		if (nready < 0) {
			printf("epoll_wait error, exit\n");
			continue;
		}

		for (i = 0;i < nready;i ++) {

			struct ntyevent *ev = (struct ntyevent*)events[i].data.ptr;

			if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {//监控epoll事件EPOLLIN
				ev->callback(ev->fd, events[i].events, ev->arg);//callback
			}
			if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {//监控epoll事件EPOLLOUT
				ev->callback(ev->fd, events[i].events, ev->arg);//callback
			}
			
		}

	}
}

int main(int argc, char *argv[]) {

	unsigned short port = SERVER_PORT;
	if (argc == 2) {
		port = atoi(argv[1]);
	}

	int sockfd = init_sock(port);//传入端口号，初始化socket，，返回得到监听的socketfd

	struct ntyreactor *reactor = (struct ntyreactor*)malloc(sizeof(struct ntyreactor));//创建ntyreactor结构体对象
	ntyreactor_init(reactor);//初始化结构体，， 包含一个文件描述符和一个ntyreact类型的事件指针,reactor中epfd是监听红黑树的根节点fd
	
	ntyreactor_addlistener(reactor, sockfd, accept_cb);
	ntyreactor_run(reactor);

	ntyreactor_destory(reactor);
	close(sockfd);
	

	return 0;
}

```

从程序可以看出

```cpp
int len = send(fd, ev->buffer, ev->length, 0);//发送数据
	if (len > 0) {
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

		nty_event_del(reactor->epfd, ev);//删除epoll注册事件
		nty_event_set(ev, fd, recv_cb, reactor);//设置回调函数
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, ev);//条件epoll注册事件
```

发送完数据就删除epoll注册事件，再重新注册。如此频繁的增加删除是否浪费CPU资源？
答：对于同一个socket而言，完成收发至少占用两个树上的位置（不删除的情况）。而交替（本文情况）只需要一个。因此从这个角度看增加删除操作epoll事件不会更加浪费CPU资源。


Reactor 模式是编写高性能网络服务器的必备技术之一，它具有如下的优点：

- 响应快，不必为单个同步事件所阻塞，虽然 Reactor 本身依然是同步的；
- 编程相对简单，可以最大程度的避免复杂的多线程及同步问题，并且避免了多线程/进程的切换开销；
- 可扩展性，可以方便的通过增加 Reactor 实例个数来充分利用 CPU 资源；
- 可复用性，reactor 框架本身与具体事件处理逻辑无关，具有很高的复用性；



Reactor 模型开发效率上比起直接使用 IO 复用要高，它通常是单线程的，设计目标是希望单线程使用一颗 CPU 的全部资源，但也有附带优点，即每个事件处理中很多时候可以不考虑共享资源的互斥访问。可是缺点也是明显的，现在的硬件发展，已经不再遵循摩尔定律，CPU 的频率受制于材料的限制不再有大的提升，而改为是从核数的增加上提升能力，当程序需要使用多核资源时，Reactor 模型就会悲剧。

网络服务器中还有个Proactor模型也比较流行， proactor 模型最大的特点就是 Proactor 最大的特点是使用**异步** I/O（Reactor采用的**同步**IO）。所有的 I/O 操作都交由系统提供的异步 I/O 接口去执行。工作线程仅仅负责业务逻辑。在 Proactor 中，用户函数启动一个异步的文件操作。同时将这个操作注册到多路复用器上。多路复用器并不关心文件是否可读或可写而是关心这个异步读操作是否完成。异
步操作是操作系统完成，用户程序不需要关心。多路复用器等待直到有完成通知到来。当操作系统完成了读文件操作——将读到的数据复制到了用户先前提供的缓冲区之后，通知多路复用器相关操作已完成。多路复用器再调用相应的处理程序，处理数据。
Proactor 增加了编程的复杂度，但给工作线程带来了更高的效率。Proactor 可以在系统态将读写优化，利用 I/O 并行能力，提供一个高性能单线程模型。在 windows 上，由于没有 epoll 这样的机制，因此提供了 IOCP 来支持高并发， 由于操作系统做了较好的优化，windows 较常采用 Proactor 的模型利用完成端口来实现服务器。在 linux 上，在2.6 内核出现了 aio 接口，但 aio 实际效果并不理想，它的出现，主要是解决 poll 性能不佳的问题，但实际上经过测试，epoll 的性能高于 poll+aio，并且 aio 不能处理 accept，因此 linux 主要还是以 Reactor 模型为主。
