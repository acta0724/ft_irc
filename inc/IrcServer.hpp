#ifndef IRCSERVER_HPP
# define IRCSERVER_HPP

# include <string>
# include <map>
# include <vector>
# include <sys/epoll.h>
# include <stdint.h>
# include "Client.hpp"
# include "Command.hpp"

extern const std::string SERVER_NAME;

class IrcServer {
public:
  IrcServer(int port, const std::string& password);
  ~IrcServer();

  bool activate();

private:
  void acceptNewClient();
  void disconnectClient(int client_fd);
  void handleClientWrite(int client_fd);
  void sendMessage(int client_fd, const std::string& message);
  std::string processMessage(int client_fd, std::string message);

  Command parseCommand(const std::string& message);
  void handlePassCommand(int client_fd, const Command& cmd);
  void handleNickCommand(int client_fd, const Command& cmd);
  void handleUserCommand(int client_fd, const Command& cmd);
  bool isValidNickname(const std::string& nickname);
  std::string toLower(const std::string& str);

  

  int listen_fd_;
  int epoll_fd_;
  struct epoll_event ev_;
  std::map<int, std::string> client_buffers_;
  std::map<int, std::string> write_buffers_;
  std::map<int, Client> clients_;

  int port_;
  std::string password_;
};

#endif