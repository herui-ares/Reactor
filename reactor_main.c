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
	int fd;//�ļ�������
	int events;//�¼�
	void *arg;//�ص������Ĳ���
	int (*callback)(int fd, int events, void *arg);
	
	int status;
	char buffer[BUFFER_LENGTH];//������
	int length;//
	long last_active;
};



struct ntyreactor {//�ṹ�����һ���ļ���������һ���¼�ָ��
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
	ep_ev.data.ptr = ev;//����Ҫ������¼���ָ�룬�����ļ�����������Ϣ
	ep_ev.events = ev->events = events;//����Ҫ������¼�����

	int op;
	if (ev->status == 1) {
		op = EPOLL_CTL_MOD;
	} else {
		op = EPOLL_CTL_ADD;
		ev->status = 1;
	}

	if (epoll_ctl(epfd, op, ev->fd, &ep_ev) < 0) {//ע��epoll�¼�
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
	epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, &ep_ev);//ɾ��ע��epoll�¼�

	return 0;
}

int recv_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	struct ntyevent *ev = reactor->events+fd;

	int len = recv(fd, ev->buffer, 16, 0); //BUFFER_LENGTH   //��ȡfd�����ݵ���Ӧ��buffer �У�16��ָ��buffer���ȣ��������˶�ȡ������󳤶ȣ�
	nty_event_del(reactor->epfd, ev);

	if (len > 0) {
		
		ev->length = len;
		ev->buffer[len] = '\0';

		printf("C[%d]:%s\n", fd, ev->buffer);

		nty_event_set(ev, fd, send_cb, reactor);//����send_cb�ص�����
		nty_event_add(reactor->epfd, EPOLLOUT, ev);//ע��EPOLLOUT�¼�
		
		
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

	int len = send(fd, ev->buffer, ev->length, 0);//��������
	if (len > 0) {
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

		nty_event_del(reactor->epfd, ev);//ɾ��epollע���¼�
		nty_event_set(ev, fd, recv_cb, reactor);//���ûص�����
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, ev);//����epollע���¼�
		
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

	if ((clientfd = accept(fd, (struct sockaddr*)&client_addr, &len)) == -1) {//accept�������Ӷ����е�socket
		if (errno != EAGAIN && errno != EINTR) {
			
		}
		printf("accept: %s\n", strerror(errno));
		return -1;
	}

	int i = 0;
	do {
		
		for (i = 3;i < MAX_EPOLL_EVENTS;i ++) {//0��1��2��ϵͳ�̶����ļ�������
			if (reactor->events[i].status == 0) {//
				break;
			}
		}
		if (i == MAX_EPOLL_EVENTS) {
			printf("%s: max connect limit[%d]\n", __func__, MAX_EPOLL_EVENTS);
			break;
		}

		int flag = 0;
		if ((flag = fcntl(clientfd, F_SETFL, O_NONBLOCK)) < 0) {//������ͻ���ͨ�ŵ�socketΪ������
			printf("%s: fcntl nonblocking failed, %d\n", __func__, MAX_EPOLL_EVENTS);
			break;
		}

		nty_event_set(&reactor->events[clientfd], clientfd, recv_cb, reactor);  //���õ�ǰfd�Ļص���������Ϣ
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, &reactor->events[clientfd]);//��ӵ�ǰfd�Ĵ����ź�EPOLLIN | EPOLLET

	} while (0);

	printf("new connect [%s:%d][time:%ld], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), reactor->events[i].last_active, i);//������ӵĵ�ַ���˿ڣ�ʱ�䣺fd

	return 0;

}

int init_sock(short port) {

	int fd = socket(AF_INET, SOCK_STREAM, 0);//����socket�ļ�������
	fcntl(fd, F_SETFL, O_NONBLOCK);//���÷�����fd

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));//�󶨶˿ں�IP��ַ

	if (listen(fd, 20) < 0) {//�����˿�
		printf("listen failed : %s\n", strerror(errno));
	}

	return fd;
}


int ntyreactor_init(struct ntyreactor *reactor) {

	if (reactor == NULL) return -1;
	memset(reactor, 0, sizeof(struct ntyreactor));//memset��0

	reactor->epfd = epoll_create(1); //����epoll�������ļ����������ڵ㣨epoll���Ժ�����洢��������
	if (reactor->epfd <= 0) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		return -2;
	}

	reactor->events = (struct ntyevent*)malloc((MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));//����洢�¼��Ŀռ�
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

	nty_event_set(&reactor->events[sockfd], sockfd, acceptor, reactor);//���ûص�����
	nty_event_add(reactor->epfd, EPOLLIN, &reactor->events[sockfd]);//���epollע���¼�

	return 0;
}



int ntyreactor_run(struct ntyreactor *reactor) {
	if (reactor == NULL) return -1;
	if (reactor->epfd < 0) return -1;
	if (reactor->events == NULL) return -1;
	
	struct epoll_event events[MAX_EPOLL_EVENTS+1];
	
	int checkpos = 0, i;

	while (1) {//�˴�ѭ���ǿ������Ƿ�ʱ��������ѯ��֤

		long now = time(NULL);
		for (i = 0;i < 100;i ++, checkpos ++) {
			if (checkpos == MAX_EPOLL_EVENTS) {
				checkpos = 0;
			}

			if (reactor->events[checkpos].status != 1) {
				continue;
			}

			long duration = now - reactor->events[checkpos].last_active;//��ʱʱ��

			if (duration >= 60) {//ʱ�����60���ر��ļ�������
				close(reactor->events[checkpos].fd);//�ر��ļ�������
				printf("[fd=%d] timeout\n", reactor->events[checkpos].fd);
				nty_event_del(reactor->epfd, &reactor->events[checkpos]);//ɾ��epollע���¼�
			}
		}


		int nready = epoll_wait(reactor->epfd, events, MAX_EPOLL_EVENTS, 1000);//events���ڻش�Ҫ������¼�
		if (nready < 0) {
			printf("epoll_wait error, exit\n");
			continue;
		}

		for (i = 0;i < nready;i ++) {

			struct ntyevent *ev = (struct ntyevent*)events[i].data.ptr;

			if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {//���epoll�¼�EPOLLIN
				ev->callback(ev->fd, events[i].events, ev->arg);//callback
			}
			if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {//���epoll�¼�EPOLLOUT
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

	int sockfd = init_sock(port);//����˿ںţ���ʼ��socket�������صõ�������socketfd

	struct ntyreactor *reactor = (struct ntyreactor*)malloc(sizeof(struct ntyreactor));//����ntyreactor�ṹ�����
	ntyreactor_init(reactor);//��ʼ���ṹ�壬�� ����һ���ļ���������һ��ntyreact���͵��¼�ָ��,reactor��epfd�Ǽ���������ĸ��ڵ�fd
	
	ntyreactor_addlistener(reactor, sockfd, accept_cb);
	ntyreactor_run(reactor);

	ntyreactor_destory(reactor);
	close(sockfd);
	

	return 0;
}


