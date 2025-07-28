#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>

class Client {
public:
    Client();
    Client(int fd);
    ~Client();

    // ゲッター
    int getFd() const;
    const std::string& getNickname() const;
    const std::string& getUsername() const;
    const std::string& getRealname() const;
    const std::string& getHostname() const;
    const std::vector<std::string>& getChannels() const;
    bool isRegistered() const;
    bool isAuthenticated() const;

    // セッター
    void setFd(int fd);
    void setNickname(const std::string& nickname);
    void setUsername(const std::string& username);
    void setRealname(const std::string& realname);
    void setHostname(const std::string& hostname);
    void setRegistered(bool registered);
    void setAuthenticated(bool authenticated);

    // チャンネル操作
    void joinChannel(const std::string& channel);
    void leaveChannel(const std::string& channel);
    bool isInChannel(const std::string& channel) const;

private:
    Client(const Client& other);
    Client& operator=(const Client& other);

    int fd_;                        // クライアントのソケットディスクリプタ
    std::string nickname_;          // ニックネーム
    std::string username_;          // ユーザー名
    std::string realname_;          // 本名
    std::string hostname_;          // ホスト名
    std::vector<std::string> channels_; // 参加しているチャンネル
    bool registered_;               // 登録済みかどうか
    bool authenticated_;            // パスワード認証済みかどうか
};

#endif // CLIENT_HPP
