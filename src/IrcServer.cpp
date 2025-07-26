#include "IrcServer.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <cerrno>
#include <map>
#include <cstdlib>
#include <fcntl.h>
#include <stdint.h>

#ifndef nullptr
# define nullptr (0)
#endif

const std::string SERVER_NAME = "irc.example.com";

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

IrcServer::IrcServer(int port, const std::string& password)
  : port_(port)
  , password_(password)
{
}

IrcServer::~IrcServer()
{
}

bool IrcServer::activate() {
  if ((listen_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return false;
  }
  if (fcntl(listen_fd_, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl");
    close(listen_fd_);
    return false;
  }
  
  int optval = 1;
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

        if (event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
          disconnectClient(client_fd);
          continue;
        }
        // 
        if (event_flags & EPOLLOUT) {
          handleClientWrite(client_fd);
        }
        if (event_flags & EPOLLIN) {
          std::clog << "[EVENT]: read from client" << std::endl;
          // 
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
            // : O(N^2)
            client_buffers_[client_fd].append(buf, read_num);
            size_t pos;
            while ((pos = client_buffers_[client_fd].find('\n')) != std::string::npos) {
              std::string message = client_buffers_[client_fd].substr(0, pos);
              if (!message.empty() && message[message.length() - 1] == '\r') {
                message.erase(message.length() - 1);
              }
              std::string reply = processMessage(client_fd, message);
              if (!reply.empty()) {
                sendMessage(client_fd, reply);
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

void IrcServer::acceptNewClient() {
  // 
  sockaddr_in client_address;
  int client_addr_len;
  int conn_fd = accept(listen_fd_, (struct sockaddr *)&client_address, (socklen_t*)&client_addr_len);
  if (conn_fd == -1) {
    perror("accept");
    // EAGAINEWOULDBLOCK
    return;
  }
  if (fcntl(conn_fd, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl conn_fd");
    close(conn_fd);
    return;
  }
  struct epoll_event conn_ev;
  conn_ev.events = EPOLLIN | EPOLLET;
  conn_ev.data.fd = conn_fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn_fd, &conn_ev) == -1) {
    perror("epoll_ctl: conn_fd");
    close(conn_fd);
  }
  clients_.insert(std::make_pair(conn_fd, Client(conn_fd)));
  client_buffers_[conn_fd] = "";
  write_buffers_[conn_fd] = "";
}

void IrcServer::disconnectClient(int client_fd) {
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, NULL);
  close(client_fd);
  client_buffers_.erase(client_fd);
  write_buffers_.erase(client_fd);
  clients_.erase(client_fd);
}

void IrcServer::handleClientWrite(int client_fd) {
  if (write_buffers_[client_fd].empty()) {
    return;
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

  if (write_buffers_[client_fd].empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // OUT
    ev.data.fd = client_fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev);
  }
}

void IrcServer::sendMessage(int client_fd, const std::string& message) {
  bool was_empty = write_buffers_[client_fd].empty();
  write_buffers_[client_fd] += message;

  if (was_empty) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT; // OUT
    ev.data.fd = client_fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev);
  }
}

std::string IrcServer::processMessage(int client_fd, std::string message) {
  std::cout << "Message from fd(" << client_fd << "): [" << message << "]" << std::endl;

  Command cmd = parseCommand(message);

  if (cmd.command == "CAP") {
    if (cmd.parameters.size() >= 1 && cmd.parameters[0] == "LS") {
      sendMessage(client_fd, ":" + SERVER_NAME + " CAP * LS :\r\n"); // No capabilities for now
      sendMessage(client_fd, ":" + SERVER_NAME + " CAP * END\r\n");
    } else {
      sendMessage(client_fd, ":" + SERVER_NAME + " 421 CAP :Unknown CAP subcommand\r\n");
    }
  } else if (cmd.command == "PASS") {
    handlePassCommand(client_fd, cmd);
  } else if (clients_[client_fd].getAuthLevel() < 1) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 451 :You have not registered\r\n");
  } else if (cmd.command == "NICK") {
    handleNickCommand(client_fd, cmd);
  } else if (clients_[client_fd].getAuthLevel() < 2) {
    // pass
  } else if (cmd.command == "USER") {
    handleUserCommand(client_fd, cmd);
  } else if (clients_[client_fd].getAuthLevel() < 3) {
    // pass
  } else {
    sendMessage(client_fd, ":" + SERVER_NAME + " 421 " + cmd.command + " :Unknown command\r\n");
  }
  return "";
}

Command IrcServer::parseCommand(const std::string& message) {
  Command cmd;
  std::string s = message;
  size_t pos = 0;

  if (s[0] == ':') {
    pos = s.find(' ');
    if (pos != std::string::npos) {
      cmd.prefix = s.substr(1, pos - 1);
      s.erase(0, pos + 1);
    } else {
      return cmd;
    }
  }

  pos = s.find(' ');
  if (pos != std::string::npos) {
    cmd.command = s.substr(0, pos);
    s.erase(0, pos + 1);
  } else {
    cmd.command = s;
    return cmd;
  }

  while (!s.empty()) {
    if (s[0] == ':') {
      cmd.parameters.push_back(s.substr(1));
      break;
    } else {
      pos = s.find(' ');
      if (pos != std::string::npos) {
        cmd.parameters.push_back(s.substr(0, pos));
        s.erase(0, pos + 1);
      } else {
        cmd.parameters.push_back(s);
        break;
      }
    }
  }
  return cmd;
}



void IrcServer::handlePassCommand(int client_fd, const Command& cmd) {
  if (cmd.parameters.empty()) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 461 PASS :Not enough parameters\r\n");
    return;
  }
  if (clients_[client_fd].getAuthLevel() >= 1) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 462 :You may not reregister\r\n");
    return;
  }
  if (cmd.parameters[0] == password_) {
    clients_[client_fd].setAuthLevel(1);
    // sendMessage(client_fd, ":" + SERVER_NAME + " NOTICE AUTH :*** Password accepted - you are now recognized.\r\n");
  } else {
    std::string client_name = clients_[client_fd].getNickname().empty() ? "*" : clients_[client_fd].getNickname();
    sendMessage(client_fd, ":" + SERVER_NAME + " 464 " + client_name + " :Password incorrect\r\n");
    return;
  }
}

