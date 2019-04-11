#include <iostream>
#include <algorithm>
#include <string> 

static inline void lowerString(std::string& data) {
    std::transform(data.begin(), data.end(), data.begin(), ::tolower);
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [] (int ch)
    {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [] (int ch)
    {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}


std::string ShowPromptAndGetInput(const std::string& prompt) {
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

    __attribute__((weak)) void interactive_fake_call() {
        return;
    }

    void interactive_cycle();
    __attribute__((weak))  void call_to_interactive_cycle() {}

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
        std::string command;
        while (true)
        {
            command = ShowPromptAndGetInput("(surgeon)");

            if (command == "exit" || command == "quit")
            {
                command = ShowPromptAndGetInput("Are you sure you want to exit?");
                if (IsYes(command))
                    exit(0);
            }
            else if (command == "run" || command == "r")
            {
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
            else if (command == "continue" || command == "c")
            {
                break;
            }
            else if (command == "enable checkpoint")
            {
                checkpointEnabled = true;
                std::cout << "Checkpoint enabled\n";
            }
            else if (command == "disable checkpoint")
            {
                checkpointEnabled = false;
                std::cout << "Checkpoint disabled\n";
            }
            else
            {
                std::cout << "Command not recognized\n";
            }
        }
        std::cout << "Continuing...\n";

    }
}