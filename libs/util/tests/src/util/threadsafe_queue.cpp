#include "util/threadsafe_queue.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
using namespace std::chrono_literals;

TEST(ThreadSafeQueueTest, WakesWaitingConsumer)
{
    Util::ThreadSafeQueue<int, 2> queue;
    auto consumer = std::async(std::launch::async, [&] { return queue.wait_pop(); });
    std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(queue.push(42));
    const auto ready = consumer.wait_for(500ms);
    queue.close(); // also guarantees cleanup if notification regresses
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_EQ(consumer.get(), 42);
}
TEST(ThreadSafeQueueTest, CloseWakesAndRejectsNewWork)
{
    Util::ThreadSafeQueue<int, 2> queue;
    auto consumer = std::async(std::launch::async, [&] { return queue.wait_pop(); });
    queue.close();
    EXPECT_FALSE(consumer.get());
    EXPECT_FALSE(queue.push(1));
}
TEST(ThreadSafeQueueTest, BoundedFifoSupportsWraparoundAndMoveOnlyValues)
{
    Util::ThreadSafeQueue<std::unique_ptr<int>, 2> queue;
    ASSERT_TRUE(queue.push(std::make_unique<int>(1)));
    ASSERT_TRUE(queue.push(std::make_unique<int>(2)));
    EXPECT_FALSE(queue.push(std::make_unique<int>(3)));
    EXPECT_EQ(**queue.try_pop(), 1);
    ASSERT_TRUE(queue.push(std::make_unique<int>(4)));
    EXPECT_EQ(**queue.try_pop(), 2);
    queue.close();
    EXPECT_EQ(**queue.wait_pop(), 4);
    EXPECT_FALSE(queue.wait_pop());
    EXPECT_EQ(queue.high_watermark(), 2U);
    EXPECT_EQ(queue.rejected(), 1U);
}
TEST(ThreadSafeQueueTest, ConcurrentProducersPreserveEveryAcceptedValue)
{
    Util::ThreadSafeQueue<int, 16> queue;
    auto consumer = std::async(std::launch::async, [&] {
        int count = 0, sum = 0;
        while (auto value = queue.wait_pop()) { ++count; sum += *value; }
        return std::pair{count, sum};
    });
    auto producer = [&](int value) { for (int i = 0; i < 1000; ++i) while (!queue.push(value)) std::this_thread::yield(); };
    std::thread a{producer, 1}, b{producer, 2};
    a.join(); b.join(); queue.close();
    const auto result = consumer.get();
    EXPECT_EQ(result.first, 2000); EXPECT_EQ(result.second, 3000);
}
