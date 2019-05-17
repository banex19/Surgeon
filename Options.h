#pragma once
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <string>
#include "Interactive.h"

class OptionsStore {
private:
    static std::unordered_map<std::string, std::string> options;

public:
    static std::string GetOption(const std::string& optName);
    static std::string GetOptionOrError(const std::string& optName);

    static void LoadOptions(const std::string& filename);
};