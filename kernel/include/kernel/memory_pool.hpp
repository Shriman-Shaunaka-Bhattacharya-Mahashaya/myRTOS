#pragma once

#include "kernel/types.hpp"
#include "port/port.hpp"

namespace myrtos {

/**
 * @brief O(1) Fixed-Size Block Memory Pool Allocator.
 * 
 * Fixed storage; allocation is constant time and validated release scans free blocks.
 */
class MemoryPool {
public:
    MemoryPool(
        uint8_t* storage_buffer,
        size_t block_size,
        size_t block_count
    ) noexcept;

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /// Allocates one fixed-size block in O(1) time. Returns nullptr if exhausted.
    void* allocate() noexcept;

    /// Validates and returns a block; O(number of free blocks).
    Status deallocate(void* block_ptr) noexcept;

    [[nodiscard]] size_t block_size() const noexcept { CriticalSectionGuard guard; return block_size_; }
    [[nodiscard]] size_t total_blocks() const noexcept { CriticalSectionGuard guard; return block_count_; }
    [[nodiscard]] size_t free_blocks() const noexcept { CriticalSectionGuard guard; return free_count_; }
    [[nodiscard]] size_t allocated_blocks() const noexcept { CriticalSectionGuard guard; return block_count_ - free_count_; }

private:
    struct FreeNode {
        FreeNode* next{nullptr};
    };

    uint8_t* buffer_{nullptr};
    size_t block_size_{0};
    size_t block_count_{0};
    size_t free_count_{0};
    FreeNode* free_head_{nullptr};
};

/**
 * @brief Statically allocated typed Memory Pool.
 */
template <typename T, size_t BlockCount>
class StaticMemoryPool {
    static_assert(BlockCount > 0);
public:
    StaticMemoryPool() noexcept
        : pool_(reinterpret_cast<uint8_t*>(storage_), stride, BlockCount) {}

    T* allocate() noexcept {
        return reinterpret_cast<T*>(pool_.allocate());
    }

    Status deallocate(T* ptr) noexcept {
        return pool_.deallocate(ptr);
    }

    [[nodiscard]] size_t free_count() const noexcept { CriticalSectionGuard guard; return pool_.free_blocks(); }
    [[nodiscard]] size_t total_count() const noexcept { CriticalSectionGuard guard; return BlockCount; }

private:
    static constexpr size_t alignment = alignof(T) > alignof(void*) ? alignof(T) : alignof(void*);
    static constexpr size_t stride = ((sizeof(T) + alignment - 1) / alignment) * alignment;
    alignas(alignof(T) > alignof(void*) ? alignof(T) : alignof(void*))
    uint8_t storage_[stride * BlockCount];
    MemoryPool pool_;
};

} // namespace myrtos
