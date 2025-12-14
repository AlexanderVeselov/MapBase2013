#pragma once

#include <memory>
#include <unordered_map>
#include <cassert>

namespace rhi
{
class Texture;
class Buffer;
class GraphicsPipeline;
class ComputePipeline;
class RHI;

RHI* CreateRHI(uint32_t adapter);

enum class ImageFormat
{
    kUnknown,
    kRGBA32_Float,
    kRGBA16_Float,
    kRGBA8_SInt,
    kRGBA8_UInt,
    kRGBA8_UNorm,
    kBGRA8_UNorm,
    kRGBA8_SRGB,
    kRG32_Float,
    kRG16_Float,
    kR32_Float,
    kR32_Typeless,
    kD32_Float,
    kR16_Float
};

enum class ResourceType
{
    kTexture,
    kBuffer
};

enum class TextureBindFlags
{
    kNone = 0,
    kShaderResource = 1 << 0,
    kRenderTarget  = 1 << 1,
    kDepthStencil = 1 << 2,
    kUnorderedAccess = 1 << 3
};

enum class BufferUsage
{
    kDefault,
    kDynamic
};

enum class BufferBindFlags : uint32_t
{
    kNone = 1 << 0,
    kVertexBuffer = 1 << 1,
    kIndexBuffer  = 1 << 2,
    kConstantBuffer = 1 << 3,
    kShaderResource = 1 << 4,
    kUnorderedAccess = 1 << 5
};

template<typename Enum>
constexpr auto ToU(Enum e)
{
    return static_cast<std::underlying_type_t<Enum>>(e);
}

template<typename Enum>
constexpr Enum operator|(Enum a, Enum b)
{
    return static_cast<Enum>(ToU(a) | ToU(b));
}

template<typename Enum>
constexpr Enum operator&(Enum a, Enum b)
{
    return static_cast<Enum>(ToU(a) & ToU(b));
}

template<typename Enum>
constexpr uint32_t operator&(Enum a, Enum b)
{
    return static_cast<uint32_t>(ToU(a) & ToU(b));
}

template<typename Enum>
constexpr Enum& operator|=(Enum& a, Enum b)
{
    a = a | b;
    return a;
}

template<typename Enum>
constexpr bool HasFlag(Enum value, Enum flag)
{
    return (ToU(value) & ToU(flag)) != 0;
}

class RHI
{
public:
    virtual std::shared_ptr<Texture> CreateTexture(uint32_t width, uint32_t height, ImageFormat format,
        uint32_t mip_levels = 1, TextureBindFlags bind_flags = TextureBindFlags::kNone, uint32_t misc_flags = 0) = 0;
    virtual std::shared_ptr<Buffer> CreateBuffer(uint32_t size, BufferUsage usage, BufferBindFlags bind_flags) = 0;
    virtual std::shared_ptr<GraphicsPipeline> CreateGraphicsPipeline(char const* vs, char const* ps) = 0;
    virtual std::shared_ptr<ComputePipeline> CreateComputePipeline(char const* cs) = 0;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void BindGraphicsPipeline(std::shared_ptr<GraphicsPipeline> pipeline) = 0;
    virtual void BindComputePipeline(std::shared_ptr<ComputePipeline> pipeline) = 0;
    virtual void Draw(uint32_t vertex_count, uint32_t start_vertex) = 0;
    virtual void Dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) = 0;
    virtual void SetRenderTarget(std::shared_ptr<Texture> color,
        std::shared_ptr<Texture> depth) = 0;
    virtual void ClearColorTexture(std::shared_ptr<Texture> texture, float r, float g, float b, float a) = 0;
    virtual void ClearDepthTexture(std::shared_ptr<Texture> texture, float depth) = 0;
    virtual void UploadBuffer(std::shared_ptr<Buffer> buffer, void const* data, size_t size) = 0;
    virtual void* MapBuffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void UnmapBuffer(std::shared_ptr<Buffer> buffer) = 0;

    // Resource binding
    virtual void BindVertexBuffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void BindIndexBuffer(std::shared_ptr<Buffer> buffer) = 0;

    virtual void Flush() = 0;
    virtual ~RHI() = default;
};

