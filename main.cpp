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
#include <vector>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <ctime>
#include <cstring> // For memset
#include <cctype>
#include "Client.hpp"
#include "Channel.hpp"

#define MAX_CONNECTIONS 100  // 最大接続数
#define MAX_NICKNAME_LENGTH 9  // RFC 1459に基づくニックネームの最大長
#define MAX_MESSAGE_LENGTH 512  // RFC 1459に基づくメッセージの最大長

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
    : password_(password), port_(port) // Initialize in declaration order
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

    int optval = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
      perror("setsockopt");
      close(listen_fd_);
      return false;
    }

    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address)); // It's good practice to zero out the struct.
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
        if (errno == EINTR) continue;
        perror("epoll_wait");
        break;
      }
      for (int i = 0; i < n; ++i) {
        if (events[i].data.fd == listen_fd_) {
          acceptNewClient();
        } else {
          int client_fd = events[i].data.fd;
          uint32_t event_flags = events[i].events;

          if (event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            disconnectClient(client_fd);
            continue;
          }
          if (event_flags & EPOLLOUT) {
            handleClientWrite(client_fd);
          }
          if (event_flags & EPOLLIN) {
            handleClientRead(client_fd);
          }
        }
      }
    }
    return false;
  }

private:
  void acceptNewClient() {
    sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    int conn_fd = accept(listen_fd_, (struct sockaddr *)&client_address, &client_addr_len);
    if (conn_fd == -1) {
      perror("accept");
      return;
    }
    
    int flags = fcntl(conn_fd, F_GETFL, 0);
    fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK);
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);

    Client* client = new Client(conn_fd);
    client->setHostname(std::string(client_ip));
    clients_[conn_fd] = client;
    
    struct epoll_event conn_ev;
    conn_ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    conn_ev.data.fd = conn_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn_fd, &conn_ev) == -1) {
      perror("epoll_ctl: conn_fd");
      close(conn_fd);
      delete client;
      clients_.erase(conn_fd);
    } else {
      std::cout << "New client connected: fd=" << conn_fd << ", ip=" << client_ip << std::endl;
    }
  }

  void handleClientRead(int client_fd) {
    char buf[1024];
    int read_num = read(client_fd, buf, 1024);
    if (read_num < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("read error");
        disconnectClient(client_fd);
      }
    } else if (read_num == 0) {
      disconnectClient(client_fd);
    } else {
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

  void disconnectClient(int client_fd) {
    std::cout << "Client disconnected: fd=" << client_fd;
    std::map<int, Client*>::iterator it = clients_.find(client_fd);
    if (it != clients_.end()) {
      std::cout << ", nickname=" << it->second->getNickname();
      delete it->second; // 動的に確保したメモリを解放
      clients_.erase(it);
    }
    std::cout << std::endl;
    
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
    write_buffers_.erase(client_fd);
    client_buffers_.erase(client_fd);
  }

  void handleClientWrite(int client_fd) {
    if (!write_buffers_.count(client_fd) || write_buffers_[client_fd].empty()) {
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
      ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
      ev.data.fd = client_fd;
      epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev);
    }
  }

  void queueMessage(int client_fd, const std::string& message) {
    bool was_empty = true;
    if (write_buffers_.count(client_fd)) {
        was_empty = write_buffers_[client_fd].empty();
    }
    write_buffers_[client_fd] += message;

    if (was_empty) {
      struct epoll_event ev;
      ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
      ev.data.fd = client_fd;
      epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev);
    }
  }

  bool isValidNickname(const std::string& nickname) {
    if (nickname.empty() || nickname.length() > MAX_NICKNAME_LENGTH) {
        return false;
    }

    static const std::string special_chars_first = "[]\\`_^{|}";
    static const std::string allowed_middle_chars = "-[]\\`_^{|}";

    if (!isalpha(nickname[0]) && special_chars_first.find(nickname[0]) == std::string::npos) {
        return false;
    }

    for (size_t i = 1; i < nickname.length(); ++i) {
      if (!isalnum(nickname[i]) && allowed_middle_chars.find(nickname[i]) == std::string::npos) {
        return false;
      }
    }
    return true;
  }
  
  bool isNicknameInUse(const std::string& nickname) {
    for (std::map<int, Client*>::const_iterator it = clients_.begin(); it != clients_.end(); ++it) {
      if (it->second->getNickname() == nickname) 
        return true;
    }
    return false;
  }
  
  std::string getServerName() const { 
    return "irc.42.jp"; 
  }
  
  std::string getCurrentTime() const {
    time_t now = time(0);
    char buf[80];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", localtime(&now));
    return buf;
  }
  
  std::string processMessage(int client_fd, std::string message) {
    if (message.length() > MAX_MESSAGE_LENGTH) {
      return "ERROR :Message too long\r\n";
    }
    std::map<int, Client*>::iterator it = clients_.find(client_fd);
    if (it == clients_.end()) {
        std::cerr << "Error: processMessage called for a non-existent client_fd: " << client_fd << std::endl;
        return "";
    }
    Client& client = *it->second;
    std::string client_id;
    if (client.getNickname().empty()) {
        std::stringstream ss;
        ss << "fd(" << client_fd << ")";
        client_id = ss.str();
    } else {
        client_id = client.getNickname();
    }
    std::cout << "Message from " << client_id << ": [" << message << "]" << std::endl;
    
    std::string prefix;
    // A message can optionally start with a prefix.
    if (message[0] == ':') {
        size_t space_pos = message.find(" ");
        if (space_pos != std::string::npos) {
            prefix = message.substr(1, space_pos - 1);
            message = message.substr(space_pos + 1);
        } else {
            // A message with only a prefix is invalid.
            return ""; // Silently ignore.
        }
    }

    std::string command, params;
    size_t space_pos = message.find(" ");
    if (space_pos != std::string::npos) {
      command = message.substr(0, space_pos);
      params = message.substr(space_pos + 1);
    } else {
      command = message;
    }
    
    std::string upper_command = command;
    for (size_t i = 0; i < upper_command.length(); ++i) {
        upper_command[i] = toupper(upper_command[i]);
    }
    if (upper_command == "PASS") {
      return handleCommandPass(client, params);
    }
    if (!client.isAuthenticated()) {
      return ":" + getServerName() + " 451 " + (client.getNickname().empty() ? "*" : client.getNickname()) + " :You have not registered\r\n";
    }
    if (upper_command == "NICK") {
      return handleCommandNick(client, params);
    } else if (upper_command == "USER") {
      return handleCommandUser(client, params);
    }  else if (upper_command == "PING") {
      return "PONG " + getServerName() + " :" + (params.empty() ? getServerName() : params) + "\r\n";
    } else if (upper_command == "QUIT") {
      return "";
    } else {
      if (!client.isRegistered()) {
        return ":" + getServerName() + " 451 " + client.getNickname() + " :You have not registered\r\n";
      }
      return ":" + getServerName() + " 421 " + client.getNickname() + " " + upper_command + " :Unknown command\r\n";
    }
  }
  
  std::string sendWelcomeMessages(const Client& client) {
    std::string result;
    result += ":" + getServerName() + " 001 " + client.getNickname() + " :Welcome to the Internet Relay Network " + client.getNickname() + "!" + client.getUsername() + "@" + client.getHostname() + "\r\n";
    result += ":" + getServerName() + " 002 " + client.getNickname() + " :Your host is " + getServerName() + ", running version 1.0\r\n";
    result += ":" + getServerName() + " 003 " + client.getNickname() + " :This server was created " + getCurrentTime() + "\r\n";
    result += ":" + getServerName() + " 004 " + client.getNickname() + " " + getServerName() + " 1.0 o o\r\n";
    return result;
  }

  std::string handleCommandPass(Client& client, const std::string& params) {
    if (client.isRegistered())
      return ":" + getServerName() + " 462 " + client.getNickname() + " :You may not reregister\r\n";
    if (params.empty())
      return ":" + getServerName() + " 461 " + (client.getNickname().empty() ? "*" : client.getNickname()) + " PASS :Not enough parameters\r\n";
    if (params == password_) {
      client.setAuthenticated(true);
      return "";
    } else {
      return ":" + getServerName() + " 464 " + (client.getNickname().empty() ? "*" : client.getNickname()) + " :Password incorrect\r\n";
    }
  }

  std::string handleCommandNick(Client& client, const std::string& params) {
    if (params.empty())
      return ":" + getServerName() + " 431 " + (client.getNickname().empty() ? "*" : client.getNickname()) + " :No nickname given\r\n";
    std::string new_nickname = params.substr(0, params.find(" "));
    if (!isValidNickname(new_nickname))
      return ":" + getServerName() + " 432 " + (client.getNickname().empty() ? "*" : client.getNickname()) + " " + new_nickname + " :Erroneous nickname\r\n";
    if (isNicknameInUse(new_nickname))
      return ":" + getServerName() + " 433 " + (client.getNickname().empty() ? "*" : client.getNickname()) + " " + new_nickname + " :Nickname is already in use\r\n";
    
    std::string old_nickname = client.getNickname();
    client.setNickname(new_nickname);
    if (!old_nickname.empty()) {
      return ":" + old_nickname + "!" + client.getUsername() + "@" + client.getHostname() + " NICK :" + new_nickname + "\r\n";
    }
    // Check if the user can now be fully registered
    if (!client.getUsername().empty() && !client.isRegistered()) {
      client.setRegistered(true);
      return sendWelcomeMessages(client);
    }
    return "";
  }

  std::string handleCommandUser(Client& client, const std::string& params) {
    if (client.isRegistered())
      return ":" + getServerName() + " 462 " + client.getNickname() + " :You may not reregister\r\n";
    size_t pos = params.find(" :");
    std::string realname = (pos == std::string::npos) ? "" : params.substr(pos + 2);
    std::string user_params = (pos == std::string::npos) ? params : params.substr(0, pos);
    std::vector<std::string> parts;
    std::string part;
    std::istringstream iss(user_params);
    while (iss >> part) parts.push_back(part);
    if (parts.size() < 3 || realname.empty())
      return ":" + getServerName() + " 461 " + client.getNickname() + " USER :Not enough parameters\r\n";
    
    client.setUsername(parts[0]);
    client.setRealname(realname);
    
    if (!client.getNickname().empty()) {
      client.setRegistered(true);
      return sendWelcomeMessages(client);
    }
    return "";
  }

  // std::string handleCommandJoin(Client& client, std::string params) {

  // }

  int listen_fd_;
  int epoll_fd_;
  struct epoll_event ev_;
  std::map<int, std::string> client_buffers_;
  std::map<int, std::string> write_buffers_;
  std::map<int, Client*> clients_;
  std::string password_; // Declaration order
  int port_;             // must match initializer list order

  std::map<std::string, Channel*> channels_; //<ChannelName, ChannelClass*>
};

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
    return 1;
  }

  int port = atoi(argv[1]); // atoi is in the global namespace
  std::string password = argv[2];

  IrcServer server(port, password);
  server.activate();
  return 0;
}
