#pragma once
#include <string>
#include <vector>

std::vector<std::string> listImageFiles(const std::string& dir);
int64_t getFileModifiedTime(const std::string& path);
int64_t getFileSize(const std::string& path);
std::string readFile(const std::string& path);
std::string getParentDir(const std::string& path);
std::string getFilename(const std::string& path);
bool isAbsolute(const std::string& path);
std::string normalizePath(const std::string& path);