void IrcServer::handleNickCommand(int client_fd, const Command& cmd) {
  if (cmd.parameters.empty()) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 431 :No nickname given\r\n");
    return;
  }
  // XXX: わざわざ合成してからvalidationをしている
  std::string nickname = cmd.parameters[0];
  // Reconstruct the full nickname in case parseCommand split it
  for (size_t i = 1; i < cmd.parameters.size(); ++i) {
    nickname += " " + cmd.parameters[i];
  }
  if (!isValidNickname(nickname)) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 432 " + nickname + " :Erroneous nickname\r\n");
    return;
  }

  for (std::map<int, Client>::iterator it = clients_.begin(); it != clients_.end(); ++it) {
    std::clog << "[DEBUG] Existing client FD: " << it->first << ", Nickname: [" << it->second.getNickname() << "]" << std::endl;
    if (it->first != client_fd && it->second.getNickname() == nickname) {
      std::clog << "433 pattern" << std::endl;
      sendMessage(client_fd, ":" + SERVER_NAME + " 433 * :Nickname is already in use\r\n");
      return;
    }
  }
  // 既存のニックネームがある場合、変更メッセージを送信
  if (!clients_[client_fd].getNickname().empty()) {
    sendMessage(client_fd, ":" + clients_[client_fd].getNickname() + " NICK :" + nickname + "\r\n");
  }
  clients_[client_fd].setNickname(nickname);
  clients_[client_fd].setAuthLevel(2);
  // sendMessage(client_fd, ":" + SERVER_NAME + " NOTICE AUTH :*** Nickname set to " + nickname + ".\r\n");
}

bool IrcServer::isValidNickname(const std::string& nickname) {
  if (nickname.empty() || nickname.length() > 9) {
    return false;
  }
  // First character must be a letter or a special character
  if (!((nickname[0] >= 'a' && nickname[0] <= 'z') ||
        (nickname[0] >= 'A' && nickname[0] <= 'Z') ||
        std::string("[]\\`_^{|}") .find(nickname[0]) != std::string::npos)) {
    return false;
  }
  // Subsequent characters can be letters, digits, or special characters
  for (size_t i = 1; i < nickname.length(); ++i) {
    if (!((nickname[i] >= 'a' && nickname[i] <= 'z') ||
          (nickname[i] >= 'A' && nickname[i] <= 'Z') ||
          (nickname[i] >= '0' && nickname[i] <= '9') ||
          std::string("-[]\\`_^{|}") .find(nickname[i]) != std::string::npos)) {
      return false;
    }
  }
  return true;
}

std::string IrcServer::toLower(const std::string& str) {
  std::string lower_str = str;
  for (size_t i = 0; i < lower_str.length(); ++i) {
    lower_str[i] = std::tolower(lower_str[i]);
  }
  return lower_str;
}

void IrcServer::handleUserCommand(int client_fd, const Command& cmd) {
  if (cmd.parameters.size() < 4) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 461 USER :Not enough parameters\r\n");
    return;
  }
  if (!clients_[client_fd].getUsername().empty()) {
    sendMessage(client_fd, ":" + SERVER_NAME + " 462 :You may not reregister\r\n");
    return;
  }
  clients_[client_fd].setUsername(cmd.parameters[0]);
  clients_[client_fd].setAuthLevel(3);
  std::string realname = cmd.parameters[3];
  // sendMessage(client_fd, ":" + SERVER_NAME + " NOTICE AUTH :*** Username and realname set.\r\n");
  sendMessage(client_fd, ":" + SERVER_NAME + " 001 " + clients_.at(client_fd).getNickname() + " :Welcome to the ft_irc Network, " + clients_.at(client_fd).getNickname() + "!\r\n");
}

