#include "kernel/memory_pool.hpp"
#include <cstdint>
#include <limits>

namespace myrtos {

MemoryPool::MemoryPool(
    uint8_t* storage_buffer,
    size_t block_size,
    size_t block_count
) noexcept
    : buffer_(storage_buffer),
      block_size_(block_size < sizeof(FreeNode) ? sizeof(FreeNode) : block_size),
      block_count_(block_count),
      free_count_(block_count),
      free_head_(nullptr) {
    if (buffer_ == nullptr || block_count_ == 0 ||
        reinterpret_cast<uintptr_t>(buffer_) % alignof(FreeNode) != 0 ||
        block_size % alignof(FreeNode) != 0 || block_size < sizeof(FreeNode) ||
        block_count_ > std::numeric_limits<size_t>::max() / block_size_) {
        buffer_ = nullptr; free_count_ = block_count_ = 0; return;
    }

    // Chain all blocks into an embedded free list
    for (size_t i = 0; i < block_count_; ++i) {
        auto* current = reinterpret_cast<FreeNode*>(buffer_ + (i * block_size_));
        if (i + 1 < block_count_) {
            auto* next = reinterpret_cast<FreeNode*>(buffer_ + ((i + 1) * block_size_));
            current->next = next;
        } else {
            current->next = nullptr;
        }
    }
    free_head_ = reinterpret_cast<FreeNode*>(buffer_);
}

void* MemoryPool::allocate() noexcept {
    CriticalSectionGuard guard;

    if (free_head_ == nullptr || free_count_ == 0) {
        return nullptr;
    }

    FreeNode* allocated = free_head_;
    free_head_ = free_head_->next;
    --free_count_;

    return static_cast<void*>(allocated);
}

Status MemoryPool::deallocate(void* block_ptr) noexcept {
    if (block_ptr == nullptr || buffer_ == nullptr) {
        return Status::InvalidParam;
    }

    auto* byte_ptr = static_cast<uint8_t*>(block_ptr);
    const auto address = reinterpret_cast<uintptr_t>(byte_ptr);
    const auto base = reinterpret_cast<uintptr_t>(buffer_);
    if (address < base) return Status::InvalidParam;
    const auto offset = address - base;

    // Validate pointer lies within pool boundary and matches block alignment
    if (offset >= (block_count_ * block_size_) || (offset % block_size_) != 0) {
        return Status::InvalidParam;
    }

    CriticalSectionGuard guard;

    auto* node = static_cast<FreeNode*>(block_ptr);
    for (auto* free = free_head_; free != nullptr; free = free->next) {
        if (free == node) return Status::InvalidParam;
    }
    node->next = free_head_;
    free_head_ = node;
    ++free_count_;

    return Status::Ok;
}

} // namespace myrtos
