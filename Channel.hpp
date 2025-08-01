#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdexcept>

#define MODE_INVITE_ONLY  (1 << 0) // +i
#define MODE_TOPIC_LOCKED (1 << 1) // +t
#define MODE_KEY_REQUIRED (1 << 2) // +k
#define MODE_LIMIT_SET    (1 << 3) // +l

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

    // --- Get/Set MODE
    bool isInviteOnly() const;
    void setInviteOnly();
    void unsetInviteOnly();
    bool topicLocked() const;
    void setTopicLocked();
    void unsetTopicLocked();
    const std::string& getKey() const;
    void setKey(const std::string& key);
    size_t getUserLimit();
    void setUserLimit(size_t size);

    //INVITE
    void addInviteFd(int fd);
    void removeInvitedFd(int fd);
    bool isInvited(const Client& client);

    // --- Utils ---
    static bool isChannelFirstCharacter(char c);
    bool userFull() const;

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
    std::set<int> invitedFds_;         // list of invited fd
    std::vector<int> operators_;          // オペレータのfdリスト

    // --- MODE ---
    int mode_; //mode bitmap
    std::string key_; //"" when not set
    size_t user_limit_; //0 when unlimited
};

#endif // CHANNEL_HPP
