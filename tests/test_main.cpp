#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

bool TestConfigResolvesRelativePathsAndClampIntervals();
bool TestConfigThrowsOnInvalidThreshold();
bool TestConfigThrowsWhenMissingAppNode();

bool TestQueuePushPopOrder();
bool TestQueueCloseReturnsNullopt();
bool TestQueueIgnoresPushAfterClose();

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"Config resolves relative paths and clamps intervals", TestConfigResolvesRelativePathsAndClampIntervals},
        {"Config throws on invalid threshold", TestConfigThrowsOnInvalidThreshold},
        {"Config throws when app node missing", TestConfigThrowsWhenMissingAppNode},
        {"Queue push/pop order", TestQueuePushPopOrder},
        {"Queue close returns nullopt", TestQueueCloseReturnsNullopt},
        {"Queue ignores push after close", TestQueueIgnoresPushAfterClose},
    };

    int passed = 0;
    for (const auto& [name, fn] : tests) {
        try {
            const bool ok = fn();
            if (ok) {
                ++passed;
                std::cout << "[PASS] " << name << std::endl;
            } else {
                std::cerr << "[FAIL] " << name << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << name << " (exception: " << e.what() << ")" << std::endl;
        } catch (...) {
            std::cerr << "[FAIL] " << name << " (unknown exception)" << std::endl;
        }
    }

    if (passed != static_cast<int>(tests.size())) {
        std::cerr << "\nSummary: " << passed << "/" << tests.size() << " passed" << std::endl;
        return 1;
    }

    std::cout << "\nSummary: " << passed << "/" << tests.size() << " passed" << std::endl;
    return 0;
}
