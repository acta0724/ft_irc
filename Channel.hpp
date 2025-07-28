#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <map>
#include <stdexcept>

// 前方宣言
class Client;

class Channel {
public:
    Channel(const std::string& name);
    ~Channel();

    // --- 基本情報 ---
    const std::string& getName() const;
    const std::string& getTopic() const;
    void setTopic(const std::string& topic);

    // --- クライアント管理 ---
    void addClient(Client* client);
    void removeClient(int client_fd);
    bool hasClient(int client_fd) const;
    const std::map<int, Client*>& getClients() const;
    std::vector<std::string> getNicknames() const;
    void broadcast(const std::string& message, int exclude_fd = -1);

    // --- オペレータ管理 ---
    void addOperator(int client_fd);
    void removeOperator(int client_fd);
    bool isOperator(int client_fd) const;

private:
    // A channel must have a name and should not be copied.
    // Therefore, the default constructor, copy constructor,
    // and assignment operator are declared private.
    Channel();
    Channel(const Channel& other);
    Channel& operator=(const Channel& other);

    std::string name_;
    std::string topic_;
    std::map<int, Client*> clients_;      // <fd, Client*>
    std::vector<int> operators_;          // オペレータのfdリスト
};

#endif // CHANNEL_HPP
