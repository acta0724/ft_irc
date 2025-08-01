#include "utils.hpp"

std::vector<std::string> str_split_to_vector(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    std::string item2;
    while (std::getline(ss, item, delimiter))
    {
        if (!item.empty())
        {
            if (item[0] == ':')
            {
                while (std::getline(ss, item2))
                    item.append(item2);
                item.erase(item[0]);
                result.push_back(item);
                break;
            }
            else
                result.push_back(item);
        }
    }
    return result;
}