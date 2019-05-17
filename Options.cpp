#include "Options.h"

std::unordered_map<std::string, std::string> OptionsStore::options;

std::string OptionsStore::GetOption(const std::string& optName) {
    if (options.find(optName) != options.end())
        return options[optName];
    else return "";
}

std::string OptionsStore::GetOptionOrError(const std::string& optName) {
    if (options.find(optName) != options.end())
        return options[optName];
    else
    {
        std::cout << "Error: required option " << optName << " is not defined\n";
        exit(-1);
    }
}

void OptionsStore::LoadOptions(const std::string& filename) {
    std::ifstream file{ filename };

    if (!file) {
        std::cout << "Error loading configuration file " << filename << "\n";
        exit(-1);
    }

    std::string line;
    while (std::getline(file, line)) {
        ltrim(line);
        if (line.size() == 0 || line[0] == '#')
            continue;

        auto tokens = split(line, '=');
        if (tokens.size() != 2) {
            std::cout << "Configuration file malformed\n";
            exit(-1);
        }

        auto key = tokens[0];
        trim(key);
        auto val = tokens[1];
        trim(val);

        options[key] = val;
    }
}
