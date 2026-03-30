#include "logger.hpp"

#include <atomic>
#include <iostream>

namespace logger {

namespace {
std::atomic<bool> g_debug_enabled{false};
}

void SetDebugEnabled(bool enabled) {
    g_debug_enabled.store(enabled);
}

void Info(const std::string& message) {
    std::cout << message << std::endl;
}

void Warn(const std::string& message) {
    std::cerr << message << std::endl;
}

void Error(const std::string& message) {
    std::cerr << message << std::endl;
}

void Debug(const std::string& message) {
    if (g_debug_enabled.load()) {
        std::cout << message << std::endl;
    }
}

}  // namespace logger
