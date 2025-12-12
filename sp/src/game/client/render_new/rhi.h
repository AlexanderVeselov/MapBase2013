#pragma once

#include <memory>

namespace rhi
{

class Texture;
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

class RHI
{
public:
    virtual std::shared_ptr<Texture> CreateTexture(uint32_t width, uint32_t height, ImageFormat format,
        uint32_t mip_levels = 1, uint32_t bind_flags = 0, uint32_t misc_flags = 0) = 0;
    virtual std::shared_ptr<Pipeline> CreatePipeline(char const* vs, char const* ps) = 0;
    virtual void BindPipeline(std::shared_ptr<Pipeline> pipeline) = 0;
    virtual void Draw(uint32_t vertex_count, uint32_t start_vertex) = 0;
    virtual void ClearTexture(std::shared_ptr<Texture> texture, float r, float g, float b, float a) = 0;
    virtual void Flush() = 0;
    virtual ~RHI() = default;
};

class Texture
{
public:
	virtual void* GetSharedHandle() const = 0;
    virtual ~Texture() = default;
};

class Pipeline
{
public:
    virtual ~Pipeline() = default;
};

}
