#include "Channel.hpp"
#include "Client.hpp" // Clientのメソッドを使うためインクルード
#include <algorithm>

Channel::Channel(const std::string& name) : name_(name), key_("")
{
    if (name.empty())
        throw std::invalid_argument("Channel name is empty");

    if (!isChannelFirstCharacter(name[0]))
        throw std::invalid_argument("Channel name begin with invalid character: " + name);
    
    if (name.size() > 50)
        throw std::invalid_argument("Channel name is too long (up to 50): " + name);
    
    std::string::const_iterator it = name.begin();
    std::string::const_iterator ite = name.end();
    while (it != ite)
    {
        char c = *it;
        if (c == ' '
            || c == static_cast<char>(7) //control G (^G or ASCII 7)
            || c == ','
            || c == ':')
            throw std::invalid_argument("Channel name contains invalid character: " + name);
        it++;
    }
}

Channel::~Channel() {}

const std::string& Channel::getName() const {
    return name_;
}

const std::string& Channel::getKey() const {
    return key_;
}

const std::string& Channel::getTopic() const {
    return topic_;
}

void Channel::setKey(const std::string& key) {
    key_ = key;
}

void Channel::setTopic(const std::string& topic) {
    topic_ = topic;
}

void Channel::addClient(Client* client) {
    // Add the client if they are not null and not already in the channel.
    if (client && clients_.find(client->getFd()) == clients_.end()) {
        clients_[client->getFd()] = client;
        // The first user to join the channel automatically becomes an operator.
        if (clients_.size() == 1) {
            operators_.push_back(client->getFd());
        }
    }
}

void Channel::removeClient(int client_fd) {
    clients_.erase(client_fd);
    // もしオペレータだったら、オペレータリストからも削除
    removeOperator(client_fd);
}

bool Channel::hasClient(int client_fd) const {
    return clients_.count(client_fd) > 0;
}

const std::map<int, Client*>& Channel::getClients() const {
    return clients_;
}

std::vector<std::string> Channel::getNicknames() const {
    std::vector<std::string> nicknames;
    for (std::map<int, Client*>::const_iterator it = clients_.begin(); it != clients_.end(); ++it) {
        nicknames.push_back(it->second->getNickname());
    }
    return nicknames;
}

/**
 * @brief チャンネル内のクライアントにメッセージをブロードキャストします。
 * @note この関数自体はメッセージを送信しません。Serverクラスがこの関数から得たクライアントリストを元に送信処理を行います。
 *       より良い設計は、Serverクラスにbroadcastメソッドを持たせることです。
 */
void Channel::broadcast(const std::string& message, int exclude_fd) {
    // この関数はServerクラスの`queueMessage`を知らないため、
    // 実際の送信はServerクラス側で行う必要があります。
    // Server側で getClients() を使ってループを回し、メッセージをキューに入れるのが良い設計です。
    (void)message;
    (void)exclude_fd;
}

void Channel::addOperator(int client_fd) {
    // A client can only become an operator if they are in the channel and not already an operator.
    if (hasClient(client_fd) && !isOperator(client_fd)) {
        operators_.push_back(client_fd);
    }
}

void Channel::removeOperator(int client_fd) {
    std::vector<int>::iterator it = std::find(operators_.begin(), operators_.end(), client_fd);
    if (it != operators_.end()) {
        operators_.erase(it);
    }
}

bool Channel::isOperator(int client_fd) const {
    return std::find(operators_.begin(), operators_.end(), client_fd) != operators_.end();
}

bool Channel::isChannelFirstCharacter(char c)
{
    if (!(c == '&'
        || c == '#'
        || c == '+'
        || c == '!'))
        return (false);
    return (true);
}
