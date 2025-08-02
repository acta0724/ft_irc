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
#include "utils.hpp"

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
    } else if (upper_command == "JOIN") {
      handleCommandJoin(client, params);
      return "";
    } else if (upper_command == "PART") {
      handleCommandPart(client, params);
      return "";
    } else if (upper_command == "PRIVMSG") {
      handlePrivmsg(client, params);
      return "";
    } else if (upper_command == "MODE") {
      handleCommandMode(client, params);
      return "";
    } else if (upper_command == "TOPIC") {
      handleCommandTopic(client, params);
      return "";
    } else if (upper_command == "INVITE") {
      handleCommandInvite(client, params);
      return "";
    } else if (upper_command == "KICK") {
      handleCommandKick(client, params);
      return "";
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

void queueMessageEverybodyInChannelElse(const Channel& channel, const std::string msg, int elseClientFd) //elseがない場合は無効なfd(-1など)を指定する
{
  std::map<int, Client *>::const_iterator it = channel.getClients().begin();
  std::map<int, Client *>::const_iterator ite = channel.getClients().end();
  while (it != ite)
  {
    int fd = it->second->getFd();
    if (fd != elseClientFd)
      queueMessage(fd, msg);
    it++;
  }
}

const std::string getPrefix(const Client& client)
{
  return (client.getNickname() + "!" + client.getUsername() + "@" + client.getHostname());
}

void noSuchChannel(const Client& client, const std::string& channelName)
{
  std::string msg = ":" + getServerName() + " 403 " + client.getNickname() + " " + channelName + " :No such channel\r\n";
  queueMessage(client.getFd(), msg);
}

void noSuchNick(const Client& client, const std::string& nickName)
{
  std::string msg = ":" + getServerName() + " 401 " + client.getNickname() + " " + nickName + " :No such nick/channel\r\n";
  queueMessage(client.getFd(), msg);
}

void notEnoughParams(const Client& client, const std::string command)
{
  std::string msg = ":" + getServerName() + " 461 " + client.getNickname() + " " + command + " :Not enough parameters\r\n";
  queueMessage(client.getFd(), msg);
}

void notOnChannel(const Client& client, const std::string channelName)
{
  std::string msg = ":" + getServerName() + " 442 " + client.getNickname() + " " + channelName + " :You're not on that channel\r\n";
  queueMessage(client.getFd(), msg);
}

void notChannelOperator(const Client& client, const std::string channelName)
{
  std::string msg = ":" + getServerName() + " 442 " + client.getNickname() + " " + channelName + " :You're not channel operator\r\n";
  queueMessage(client.getFd(), msg);
}

void userNotInChannel(const Client& client, const std::string channelName, const std::string targetName)
{
  std::string msg = ":" + getServerName() + " 441 " + client.getNickname() + " " + targetName + " " + channelName + " :They aren't on that channel\r\n";;
  queueMessage(client.getFd(), msg);
}

void handleCommandJoin(Client& client, const std::string& params) {
  //parse
	std::string client_res_msg;

	size_t pos = params.find(' ');
	std::string left;
	std::string right;
	if (pos != std::string::npos)
	{
		left = params.substr(0, pos);
		right = params.substr(pos + 1);
	}
	else
	{
		left = params;
		right = "";
	}

	std::vector<std::string> ch_names = str_split_to_vector(left, ',');
	std::vector<std::string> ch_keys = str_split_to_vector(right, ',');
  //
	
	std::vector<std::string>::iterator name_it = ch_names.begin();
	std::vector<std::string>::iterator name_ite = ch_names.end();
	std::vector<std::string>::iterator key_it = ch_keys.begin();
	std::vector<std::string>::iterator key_ite = ch_keys.end();
	while (name_it != name_ite)
	{
		std::map<std::string, Channel*>::iterator channel_it = channels_.find(*name_it);
    Channel *channel;

		if (channel_it == channels_.end()) //create a new channel
		{
			try
			{
				channels_.insert(std::make_pair(*name_it, new Channel(*name_it)));
			}
			catch(const std::exception& e) //fail
			{
				client_res_msg.append(":" + getServerName() + " 403 " + client.getNickname() + " " + *name_it + " :No such channel\r\n");
				std::cerr << e.what() << '\n';
		    name_it++;
        if (key_it != key_ite)
			    key_it++;
        continue;
			}
		}
    channel = channels_[*name_it];

    //check key
    std::string key = channel->getKey();
    if (!(key.empty() || (key_it != key_ite && key == *key_it)))
    {
      client_res_msg.append(":" + getServerName() + " 475 " + client.getNickname() + " " + *name_it + " :Cannot join channel (+k)\r\n");
      name_it++;
      if (key_it != key_ite)
        key_it++;
      continue;
    }

    //invite check
    if (channel->isInvited(client)) //remove invitation
      channel->removeInvitedFd(client.getFd());
    else if (channel->isInviteOnly()) //not invited but need invitation
    {
      client_res_msg.append(":" + getServerName() + " 473 " + client.getNickname() + " " + *name_it + " :Cannot join channel (+i)\r\n");
      name_it++;
      if (key_it != key_ite)
        key_it++;
      continue;
    }

    //channel limit check
    if (channel->userFull())
    {
      client_res_msg.append(":" + getServerName() + " 471 " + client.getNickname() + " " + *name_it + " :Cannot join channel (+l)\r\n");
      if (key_it != key_ite)
        key_it++;
      continue;
    }

    // --- join successfully ---
    client.joinChannel(*name_it);
    
    //message for joining client
    std::string client_join_msg = ":" + getPrefix(client) + " JOIN :" + *name_it + "\r\n";//JOIN message
    if (!channel->getTopic().empty())
      client_join_msg.append(":" + getServerName() + " 332 " + client.getNickname() + " " + *name_it + " :" + channel->getTopic() + "\r\n");//TOPIC message
    else
      client_join_msg.append(":" + getServerName() + " 331 " + client.getNickname() + " " + *name_it + " :No topic is set\r\n");//NO TOPIC message
    //build clients NAMES
    std::string names = client.getNickname();
    std::map<int, Client *>::const_iterator clients_it = channel->getClients().begin();
    std::map<int, Client *>::const_iterator clients_ite = channel->getClients().end();
    while (clients_it != clients_ite)
    {
      names.append(" " + clients_it->second->getNickname());
      clients_it++;
    }
    //
    client_join_msg.append(":" + getServerName() + " 353 " + client.getNickname() + " = " + *name_it + " :" + names + "\r\n");
    client_join_msg.append(":" + getServerName() + " 366 " + client.getNickname() + " " + *name_it + " :End of NAMES list\r\n");
    client_res_msg.append(client_join_msg);
    //

    //message for eberybody else
    std::string join_msg = ":" + getPrefix(client) + " JOIN :" + *name_it + "\r\n";
    queueMessageEverybodyInChannelElse(*channel, join_msg, client.getFd());
    //
    channel->addClient(&client);

    name_it++;
    if (key_it != key_ite)
      key_it++;
  }
  queueMessage(client.getFd(), client_res_msg);
  }

  void handleCommandPart(Client& client, const std::string params)
  {
    //parse
    std::vector<std::string> channels;
    std::string leaveMsg;
    bool hasMsg = false;

    size_t pos = params.find(' ');
    if (pos != std::string::npos)
    {
      std::string channels_str = params.substr(0, pos);
      leaveMsg = params.substr(pos + 1);
      if (leaveMsg[0] == ':')
        leaveMsg.erase(0, 1);
      channels = str_split_to_vector(channels_str, ',');
      hasMsg = true;
    }
    else
    {
      channels = str_split_to_vector(params, ',');
      leaveMsg = "";
    }
    if (channels.size() < 1)
    {
      notEnoughParams(client, "PART");
      return;
    }
    //

    std::vector<std::string>::iterator it = channels.begin();
    std::vector<std::string>::iterator ite = channels.end();
    while (it != ite)
    {
      std::map<std::string, Channel *>::iterator ch_it = channels_.find(*it);
      if (ch_it != channels_.end())
      {
        Channel *channel = ch_it->second;
        if (channel->hasClient(client.getFd()))
        {
          channel->removeClient(client.getFd());
          client.leaveChannel(*it);
          std::string msg;
          if (hasMsg)
            msg = ":" + getPrefix(client) + " PART " + *it + " :" + leaveMsg + "\r\n";
          else
            msg = ":" + getPrefix(client) + " PART " + *it + "\r\n";
          queueMessageEverybodyInChannelElse(*channel, msg, client.getFd());
          queueMessage(client.getFd(), msg);
        }
        else // client not on channel
          notOnChannel(client, *it);
      }
      else //channel not found
        noSuchChannel(client, *it);
      it++;
    }
  }

  void handlePrivmsg(Client& client, const std::string params)
  {
    std::string noMsgError = ":" + getServerName() + " 412 " + client.getNickname() + " :No text to send\r\n";
    //parse
    size_t pos = params.find(' ');
    std::string left;
    std::string right;
    
    if (pos != std::string::npos)
    {
      left = params.substr(0, pos);
      if (pos + 1 < params.size() && params[pos + 1] == ':')
        right = params.substr(pos + 2);
      else
        right = params.substr(pos + 1);
      if (right.empty()) //no message
      {
        queueMessage(client.getFd(), noMsgError);
        return;
      }
    }
    else // no message
    {
      queueMessage(client.getFd(), noMsgError);
      return;
    }
    //

    if (Channel::isChannelFirstCharacter(left[0])) //message to channel
    {
      std::map<std::string, Channel *>::iterator ch_it = channels_.find(left);
      std::map<std::string, Channel *>::iterator ch_ite = channels_.end();
      if (ch_it == ch_ite) //channel not found
      {
        noSuchChannel(client, left);
        return;
      }
      Channel& channel = *(ch_it->second);
      std::string msg = ":" + client.getNickname() + "!" + client.getUsername() + "@" + client.getHostname() + " PRIVMSG " + left + " :" + right + "\r\n";
      queueMessageEverybodyInChannelElse(channel, msg, client.getFd());
    }
    else //message to a client
    {
      std::map<int, Client *>::iterator it = clients_.begin();
      std::map<int, Client *>::iterator ite = clients_.end();
      while (it != ite)
      {
        Client *target = it->second;
        if (target->getNickname() == left)
        {
          std::string msg = ":" + getPrefix(client) + " PRIVMSG " + left + " :" + right + "\r\n";
          queueMessage(target->getFd(), msg);
          return;
        }
        it++;
      }
      noSuchNick(client, left);
    }
  }

  void handleCommandMode(Client& client, const std::string& params) //複数未対応(+o bob +lなど)
  {
    //parse
    Channel *channel;
    
    std::vector<std::string> paramsV = str_split_to_vector(params, ' ');
    if (paramsV.size() == 0)
    {
      notEnoughParams(client, "MODE");
      return;
    }
    std::vector<std::string>::iterator it = paramsV.begin();
    std::vector<std::string>::iterator ite = paramsV.end();
    //channel check
    std::map<std::string, Channel*>::iterator ch_it = channels_.find(*it);
    if (ch_it != channels_.end())
      channel = ch_it->second;
    else
    {
      noSuchChannel(client, *it);
      return;
    }
    it++;

    if(!channel->hasClient(client.getFd())) //client not on channel
    {
      notOnChannel(client, channel->getName());
      return;
    }
    if (!channel->isOperator(client.getFd())) //not an operator
    {
      notChannelOperator(client, channel->getName());
        return;
    }

    //parse mode and param
    std::string mode;
    std::string param = "";
    std::string ch_name = channel->getName();
    bool hasParam = false;
    if (it != ite)
    {
      mode = *it;
      it++;
    }
    else
    {
      notEnoughParams(client, "MODE");
      return;
    }

    if (it != ite)
    {
      param = *it;
      hasParam = true;
    }

    if (mode == "+i")
    {
      channel->setInviteOnly();
    }
    else if (mode == "-i")
    {
      channel->unsetInviteOnly();
    }
    else if (mode == "+t")
    {
      channel->setTopicLocked();
    }
    else if (mode == "-t")
    {
      channel->unsetTopicLocked();
    }
    else if (mode == "+k")
    {
      if (!hasParam)
      {
        notEnoughParams(client, "MODE");
        return;
      }
      channel->setKey(param);
    }
    else if (mode == "-k")
    {
      channel->setKey("");
    }
    else if (mode == "+o")
    {
      if (!hasParam)
      {
        notEnoughParams(client, "MODE");
        return;
      }
      Client *target = &client;
      std::map<int, Client *>::iterator c_it = clients_.begin();
      std::map<int, Client *>::iterator c_ite = clients_.end();
      while (c_it != c_ite)
      {
        if (c_it->second->getNickname() == param)
        {
          target = c_it->second;
          break ;
        }
        c_it++;
      }
      if (target == &client) //nickname not found
      {
        noSuchNick(client, param);
        return;
      }
      channel->addOperator(target->getFd());
    }
    else if (mode == "-o")
    {
      if (!hasParam)
      {
        notEnoughParams(client, "MODE");
        return;
      }
      Client *target = &client;
      std::map<int, Client *>::iterator c_it = clients_.begin();
      std::map<int, Client *>::iterator c_ite = clients_.end();
      while (c_it != c_ite)
      {
        if (c_it->second->getNickname() == param)
        {
          target = c_it->second;
          break ;
        }
        c_it++;
      }
      if (target == &client) //nickname not found
      {
        noSuchNick(client, param);
        return;
      }
      channel->addOperator(target->getFd());
      channel->removeOperator(client.getFd());
    }
    else if (mode == "+l")
    {
      if (!hasParam)
      {
        notEnoughParams(client, "MODE");
        return;
      }
      size_t size = static_cast<size_t>(atoi(param.c_str()));
      channel->setUserLimit(size);
    }
    else if (mode == "-l")
    {
      channel->setUserLimit(0);
    }
    else //unknown mdoe
    {
      std::string msg = ":" + getServerName() + " 472 " + client.getNickname() + " " + mode[1] + " :is unknown mode char to me\r\n";
      queueMessage(client.getFd(), msg);
      return;
    }
    std::string msg;
    if (hasParam)
      msg = ":" + getPrefix(client) + " MODE " + ch_name + " " + mode + " " + param + "\r\n";
    else
      msg = ":" + getPrefix(client) + " MODE " + ch_name + " " + mode + "\r\n";
    queueMessageEverybodyInChannelElse(*channel, msg, -1);
  }

  void handleCommandInvite(Client& client, const std::string& params)
  {
    //parse
    std::vector<std::string> paramsV = str_split_to_vector(params, ' ');
    if (paramsV.size() < 2)
    {
      notEnoughParams(client, "INVITE");
      return;
    }
    const std::string targetName = paramsV[0];
    std::string ch_name = paramsV[1];
    //

    Client *target = &client;
    std::map<int, Client *>::iterator c_it = clients_.begin();
    std::map<int, Client *>::iterator c_ite = clients_.end();
    while (c_it != c_ite)
    {
      if (c_it->second->getNickname() == targetName)
      {
        target = c_it->second;
        break ;
      }
      c_it++;
    }
    if (target == &client) //nickname not found
    {
      noSuchNick(client, targetName);
      return;
    }

    Channel *channel;
    bool channelExist = true;
    std::map<std::string, Channel *>::iterator ch_it = channels_.find(ch_name);
    if (ch_it != channels_.end())
      channel = ch_it->second;
    else //channel not found
      channelExist = false;

    if(channelExist) //client not on channel
    {
      if (!channel->hasClient(client.getFd())) //not on channel
      {
        notOnChannel(client, channel->getName());
        return;
      }
      if (channel->isInviteOnly() && !channel->isOperator(client.getFd())) //not an operator
      {
        notChannelOperator(client, ch_name);
        return;
      }

      channel->addInviteFd(target->getFd());
    }

    std::string targetMsg = ":" + getPrefix(client) + " INVITE " + targetName + " " + ch_name + "\r\n";
    std::string clientMsg = ":" + getServerName() + " 342 " + client.getNickname() + " " + targetName + " " + ch_name + "\r\n";
    queueMessage(target->getFd(), targetMsg);
    queueMessage(client.getFd(), clientMsg);
  }

  void handleCommandTopic(Client& client, const std::string& params)
  {
    //parse
    std::vector<std::string> paramsV = str_split_to_vector(params, ' ');
    std::string ch_name;
    std::string topic = "";
    if (paramsV.size() < 1)
    {
      notEnoughParams(client, "TOPIC");
      return;
    }
    //

    Channel *channel;
    ch_name = paramsV[0];
    //error handling
    std::map<std::string, Channel *>::iterator it = channels_.find(ch_name);
    if (it == channels_.end()) // no such nhannel
    {
      noSuchChannel(client, ch_name);
      return;
    }
    else
      channel = it->second;

    if (!channel->hasClient(client.getFd())) //not on channel
    {
      notOnChannel(client, ch_name);
      return;
    }

    if (channel->topicLocked() && !channel->isOperator(client.getFd())) // not operator
    {
      notChannelOperator(client, ch_name);
      return;
    }
    //

    //success
    if (paramsV.size() == 1)
    {
      if (channel->getTopic() != "")
      {
        std::string msg = ":" + getServerName() + " 332 " + client.getNickname() + " " + ch_name + " :" + channel->getTopic() + "\r\n";
        queueMessage(client.getFd(), msg);
        return;
      }
      else
      {
        std::string msg = ":" + getServerName() + " 331 " + client.getNickname() + " " + ch_name + " :No topic is set\r\n";
        queueMessage(client.getFd(), msg);
        return;
      }
    }
    else if (paramsV.size() == 2)
    {
      std::string topic = paramsV[1];
      if (!topic.empty())
      {
        channel->setTopic(topic);
        std::string msg = ":" + getPrefix(client) + " TOPIC " + ch_name + " :" + topic + "\r\n";
        queueMessageEverybodyInChannelElse(*channel, msg, -1);
      }
      else
      {
        channel->setTopic("");
        std::string msg = ":" + getPrefix(client) + " TOPIC " + ch_name + " :\r\n";
        queueMessageEverybodyInChannelElse(*channel, msg, -1);
      }
    }
  }

  void handleCommandKick(Client& client, const std::string& params) 
  {
    //parse
    std::vector<std::string> paramsV = str_split_to_vector(params, ' ');
    if (!(paramsV.size() == 2 || paramsV.size() == 3)) //invalid size of parameter
    {
      notEnoughParams(client, "KICK");
      return;
    }

    std::vector<std::string> channels = str_split_to_vector(paramsV[0], ',');
    std::vector<std::string> eject_users = str_split_to_vector(paramsV[1], ',');

    if (!channels.size() || !eject_users.size() || !(channels.size() == 1 || channels.size() == eject_users.size())) //size not match
    {
      notEnoughParams(client, "KICK");
      return;
    }

    std::vector<std::string>::iterator ch_it = channels.begin();
    std::vector<std::string>::iterator username_it = eject_users.begin();
    std::vector<std::string>::iterator username_ite = eject_users.end();
    while (username_it != username_ite)
    {
      //get channel
      std::map<std::string, Channel *>::iterator it = channels_.find(*ch_it);
      Channel *channel;
      if (it != channels_.end())
        channel = it->second;
      else
      {
        noSuchChannel(client, *ch_it);
        if (channels.size() != 1)
          ch_it++;
        username_it++;
        continue;
      }

      if (!channel->hasClient(client.getFd())) // client not on channel
      {
        notOnChannel(client, *ch_it);
        if (channels.size() != 1)
          ch_it++;
        username_it++;
        continue ;
      }

      if (!channel->isOperator(client.getFd())) // is not operator
      {
        notChannelOperator(client, *ch_it);
        if (channels.size() != 1)
          ch_it++;
        username_it++;
        continue ;
      }

      //get target
      std::map<int, Client *>::iterator client_it = clients_.begin();
      std::map<int, Client *>::iterator client_ite = clients_.end();
      Client *target;
      while (client_it != client_ite)
      {
        if (client_it->second->getNickname() == *username_it)
        {
          target = client_it->second;
          break ;
        }
        client_it++;
      }
      if (client_it == client_ite)
      {
        noSuchNick(client, *username_it);
        if (channels.size() != 1)
          ch_it++;
        username_it++;
        continue;
      }
      if (target->getFd() == client.getFd())
      {
        std::string msg = ":" + getServerName() + " 482 " + client.getNickname() + " " + *ch_it + " :You can't kick yourself\r\n";
        queueMessage(client.getFd(), msg);
        if (channels.size() != 1)
          ch_it++;
        username_it++;
        continue;
      }

      if (!channel->hasClient(target->getFd())) //user not in channel
      {
        userNotInChannel(client, *ch_it, *username_it);
        if (channels.size() != 1)
          ch_it++;
        username_it++;
        continue;
      }

      std::string msg;
      if (paramsV.size() == 2) // no comment
        msg = ":" + getPrefix(client) + " KICK " + *ch_it + " " + *username_it + "\r\n";
      if (paramsV.size() == 3)
        msg = ":" + getPrefix(client) + " KICK " + *ch_it + " " + *username_it + " :" + paramsV[2] + "\r\n";
      queueMessageEverybodyInChannelElse(*channel, msg, -1);

      channel->removeClient(target->getFd());
      target->leaveChannel(*ch_it);
      if (channels.size() != 1)
          ch_it++;
      username_it++;
    }
  }

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
