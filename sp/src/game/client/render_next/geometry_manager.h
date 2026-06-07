#pragma once

#include "gpu_buffer.hpp"
#include "gpu_device.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

template<typename T>
class LinearArena
{
public:
    struct Slice
    {
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    Slice Append(std::vector<T> const& data)
    {
        Slice slice = {static_cast<uint32_t>(data_.size()), static_cast<uint32_t>(data.size())};
        if (data.empty())
        {
            return slice;
        }

        data_.insert(data_.end(), data.begin(), data.end());
        MarkDirty(slice.offset, slice.count);
        return slice;
    }

    template<typename TIterator>
    Slice Append(TIterator first, TIterator last)
    {
        Slice slice = {static_cast<uint32_t>(data_.size()), static_cast<uint32_t>(std::distance(first, last))};
        if (slice.count == 0)
        {
            return slice;
        }

        data_.insert(data_.end(), first, last);
        MarkDirty(slice.offset, slice.count);
        return slice;
    }

    void Clear()
    {
        data_.clear();
        dirty_ = true;
        dirty_offset_ = 0;
        dirty_count_ = 0;
    }

    std::vector<T> const& Data() const
    {
        return data_;
    }

    uint32_t Size() const
    {
        return static_cast<uint32_t>(data_.size());
    }

    bool Dirty() const
    {
        return dirty_;
    }

    uint32_t DirtyOffset() const
    {
        return dirty_offset_;
    }

    uint32_t DirtyCount() const
    {
        return dirty_count_;
    }

    void ClearDirty()
    {
        dirty_ = false;
        dirty_offset_ = 0;
        dirty_count_ = 0;
    }

private:
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

private:
    std::vector<T> data_;
    bool dirty_ = false;
    uint32_t dirty_offset_ = 0;
    uint32_t dirty_count_ = 0;
};

struct GeometrySlice
{
    uint32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
};

template<typename TVertex, typename TIndex>
class GeometryManager
{
public:
    GeometrySlice Append(std::vector<TVertex> const& vertices, std::vector<TIndex> const& indices)
    {
        typename LinearArena<TVertex>::Slice vertex_slice = vertices_.Append(vertices);
        typename LinearArena<TIndex>::Slice index_slice = indices_.Append(indices);
        return {vertex_slice.offset, vertex_slice.count, index_slice.offset, index_slice.count};
    }

    template<typename TVertexIterator, typename TIndexIterator>
    GeometrySlice Append(TVertexIterator vertex_first, TVertexIterator vertex_last, TIndexIterator index_first, TIndexIterator index_last)
    {
        typename LinearArena<TVertex>::Slice vertex_slice = vertices_.Append(vertex_first, vertex_last);
        typename LinearArena<TIndex>::Slice index_slice = indices_.Append(index_first, index_last);
        return {vertex_slice.offset, vertex_slice.count, index_slice.offset, index_slice.count};
    }

    void Clear()
    {
        vertices_.Clear();
        indices_.Clear();
    }

    void SyncToGpu(gpu::DevicePtr const& device)
    {
        SyncArenaToGpu(device, vertices_, vertex_buffer_, sizeof(TVertex),
            gpu::BufferFlags::kCpuAccess, vertex_count_);
        SyncArenaToGpu(device, indices_, index_buffer_, sizeof(TIndex),
            gpu::BufferFlags::kCpuAccess, index_count_);
    }

    std::vector<TVertex> const& VertexData() const
    {
        return vertices_.Data();
    }

    std::vector<TIndex> const& IndexData() const
    {
        return indices_.Data();
    }

    uint32_t VertexCount() const
    {
        return vertices_.Size();
    }

    uint32_t IndexCount() const
    {
        return indices_.Size();
    }

    gpu::BufferPtr const& VertexBuffer() const
    {
        return vertex_buffer_;
    }

    gpu::BufferPtr const& IndexBuffer() const
    {
        return index_buffer_;
    }

private:
    template<typename TData>
    static void SyncArenaToGpu(gpu::DevicePtr const& device, LinearArena<TData>& arena, gpu::BufferPtr& io_buffer,
        uint32_t stride, gpu::BufferFlags flags, uint32_t& out_count)
    {
        out_count = arena.Size();
        uint32_t element_count = arena.Size() > 0 ? arena.Size() : 1u;
        uint64_t required_size = static_cast<uint64_t>(stride) * element_count;

        if (!io_buffer)
        {
            io_buffer = device->CreateBuffer(required_size, stride, flags);
            arena.ClearDirty();
            void* mapped_data = io_buffer->Map();
            if (arena.Size() > 0)
            {
                std::memcpy(mapped_data, arena.Data().data(), static_cast<size_t>(stride) * arena.Size());
            }
            else
            {
                std::memset(mapped_data, 0, stride);
            }
            io_buffer->Unmap();
            return;
        }

        if (io_buffer->GetSize() < required_size)
        {
            io_buffer->Resize(required_size);
            void* mapped_data = io_buffer->Map();
            if (arena.Size() > 0)
            {
                std::memcpy(mapped_data, arena.Data().data(), static_cast<size_t>(stride) * arena.Size());
            }
            else
            {
                std::memset(mapped_data, 0, stride);
            }
            io_buffer->Unmap();
            arena.ClearDirty();
            return;
        }

        if (!arena.Dirty())
        {
            return;
        }

        void* mapped_data = io_buffer->Map();
        if (arena.Size() > 0 && arena.DirtyCount() > 0)
        {
            std::memcpy(static_cast<uint8_t*>(mapped_data) + static_cast<size_t>(arena.DirtyOffset()) * stride,
                arena.Data().data() + arena.DirtyOffset(), static_cast<size_t>(arena.DirtyCount()) * stride);
        }
        else if (arena.Size() == 0)
        {
            std::memset(mapped_data, 0, stride);
        }
        io_buffer->Unmap();
        arena.ClearDirty();
    }

private:
    LinearArena<TVertex> vertices_;
    LinearArena<TIndex> indices_;
    gpu::BufferPtr vertex_buffer_;
    gpu::BufferPtr index_buffer_;
    uint32_t vertex_count_ = 0;
    uint32_t index_count_ = 0;
};
