#include "Client.hpp"

Client::Client(int fd) : _fd(fd), _authenticated(false) {}

Client::~Client() {}

int Client::getFd() const {
    return _fd;
}

bool Client::isAuthenticated() const {
    return _authenticated;
}

std::string Client::getNickname() const {
    return _nickname;
}

std::string Client::getUsername() const {
    return _username;
}

void Client::setAuthenticated(bool auth) {
    _authenticated = auth;
}

void Client::setNickname(const std::string& nickname) {
    _nickname = nickname;
}

void Client::setUsername(const std::string& username) {
    _username = username;
}
