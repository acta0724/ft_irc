#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cerrno>
#include <map>
#include <cstdlib>

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
  IrcServer(int port, const std::string& password)
    : port_(port)
    , password_(password)
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
      perror("bind");
      close(listen_fd_);
      return false;
    }
    if (listen(listen_fd_, SOMAXCONN) == -1) {
      perror("listen");
      close(listen_fd_);
      return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
      perror("epoll_create1");
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
          uint32_t event_flags = events[i].events;

          // if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP || events[i].events & EPOLLRDHUP) {
          //   std::clog << "Client disconnected or error. fd: " << client_fd
          //             << ", event: " << get_event_flags(events[i].events) << std::endl;
          //   epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
          //   close(client_fd);
          //   return false;
          // }
          if (event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            disconnectClient(client_fd);
            continue;
          }
          // ✅ 書き込み可能イベント
          if (event_flags & EPOLLOUT) {
            handleClientWrite(client_fd);
          }
          if (event_flags & EPOLLIN) {
            std::clog << "[EVENT]: read from client" << std::endl;
            // クライアントソケットからの読み取りclient_buffers_[x]に追加
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
              // 時間計算量: O(N^2)
              client_buffers_[client_fd].append(buf, read_num);
              size_t pos;
              while ((pos = client_buffers_[client_fd].find('\n')) != std::string::npos) {
                std::string message = client_buffers_[client_fd].substr(0, pos);
                if (!message.empty() && message[message.length() - 1] == '\r') {
                  message.erase(message.length() - 1);
                }
                std::string reply = processMessage(client_fd, message);
                if (!reply.empty()) {
                  queueMessage(client_fd, reply);
                }
                client_buffers_[client_fd].erase(0, pos + 1);
              }
            }
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
    // bufferの削除
    client_buffers_.erase(client_fd);
    write_buffers_.erase(client_fd);
  }

  void handleClientWrite(int client_fd) {
    if (write_buffers_[client_fd].empty()) {
      return; // 送信バッファが空なら何もしない
    }

    const std::string& data_to_send = write_buffers_[client_fd];
    int sent_bytes = write(client_fd, data_to_send.c_str(), data_to_send.length());

    if (sent_bytes == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("write error");
        disconnectClient(client_fd);
      }
      return;
    }

    write_buffers_[client_fd].erase(0, sent_bytes);

    // バッファが空になったらEPOLLOUTの監視を解除
    if (write_buffers_[client_fd].empty()) {
      struct epoll_event ev;
      ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // OUTを外す
      ev.data.fd = client_fd;
      epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev);
    }
  }

  void queueMessage(int client_fd, const std::string& message) {
    bool was_empty = write_buffers_[client_fd].empty();
    write_buffers_[client_fd] += message;

    // ✅ バッファが空の状態からデータが追加された場合のみ、EPOLLOUTを登録
    if (was_empty) {
      struct epoll_event ev;
      ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT; // OUTを追加
      ev.data.fd = client_fd;
      epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev);
    }
  }

  std::string processMessage(int client_fd, std::string message) {
    std::cout << "Message from fd(" << client_fd << "): [" << message << "]" << std::endl;
    return message + "\n";
  }

  int listen_fd_;
  int epoll_fd_;
  struct epoll_event ev_;
  std::map<int, std::string> client_buffers_;
  std::map<int, std::string> write_buffers_;

  int port_;
  std::string password_;
};

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
    return 1;
  }

  int port = std::atoi(argv[1]);
  std::string password = argv[2];

  if (port <= 0 || port > 65535) {
    std::cerr << "Error: Invalid port number. Port must be between 1 and 65535." << std::endl;
    return 1;
  }

  IrcServer server = IrcServer(port, password);
  server.activate();
  return 0;
}
