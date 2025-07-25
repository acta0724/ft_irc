#ifndef COMMAND_HPP
# define COMMAND_HPP

# include <string>
# include <vector>

struct Command {
    std::string prefix;
    std::string command;
    std::vector<std::string> parameters;

    Command() : prefix(""), command("") {}
};

#endif