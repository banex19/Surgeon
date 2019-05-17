#pragma once 
#include <iostream>
#include <algorithm>
#include <string> 
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cctype>


std::vector <std::string> split(std::string strToSplit, char delimeter, bool trimEach = false);
void lowerString(std::string& data);
void ltrim(std::string& s);
void rtrim(std::string& s);
void trim(std::string& s);

std::vector<std::string> ShowPromptAndGetInput(const std::string& prompt);
std::string ShowPromptAndGetSingleInput(const std::string& prompt);

bool IsYes(const std::string& command);
bool IsNo(const std::string& command);