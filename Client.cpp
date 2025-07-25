#include "Client.hpp"
#include <algorithm>

Client::Client(int fd)
    : fd_(fd), registered_(false), authenticated_(false)
{
}

Client::~Client()
{
}

// ゲッター
int Client::getFd() const
{
    return fd_;
}

const std::string& Client::getNickname() const
{
    return nickname_;
}

const std::string& Client::getUsername() const
{
    return username_;
}

const std::string& Client::getRealname() const
{
    return realname_;
}

const std::string& Client::getHostname() const
{
    return hostname_;
}

const std::vector<std::string>& Client::getChannels() const
{
    return channels_;
}

bool Client::isRegistered() const
{
    return registered_;
}

bool Client::isAuthenticated() const
{
    return authenticated_;
}

// セッター
void Client::setFd(int fd)
{
    fd_ = fd;
}

void Client::setNickname(const std::string& nickname)
{
    nickname_ = nickname;
}

void Client::setUsername(const std::string& username)
{
    username_ = username;
}

void Client::setRealname(const std::string& realname)
{
    realname_ = realname;
}

void Client::setHostname(const std::string& hostname)
{
    hostname_ = hostname;
}

void Client::setRegistered(bool registered)
{
    registered_ = registered;
}

void Client::setAuthenticated(bool authenticated)
{
    authenticated_ = authenticated;
}

// チャンネル操作
void Client::joinChannel(const std::string& channel)
{
    if (!isInChannel(channel)) {
        channels_.push_back(channel);
    }
}

void Client::leaveChannel(const std::string& channel)
{
    std::vector<std::string>::iterator it = std::find(channels_.begin(), channels_.end(), channel);
    if (it != channels_.end()) {
        channels_.erase(it);
    }
}

bool Client::isInChannel(const std::string& channel) const
{
    return std::find(channels_.begin(), channels_.end(), channel) != channels_.end();
}