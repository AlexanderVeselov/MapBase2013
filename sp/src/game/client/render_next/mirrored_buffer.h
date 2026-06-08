#pragma once

#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

template<typename T>
class MirroredBuffer
{
public:
    struct Slice
    {
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    explicit MirroredBuffer(gpu::BufferFlags flags = gpu::BufferFlags::kNone)
        : flags_(flags)
    {
    }

    MirroredBuffer(MirroredBuffer const&) = delete;
    MirroredBuffer& operator=(MirroredBuffer const&) = delete;
    MirroredBuffer(MirroredBuffer&&) = default;
    MirroredBuffer& operator=(MirroredBuffer&&) = default;

    Slice Append(std::vector<T> const& data)
    {
        return Append(data.begin(), data.end());
    }

    template<typename TIterator>
    Slice Append(TIterator first, TIterator last)
    {
        Slice slice = {static_cast<uint32_t>(cpu_data_.size()), static_cast<uint32_t>(std::distance(first, last))};
        if (slice.count == 0)
        {
            return slice;
        }

        cpu_data_.insert(cpu_data_.end(), first, last);
        MarkDirty(slice.offset, slice.count);
        return slice;
    }

    Slice Append(T const& value)
    {
        Slice slice = {static_cast<uint32_t>(cpu_data_.size()), 1};
        cpu_data_.push_back(value);
        MarkDirty(slice.offset, slice.count);
        return slice;
    }

    void Clear()
    {
        cpu_data_.clear();
        dirty_ = true;
        dirty_offset_ = 0;
        dirty_count_ = 0;
    }

    void Reset()
    {
        cpu_data_.clear();
        gpu_buffer_.reset();
        staging_buffer_.reset();
        ClearDirty();
    }

    void Sync(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer)
    {
        uint32_t element_count = cpu_data_.empty() ? 1u : static_cast<uint32_t>(cpu_data_.size());
        uint64_t required_size = static_cast<uint64_t>(sizeof(T)) * element_count;

        if (!gpu_buffer_)
        {
            gpu_buffer_ = device->CreateBuffer(required_size, sizeof(T), flags_);
            UploadWholeBuffer(device, cmd_buffer, required_size);
            return;
        }

        if (gpu_buffer_->GetSize() < required_size)
        {
            gpu_buffer_->Resize(required_size);
            UploadWholeBuffer(device, cmd_buffer, required_size);
            return;
        }

        if (!dirty_)
        {
            return;
        }

        if (cpu_data_.empty())
        {
            UploadBytes(device, cmd_buffer, 0, nullptr, sizeof(T));
        }
        else if (dirty_count_ > 0)
        {
            UploadBytes(device, cmd_buffer, static_cast<uint64_t>(dirty_offset_) * sizeof(T),
                cpu_data_.data() + dirty_offset_, static_cast<uint64_t>(dirty_count_) * sizeof(T));
        }
        ClearDirty();
    }

    bool Empty() const
    {
        return cpu_data_.empty();
    }

    uint32_t Size() const
    {
        return static_cast<uint32_t>(cpu_data_.size());
    }

    T const& operator[](size_t index) const
    {
        return cpu_data_[index];
    }

    std::vector<T> ToVector() const
    {
        return cpu_data_;
    }

    gpu::BufferPtr const& GpuBuffer() const
    {
        return gpu_buffer_;
    }

private:
    void UploadWholeBuffer(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer, uint64_t required_size)
    {
        if (cpu_data_.empty())
        {
            UploadBytes(device, cmd_buffer, 0, nullptr, sizeof(T));
        }
        else
        {
            UploadBytes(device, cmd_buffer, 0, cpu_data_.data(), required_size);
        }
        ClearDirty();
    }

    void UploadBytes(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer, uint64_t dst_offset, T const* src_data,
        uint64_t size_bytes)
    {
        EnsureStagingBuffer(device, size_bytes);

        void* mapped_data = staging_buffer_->Map();
        if (src_data)
        {
            std::memcpy(mapped_data, src_data, static_cast<size_t>(size_bytes));
        }
        else
        {
            std::memset(mapped_data, 0, static_cast<size_t>(size_bytes));
        }
        staging_buffer_->Unmap();
        cmd_buffer.CopyBuffer(staging_buffer_, 0, gpu_buffer_, dst_offset, size_bytes);
    }

    void EnsureStagingBuffer(gpu::DevicePtr const& device, uint64_t required_size)
    {
        if (!staging_buffer_)
        {
            staging_buffer_ = device->CreateBuffer(required_size, sizeof(T), gpu::BufferFlags::kCpuAccess);
            return;
        }

        if (staging_buffer_->GetSize() < required_size)
        {
            staging_buffer_->Resize(required_size);
        }
    }

    void MarkDirty(uint32_t offset, uint32_t count)
    {
        if (count == 0)
        {
            return;
        }

        if (!dirty_)
        {
            dirty_ = true;
            dirty_offset_ = offset;
            dirty_count_ = count;
            return;
        }

        uint32_t dirty_end = (std::max)(dirty_offset_ + dirty_count_, offset + count);
        dirty_offset_ = (std::min)(dirty_offset_, offset);
        dirty_count_ = dirty_end - dirty_offset_;
    }

    void ClearDirty()
    {
        dirty_ = false;
        dirty_offset_ = 0;
        dirty_count_ = 0;
    }

private:
    gpu::BufferFlags flags_;
    std::vector<T> cpu_data_;
    gpu::BufferPtr gpu_buffer_;
    gpu::BufferPtr staging_buffer_;
    bool dirty_ = false;
    uint32_t dirty_offset_ = 0;
    uint32_t dirty_count_ = 0;
};
