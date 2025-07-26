#include "Client.hpp"

Client::Client() : _fd(-1), _auth_level(0), _nickname(""), _username("") {}

Client::Client(int fd) : _fd(fd), _auth_level(0) {}

Client::~Client() {}

int Client::getFd() const {
    return _fd;
}

int Client::getAuthLevel() const {
  return _auth_level;
}

std::string Client::getNickname() const {
    return _nickname;
}

std::string Client::getUsername() const {
    return _username;
}

void Client::setAuthLevel(int auth) {
    _auth_level = auth;
}

void Client::setNickname(const std::string& nickname) {
    _nickname = nickname;
}

void Client::setUsername(const std::string& username) {
    _username = username;
}
