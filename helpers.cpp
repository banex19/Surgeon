#include <iostream>
#include <algorithm>
#include <string> 
#include <vector>
#include <sstream>
#include <cstdlib>

std::vector<std::string> split(std::string strToSplit, char delimeter) {
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

// trim from start (in place)
static inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch)
        {
            return !std::isspace(ch);
        }));
}

// trim from end (in place)
static inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch)
        {
            return !std::isspace(ch);
        }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}


std::vector<std::string> ShowPromptAndGetInput(const std::string& prompt) {
    std::cout << prompt << " ";
    std::string command;
    std::getline(std::cin, command);
    lowerString(command);
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
    lowerString(command);
    trim(command);
    return command;
}

extern "C" {

    int saveCheckpoint();
    int restoreCheckpoint();

#ifndef WIN32
    __attribute__((weak)) 
#endif
        void interactive_fake_call() {
        return;
    }

#ifndef WIN32
    __attribute__((weak))
#endif
        void call_to_interactive_cycle() {}

    void interactive_cycle();
 

    void preemptFunction() {
        std::cout << "Function intercepted\n";
        exit(0);
    }

    void interactive_preempt() {
        call_to_interactive_cycle();
    }


    bool IsYes(const std::string& command) {
        return command == "y" || command == "yes";
    }

    bool IsNo(const std::string& command) {
        return !IsYes(command);
    }


    void interactive_cycle() {
        volatile bool checkpointEnabled = false;

        std::cout << "Entering performance engineering mode...\n";
        while (true)
        {
            size_t runN = 1;
            bool notRecognized = false;
            auto command = ShowPromptAndGetInput("(surgeon)");
            auto singleCmd = command[0];

            if (singleCmd == "exit" || singleCmd == "quit")
            {
                singleCmd = ShowPromptAndGetSingleInput("Are you sure you want to exit?");
                if (IsYes(singleCmd))
                    exit(0);
            }
            else if (singleCmd == "run" || singleCmd == "r")
            {
                if (command.size() > 1) {
                    runN = std::atoi(command[1].c_str());
                    if (runN == 0) {
                        notRecognized = true;
                    }
                }
                for (size_t i = 0; i < runN; ++i) {
                    if (checkpointEnabled)
                    {
                        saveCheckpoint();
                        interactive_fake_call();
                        restoreCheckpoint();
                    }
                    else
                    {
                        interactive_fake_call();
                    }
                }
            }
            else if (singleCmd == "continue" || singleCmd == "c")
            {
                break;
            }
            else if (singleCmd == "enable" || singleCmd == "e")
            {
                if (command.size() > 1) {
                    auto secondCmd = command[1];
                    if (secondCmd == "checkpoint" || secondCmd == "c")
                    {
                        checkpointEnabled = true;
                        std::cout << "Checkpoint enabled\n";
                    }
                    else notRecognized = true;
                }
                else {
                    notRecognized = true;
                }
               
            }
            else if (singleCmd == "disable" || singleCmd == "d")
            {
                if (command.size() > 1) {
                    auto secondCmd = command[1];
                    if (secondCmd == "checkpoint" || secondCmd == "c")
                    {
                        checkpointEnabled = false;
                        std::cout << "Checkpoint disabled\n";
                    }
                    else notRecognized = true;
                }
                else {
                    notRecognized = true;
                }
            }
            else
            {
                notRecognized = true;
            }

            if (notRecognized)
            {
                std::cout << "Command not recognized\n";
            }
        }
        std::cout << "Continuing...\n";

    }
}