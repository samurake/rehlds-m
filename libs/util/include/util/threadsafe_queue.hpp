/*
 * Half Life 1 SDK License
 * Copyright(c) Valve Corp
 *
 * DISCLAIMER OF WARRANTIES. THE HALF LIFE 1 SDK AND ANY OTHER MATERIAL
 * DOWNLOADED BY LICENSEE IS PROVIDED "AS IS". VALVE AND ITS SUPPLIERS
 * DISCLAIM ALL WARRANTIES WITH RESPECT TO THE SDK, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NON-INFRINGEMENT, TITLE AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * LIMITATION OF LIABILITY. IN NO EVENT SHALL VALVE OR ITS SUPPLIERS BE LIABLE
 * FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER
 * (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
 * BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY
 * LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THE ENGINE AND/OR THE
 * SDK, EVEN IF VALVE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * For commercial use, contact: sourceengine@valvesoftware.com
 */

#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace Util {
    // Fixed storage, multiple producers/consumers. Close wakes every waiter;
    // pending items may be drained. Destruction requires joined consumers.
    template <typename T, std::size_t Capacity = 128>
    class ThreadSafeQueue final {
        static_assert(Capacity > 0);
      public:
        ThreadSafeQueue() = default;
        ThreadSafeQueue(const ThreadSafeQueue&) = delete;
        ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

        [[nodiscard]] bool push(T item)
        {
            {
                const std::scoped_lock lock{mutex_};
                if (closed_ || size_ == Capacity) {
                    ++rejected_;
                    return false;
                }
                slots_[(head_ + size_) % Capacity].emplace(std::move(item));
                ++size_;
                if (size_ > high_water_) high_water_ = size_;
            }
            available_.notify_one();
            return true;
        }

        [[nodiscard]] std::optional<T> try_pop()
        {
            const std::scoped_lock lock{mutex_};
            return take_locked();
        }

        [[nodiscard]] std::optional<T> wait_pop()
        {
            std::unique_lock lock{mutex_};
            available_.wait(lock, [this] { return closed_ || size_ != 0; });
            return take_locked();
        }

        void close()
        {
            {
                const std::scoped_lock lock{mutex_};
                closed_ = true;
            }
            available_.notify_all();
        }

        [[nodiscard]] std::size_t size() const { const std::scoped_lock lock{mutex_}; return size_; }
        [[nodiscard]] std::size_t high_watermark() const { const std::scoped_lock lock{mutex_}; return high_water_; }
        [[nodiscard]] std::size_t rejected() const { const std::scoped_lock lock{mutex_}; return rejected_; }
        [[nodiscard]] bool empty() const { return size() == 0; }
        [[nodiscard]] bool closed() const { const std::scoped_lock lock{mutex_}; return closed_; }

      private:
        std::optional<T> take_locked()
        {
            if (size_ == 0) return std::nullopt;
            auto item = std::move(slots_[head_]);
            slots_[head_].reset();
            head_ = (head_ + 1) % Capacity;
            --size_;
            return item;
        }
        mutable std::mutex mutex_{};
        std::condition_variable available_{};
        std::array<std::optional<T>, Capacity> slots_{};
        std::size_t head_{}, size_{}, high_water_{}, rejected_{};
        bool closed_{};
    };
}
