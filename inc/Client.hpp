#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <vector>

class Client
{
private:
    int         _fd;
    int        _auth_level;
    std::string _nickname;
    std::string _username;

public:
    Client(); // デフォルトコンストラクタを追加
    Client(int fd);
    ~Client();

    int         getFd() const;
    int         getAuthLevel() const;
    std::string getNickname() const;
    std::string getUsername() const;

    void setAuthLevel(int auth);
    void setNickname(const std::string& nickname);
    void setUsername(const std::string& username);
};

#endif
