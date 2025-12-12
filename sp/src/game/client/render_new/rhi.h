#pragma once

#include <memory>

namespace rhi
{

class Texture;
class Buffer;
class Pipeline;
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
    kR16_Float
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
        uint32_t mip_levels = 1, uint32_t bind_flags = 0, uint32_t misc_flags = 0) = 0;
    virtual std::shared_ptr<Buffer> CreateBuffer(uint32_t size, BufferUsage usage, BufferBindFlags bind_flags) = 0;
    virtual std::shared_ptr<Pipeline> CreatePipeline(char const* vs, char const* ps) = 0;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void BindPipeline(std::shared_ptr<Pipeline> pipeline) = 0;
    virtual void Draw(uint32_t vertex_count, uint32_t start_vertex) = 0;
    virtual void SetRenderTarget(std::shared_ptr<Texture> texture) = 0;
    virtual void ClearTexture(std::shared_ptr<Texture> texture, float r, float g, float b, float a) = 0;
    virtual void UploadBuffer(std::shared_ptr<Buffer> buffer, void const* data, size_t size) = 0;
    virtual void* MapBuffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void UnmapBuffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void BindVertexBuffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void BindIndexBuffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void BindConstantBuffer(std::shared_ptr<Buffer> buffer, uint32_t slot) = 0;
    virtual void Flush() = 0;
    virtual ~RHI() = default;
};

class Texture
{
public:
    Texture(uint32_t width, uint32_t height, ImageFormat format, uint32_t mip_levels = 1)
        : width_(width), height_(height), format_(format), mip_levels_(mip_levels) {}

    virtual void* GetSharedHandle() const = 0;
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    virtual ~Texture() = default;

protected:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    ImageFormat format_ = ImageFormat::kUnknown;
    uint32_t mip_levels_ = 1;
};

class Buffer
{
public:
    Buffer(uint32_t size, BufferUsage usage, BufferBindFlags bind_flags)
        : size_(size), usage_(usage), bind_flags_(bind_flags) {}
    virtual ~Buffer() = default;

private:
    uint32_t size_ = 0;
    BufferUsage usage_ = BufferUsage::kDefault;
    BufferBindFlags bind_flags_ = BufferBindFlags::kNone;
};

class Pipeline
{
public:
    virtual ~Pipeline() = default;
};

} // namespace rhi
