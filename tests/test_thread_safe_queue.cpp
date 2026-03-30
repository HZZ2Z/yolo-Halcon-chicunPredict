#include "thread_safe_queue.hpp"

bool TestQueuePushPopOrder() {
    ThreadSafeQueue<int> queue(4);
    queue.push(10);
    queue.push(20);
    queue.push(30);

    const auto a = queue.pop();
    const auto b = queue.pop();
    const auto c = queue.pop();

    return a.has_value() && b.has_value() && c.has_value() &&
           *a == 10 && *b == 20 && *c == 30;
}

bool TestQueueCloseReturnsNullopt() {
    ThreadSafeQueue<int> queue(2);
    queue.close();

    const auto value = queue.pop();
    return !value.has_value();
}

bool TestQueueIgnoresPushAfterClose() {
    ThreadSafeQueue<int> queue(2);
    queue.close();
    queue.push(42);

    const auto value = queue.pop();
    return !value.has_value();
}
