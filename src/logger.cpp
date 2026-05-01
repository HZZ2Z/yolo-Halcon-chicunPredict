#include "logger.hpp"

#include <iostream>

namespace logger {

void Info(const std::string& message) {
    std::cout << message << std::endl;
}

void Warn(const std::string& message) {
    std::cerr << message << std::endl;
}

void Error(const std::string& message) {
    std::cerr << message << std::endl;
}

}  // namespace logger
