#include "utils.hpp"

std::vector<std::string> str_split_to_vector(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty())
            result.push_back(item);
    }
    return result;
}
