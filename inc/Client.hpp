#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <vector>

class Client
{
private:
    int         _fd;
    bool        _authenticated;
    std::string _nickname;
    std::string _username;

public:
    Client(); // デフォルトコンストラクタを追加
    Client(int fd);
    ~Client();

    int         getFd() const;
    bool        isAuthenticated() const;
    std::string getNickname() const;
    std::string getUsername() const;

    void setAuthenticated(bool auth);
    void setNickname(const std::string& nickname);
    void setUsername(const std::string& username);
};

#endif
