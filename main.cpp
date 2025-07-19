#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cerrno>

#ifndef nullptr
# define nullptr (0)
#endif

std::string get_event_flags(uint32_t events) {
  std::string flags = "";
  if (events & EPOLLIN) flags += "EPOLLIN ";
  if (events & EPOLLOUT) flags += "EPOLLOUT ";
  if (events & EPOLLRDHUP) flags += "EPOLLRDHUP ";
  if (events & EPOLLPRI) flags += "EPOLLPRI ";
  if (events & EPOLLERR) flags += "EPOLLERR ";
  if (events & EPOLLHUP) flags += "EPOLLHUP ";
  if (events & EPOLLET) flags += "EPOLLET "; // Edge-triggered
  if (events & EPOLLONESHOT) flags += "EPOLLONESHOT ";
  if (events & EPOLLWAKEUP) flags += "EPOLLWAKEUP ";

  if (flags.empty()) {
    std::stringstream ss;
    ss << "UNKNOWN_EVENT (" << events << ") ";
    flags = ss.str();
  }
  return flags;
}

class IrcServer {
public:
  IrcServer()
    : port_(8080)
  {
  }
  ~IrcServer()
  {
  }

  bool activate() {
    if ((listen_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket");
      return false;
    }

    // SO_REUSEADDR オプションを設定
    int optval = 1; // オプションを有効にするための値
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
      perror("setsockopt");
      close(listen_fd_);
      return false;
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd_, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
      perror("setsockopt");
      close(listen_fd_);
      return false;
    }
    if (listen(listen_fd_, SOMAXCONN) == -1) {
      perror("setsockopt");
      close(listen_fd_);
      return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
      perror("setsockopt");
      close(listen_fd_);
      return false;
    }
    ev_.events = EPOLLIN;
    ev_.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev_) == -1) {
      perror("epoll_ctl: listen_fd");
      close(listen_fd_);
      close(epoll_fd_);
      return false;

    }

    // イベントループ
    while (1) {
      struct epoll_event events[100];
      int n = epoll_wait(epoll_fd_, events, 100, -1);
      if (n == -1) {
        perror("epoll_wait");
        // EINTR（シグナルによる中断）は無視して継続することが多い
        if (errno == EINTR) {
          continue;
        }
        break; // ループを抜ける
      }
      for (int i = 0; i < n; ++i) {
        std::clog
          << "i: " << i << "\n"
          << "event kind: " << get_event_flags(events[i].events) << "\n"
          << "event fd: " << events[i].data.fd << "\n"
          << std::flush;
        if (events[i].data.fd == listen_fd_) {
          std::clog << "[EVENT]: connection with new client" << std::endl;
          acceptNewClient();
        } else {
          int client_fd = events[i].data.fd;
          if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP || events[i].events & EPOLLRDHUP) {
            std::clog << "Client disconnected or error. fd: " << client_fd
                      << ", event: " << get_event_flags(events[i].events) << std::endl;
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
            return false;
          }
          std::clog << "[EVENT]: read from client" << std::endl;
          // クライアントソケットからの読み取り
          char buf[1024];
          int read_num = read(events[i].data.fd, buf, 1024);
          std::clog << "read num: " << read_num << std::endl;
          if (read_num < 0) {
            std::clog << "[EVENT]: disconnect with error (negative read num)" << std::endl;
            perror("read error");
            disconnectClient(events[i].data.fd);
          } else if (read_num == 0) {
            std::clog << "[EVENT]: disconnect with FIN (read num is 0)" << std::endl;
            disconnectClient(events[i].data.fd);
          } else {
            buf[read_num] = '\0';
            std::cout << "read: [" << buf << "]" << std::endl;
          }
        }
      }
      std::clog << "----------------" << std::endl;
    }
    return false;
  }

private:
  void acceptNewClient() {
    // 新しい接続を受け付け
    sockaddr_in client_address;
    int client_addr_len;
    int conn_fd = accept(listen_fd_, (struct sockaddr *)&client_address, (socklen_t*)&client_addr_len);
    if (conn_fd == -1) {
      perror("accept");
      // EAGAINやEWOULDBLOCKは致命的ではないが、この例では何もしない
      return;
    }
    // epollに登録
    struct epoll_event conn_ev;
    conn_ev.events = EPOLLIN | EPOLLET;
    conn_ev.data.fd = conn_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn_fd, &conn_ev) == -1) {
      perror("epoll_ctl: conn_fd");
      close(conn_fd);
    }
  }

  void disconnectClient(int client_fd) {
    // epollから削除
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, NULL);
    // fdを閉じる(ソケットのオープンファイル?への参照を正しく減らしてリソース開放させるため)
    close(client_fd);
  }

  int listen_fd_;
  int epoll_fd_;
  struct epoll_event ev_;

  int port_;
};

int main(void) {
  IrcServer server = IrcServer();
  server.activate();
}
