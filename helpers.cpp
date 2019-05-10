
#include "Interactive.h"





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

    void preemptFunction() {
        std::cout << "Function intercepted\n";
        exit(0);
    }

    void interactive_preempt() {
        call_to_interactive_cycle();
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