#include "Interactive.h"

std::vector <std::string> split(std::string strToSplit, char delimeter) {
    std::stringstream ss(strToSplit);
    std::string item;
    std::vector<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter))
    {
        if (item.size() > 0)
            splittedStrings.push_back(item);
    }
    return splittedStrings;
}

static inline void lowerString(std::string& data) {
    std::transform(data.begin(), data.end(), data.begin(), ::tolower);
}

static inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch)
        {
            return !std::isspace(ch);
        }));
}

static inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch)
        {
            return !std::isspace(ch);
        }).base(), s.end());
}

static inline void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}

bool IsYes(const std::string& command) {
    return command == "y" || command == "yes";
}
bool IsNo(const std::string& command) {
    return !IsYes(command);
}

std::vector<std::string> ShowPromptAndGetInput(const std::string& prompt) {
    std::cout << prompt << " ";
    std::string command;
    std::getline(std::cin, command);
    trim(command);
    auto commands = split(command, ' ');
    if (commands.size() > 0)
        return commands;
    else return { "" };
}

std::string ShowPromptAndGetSingleInput(const std::string& prompt) {
    std::cout << prompt << " ";
    std::string command;
    std::getline(std::cin, command);
    trim(command);
    return command;
}