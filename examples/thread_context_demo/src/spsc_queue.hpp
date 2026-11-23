#pragma once

//-----------------------------------------------------------------------------------------------------
// Lock-free single producer single consumer (SPSC) queue, size given at runtime

/*
Exactly one thread may call Push() and exactly one thread may call Pop().
Pop() returns an empty std::optional when there is no new element.
Push() returns false when an element was dropped, which one depends on the size:

size > 1: FIFO ring buffer, a full queue drops the new element
  head_ and tail_ are free running 64 bit counters, the element index is counter % size_.
  The number of elements in the queue is head_ - tail_.

size == 1: latest value, a new element overwrites an unread one
  Triple buffer: the producer owns slot back_, the consumer owns slot front_, the third slot is in middle_.
  Both sides exchange their slot with middle_ in one atomic operation, flag kNew marks an unread element in middle_.
*/

#include <atomic>   // for std::atomic
#include <cstddef>  // for size_t
#include <cstdint>  // for uintxx_t
#include <optional> // for std::optional
#include <vector>   // for std::vector

template <typename T> class SpscQueue {

  private:
    size_t size_;           // Queue size in elements
    std::vector<T> buffer_; // size_ slots, 3 slots for size 1

    // size > 1
    std::atomic<uint64_t> head_{0}; // Next element to write, written by the producer only
    std::atomic<uint64_t> tail_{0}; // Next element to read, written by the consumer only

    // size == 1
    static constexpr uint8_t kNew = 0x80;
    std::atomic<uint8_t> middle_{1}; // Slot index | kNew
    uint8_t back_{0};                // Slot of the producer
    uint8_t front_{2};               // Slot of the consumer

  public:
    explicit SpscQueue(size_t size) : size_(size < 1 ? 1 : size), buffer_(size_ == 1 ? 3 : size_) {}

    // Producer thread only
    // Returns false when an element was dropped: the new one (size > 1, queue full) or the unread previous one (size == 1)
    bool Push(const T &element) {
        if (size_ == 1) {
            buffer_[back_] = element;
            uint8_t old = middle_.exchange(back_ | kNew, std::memory_order_acq_rel); // Publish the slot, take the middle one
            back_ = old & ~kNew;
            return (old & kNew) == 0;
        }
        uint64_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) == size_) {
            return false; // Full
        }
        buffer_[head % size_] = element;
        head_.store(head + 1, std::memory_order_release); // Publish the element to the consumer
        return true;
    }

    // Consumer thread only
    std::optional<T> Pop() {
        if (size_ == 1) {
            if ((middle_.load(std::memory_order_relaxed) & kNew) == 0) {
                return std::nullopt; // No new element, only the producer can change that and it keeps kNew set
            }
            uint8_t old = middle_.exchange(front_, std::memory_order_acq_rel); // Hand back the read slot, take the new one
            front_ = old & ~kNew;
            return buffer_[front_];
        }
        uint64_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt; // Empty
        }
        T element = buffer_[tail % size_];
        tail_.store(tail + 1, std::memory_order_release); // Hand the slot back to the producer
        return element;
    }

    // Discard all unread elements, consumer thread only
    // Safe while the producer pushes: an element pushed at the same time is either discarded or remains
    void Clear() {
        if (size_ == 1) {
            middle_.fetch_and(static_cast<uint8_t>(~kNew), std::memory_order_acq_rel); // Mark the middle slot as read
            return;
        }
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); // Hand all slots back to the producer
    }

    // Number of unread elements, producer or consumer thread only
    // A snapshot: the other thread may push or pop at the same time
    uint32_t Count() const {
        if (size_ == 1) {
            return (middle_.load(std::memory_order_relaxed) & kNew) ? 1 : 0;
        }
        return static_cast<uint32_t>(head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed));
    }
};
