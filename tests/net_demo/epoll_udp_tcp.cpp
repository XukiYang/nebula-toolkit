#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512
#define UDP_BUFFER_SIZE 1024

int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

void addfd(int epollfd, int fd) {
  struct epoll_event event;
  event.data.fd = fd;
  // event.events = EPOLLIN | EPOLLET;
  event.events = EPOLLIN; //可读事件，默认为LT模式，事件一般被触发多次
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }
  const char *ip = argv[1];
  int port = atoi(argv[2]);

  int ret = 0;
  //创建tcp套接字，并绑定 、监听
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);

  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);

  ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret != -1);

  ret = listen(listenfd, 5);
  assert(ret != -1);
  //创建udp套接字，并绑定
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);
  int udpfd = socket(PF_INET, SOCK_DGRAM, 0);
  assert(udpfd >= 0);

  ret = bind(udpfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret != -1);

  struct epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);
  assert(epollfd != -1);
  //注册tcp和udp套接字的可读事件
  addfd(epollfd, listenfd);
  addfd(epollfd, udpfd);

  while (1) {
    int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0) {
      printf("epoll failure\n");
      break;
    }

    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;
      // tcp有新的可读事件，也即接受到了新的连接
      if (sockfd == listenfd) {
        struct sockaddr_in client_address;
        socklen_t client_addrlength = sizeof(client_address);
        int connfd = accept(listenfd, (struct sockaddr *)&client_address,
                            &client_addrlength);
        //将新的连接套接字也注册可读事件
        addfd(epollfd, connfd);
      }
      // udp套接字事件处理
      else if (sockfd == udpfd) {
        char buf[UDP_BUFFER_SIZE];
        memset(buf, '\0', UDP_BUFFER_SIZE);
        struct sockaddr_in client_address;
        socklen_t client_addrlength = sizeof(client_address);

        ret = recvfrom(udpfd, buf, UDP_BUFFER_SIZE - 1, 0,
                       (struct sockaddr *)&client_address, &client_addrlength);
        if (ret > 0) {
          sendto(udpfd, buf, UDP_BUFFER_SIZE - 1, 0,
                 (struct sockaddr *)&client_address, client_addrlength);
        }
      }
      //注册的socket发生可读事件
      else if (events[i].events & EPOLLIN) {
        char buf[TCP_BUFFER_SIZE];
        while (1) {
          memset(buf, '\0', TCP_BUFFER_SIZE);
          ret = recv(sockfd, buf, TCP_BUFFER_SIZE - 1, 0);
          if (ret < 0) {
            //数据接受完毕之后进入下一次可读事件
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
              break;
            }
            close(sockfd);
            break;
          }
          //服务端已经断开连接，所以断开客户端连接
          else if (ret == 0) {
            close(sockfd);
          } else {
            send(sockfd, buf, ret, 0);
          }
        }
      } else {
        printf("something else happened \n");
      }
    }
  }

  close(listenfd);
  return 0;
}