class Resource
{
public:
    virtual ResourceType GetType() const = 0;
    virtual ~Resource() = default;
};

class Texture : public Resource
{
public:
    Texture(uint32_t width, uint32_t height, ImageFormat format, uint32_t mip_levels, TextureBindFlags bind_flags)
        : width_(width), height_(height), format_(format), mip_levels_(mip_levels), bind_flags_(bind_flags) {}

    ResourceType GetType() const override { return ResourceType::kTexture; }
    virtual void* GetSharedHandle() const = 0;
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    ImageFormat GetFormat() const { return format_; }
    uint32_t GetMipLevels() const { return mip_levels_; }
    TextureBindFlags GetBindFlags() const { return bind_flags_; }

protected:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    ImageFormat format_ = ImageFormat::kUnknown;
    uint32_t mip_levels_ = 1;
    TextureBindFlags bind_flags_ = TextureBindFlags::kNone;
};

class Buffer : public Resource
{
public:
    Buffer(uint32_t size, BufferUsage usage, BufferBindFlags bind_flags)
        : size_(size), usage_(usage), bind_flags_(bind_flags) {}

    ResourceType GetType() const override { return ResourceType::kBuffer; }
    uint32_t GetSize() const { return size_; }
    BufferUsage GetUsage() const { return usage_; }
    BufferBindFlags GetBindFlags() const { return bind_flags_; }

private:
    uint32_t size_ = 0;
    BufferUsage usage_ = BufferUsage::kDefault;
    BufferBindFlags bind_flags_ = BufferBindFlags::kNone;
};

class Pipeline
{
public:
    virtual void BindConstantBuffer(std::shared_ptr<Buffer> buffer, uint32_t slot)
    {
        assert(HasFlag(buffer->GetBindFlags(), BufferBindFlags::kConstantBuffer) &&
            "Trying to bind a non-constant buffer as constant buffer");
        bound_constant_buffers_[slot] = buffer;
    }

    virtual void BindShaderResource(std::shared_ptr<Texture> texture, uint32_t slot)
    {
        assert(HasFlag(texture->GetBindFlags(), TextureBindFlags::kShaderResource) &&
            "Trying to bind a non-shader resource texture as shader resource");
        bound_shader_resources_[slot] = texture;
    }

    virtual void BindShaderResource(std::shared_ptr<Buffer> buffer, uint32_t slot)
    {
        assert(HasFlag(buffer->GetBindFlags(), BufferBindFlags::kShaderResource) &&
            "Trying to bind a non-shader resource buffer as shader resource");
        bound_shader_resources_[slot] = buffer;
    }

    virtual void BindStorageResource(std::shared_ptr<Texture> texture, uint32_t slot)
    {
        assert(HasFlag(texture->GetBindFlags(), TextureBindFlags::kUnorderedAccess) &&
            "Trying to bind a non-storage texture as storage resource");
        bound_storage_resources_[slot] = texture;
    }

    virtual void BindStorageResource(std::shared_ptr<Buffer> buffer, uint32_t slot)
    {
        assert(HasFlag(buffer->GetBindFlags(), BufferBindFlags::kUnorderedAccess) &&
            "Trying to bind a non-storage buffer as storage resource");
        bound_storage_resources_[slot] = buffer;
    }

    virtual ~Pipeline() = default;

    // Resource getters for RHI implementations
    const std::unordered_map<uint32_t, std::shared_ptr<Buffer>>& GetBoundConstantBuffers() const
    {
        return bound_constant_buffers_;
    }

    const std::unordered_map<uint32_t, std::shared_ptr<Resource>>& GetBoundShaderResources() const
    {
        return bound_shader_resources_;
    }

    const std::unordered_map<uint32_t, std::shared_ptr<Resource>>& GetBoundStorageResources() const
    {
        return bound_storage_resources_;
    }

private:
    std::unordered_map<uint32_t, std::shared_ptr<Buffer>> bound_constant_buffers_;
    std::unordered_map<uint32_t, std::shared_ptr<Resource>> bound_shader_resources_;
    std::unordered_map<uint32_t, std::shared_ptr<Resource>> bound_storage_resources_;
};

class GraphicsPipeline : public Pipeline
{
};

class ComputePipeline : public Pipeline
{
};

} // namespace rhi
