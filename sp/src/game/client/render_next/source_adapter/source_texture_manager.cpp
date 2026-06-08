#include "cbase.h"
#include "source_texture_manager.h"

#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "vtf/vtf.h"

#include <algorithm>
#include <array>

namespace
{
struct LoadedTextureData
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::vector<uint8_t>> mip_pixels;
};

std::array<uint8_t, 16> MakeFallbackTexturePixels()
{
    return {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};
}

std::string StripExtension(std::string path)
{
    size_t extension_pos = path.find_last_of('.');
    if (extension_pos != std::string::npos)
    {
        path.erase(extension_pos);
    }
    return path;
}

void TransitionImage(gpu::CommandBuffer& cmd_buffer, std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts,
    gpu::ImagePtr const& image, gpu::ImageLayout desired_layout)
{
    if (!image)
    {
        Warning("render_next: attempted to transition a null texture image\n");
        return;
    }

    gpu::ImageLayout& current_layout = image_layouts[image.get()];
    if (current_layout == desired_layout)
    {
        return;
    }

    cmd_buffer.TransitionBarrier(image, current_layout, desired_layout);
    current_layout = desired_layout;
}

gpu::ImagePtr CreateTextureImage(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, uint32_t width, uint32_t height,
    gpu::ImageFormat format, std::vector<std::vector<uint8_t>> const& mip_pixels)
{
    if (!device)
    {
        Warning("render_next: cannot create texture image without a GPU device\n");
        return {};
    }

    if (width == 0 || height == 0)
    {
        Warning("render_next: cannot create texture image with invalid size %ux%u\n", width, height);
        return {};
    }

    if (mip_pixels.empty() || mip_pixels[0].empty())
    {
        Warning("render_next: cannot create texture image %ux%u without pixel data\n", width, height);
        return {};
    }

    uint32_t mip_count = static_cast<uint32_t>(mip_pixels.size());
    gpu::ImageFlags flags = gpu::ImageFlags::kShaderResource;
    gpu::ImagePtr image = device->CreateImage(width, height, format, flags, mip_count);
    if (!image)
    {
        Warning("render_next: CreateImage failed for texture %ux%u\n", width, height);
        return {};
    }

    TransitionImage(cmd_buffer, image_layouts, image, gpu::ImageLayout::kCopyDst);
    for (uint32_t mip_level = 0; mip_level < mip_count; ++mip_level)
    {
        std::vector<uint8_t> const& mip_data = mip_pixels[mip_level];
        if (mip_data.empty())
        {
            Warning("render_next: texture mip %u for %ux%u is empty\n", mip_level, width, height);
            return {};
        }

        cmd_buffer.UploadImage(image, mip_data.data(), mip_data.size(), mip_level);
    }

    TransitionImage(cmd_buffer, image_layouts, image, gpu::ImageLayout::kShaderRead);
    return image;
}

bool LoadTextureRgba(char const* texture_name, LoadedTextureData& out_texture)
{
    if (!texture_name || texture_name[0] == '\0')
    {
        return false;
    }

    std::string vtf_path = "materials/" + StripExtension(texture_name) + ".vtf";
    CUtlBuffer buffer;
    if (!g_pFullFileSystem->ReadFile(vtf_path.c_str(), "GAME", buffer))
    {
        return false;
    }

    IVTFTexture* vtf_texture = CreateVTFTexture();
    if (!vtf_texture)
    {
        return false;
    }

    bool success = false;
    do
    {
        if (!vtf_texture->Unserialize(buffer))
        {
            break;
        }

        int width = vtf_texture->Width();
        int height = vtf_texture->Height();
        if (width <= 0 || height <= 0)
        {
            break;
        }

        constexpr ::ImageFormat kDstFormat = IMAGE_FORMAT_RGBA8888;
        out_texture.width = static_cast<uint32_t>(width);
        out_texture.height = static_cast<uint32_t>(height);
        int mip_count = vtf_texture->MipCount();
        if (mip_count <= 0)
        {
            break;
        }

        out_texture.mip_pixels.clear();
        out_texture.mip_pixels.resize(mip_count);

        for (int mip_level = 0; mip_level < mip_count; ++mip_level)
        {
            int mip_width = 0;
            int mip_height = 0;
            int mip_depth = 0;
            vtf_texture->ComputeMipLevelDimensions(mip_level, &mip_width, &mip_height, &mip_depth);
            if (mip_width <= 0 || mip_height <= 0)
            {
                out_texture.mip_pixels.clear();
                break;
            }

            size_t image_size = static_cast<size_t>(ImageLoader::GetMemRequired(mip_width, mip_height, 1, kDstFormat, false));
            std::vector<uint8_t>& mip_pixels = out_texture.mip_pixels[mip_level];
            mip_pixels.resize(image_size);

            if (!ImageLoader::ConvertImageFormat(vtf_texture->ImageData(0, 0, mip_level), vtf_texture->Format(),
                    mip_pixels.data(), kDstFormat, mip_width, mip_height, 0, 0))
            {
                out_texture.mip_pixels.clear();
                break;
            }
        }

        if (out_texture.mip_pixels.empty())
        {
            break;
        }

        success = true;
    } while (false);

    DestroyVTFTexture(vtf_texture);
    return success;
}
}

void SourceTextureManager::Reset()
{
    texture_ids_by_name_.clear();
    textures_.clear();
}

void SourceTextureManager::EnsureFallbackTexture(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts)
{
    if (!textures_.empty() && textures_[0])
    {
        return;
    }

    std::array<uint8_t, 16> fallback_pixels = MakeFallbackTexturePixels();
    gpu::ImagePtr fallback_texture = CreateTextureImage(device, cmd_buffer, image_layouts, 2, 2, gpu::ImageFormat::kRGBA8_UNorm,
        {std::vector<uint8_t>(fallback_pixels.begin(), fallback_pixels.end())});
    if (!fallback_texture)
    {
        Warning("render_next: failed to create fallback texture\n");
        return;
    }

    if (textures_.empty())
    {
        textures_.push_back(std::move(fallback_texture));
    }
    else
    {
        textures_[0] = std::move(fallback_texture);
    }
}

uint32_t SourceTextureManager::LoadTexture(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, char const* texture_name)
{
    EnsureFallbackTexture(device, cmd_buffer, image_layouts);

    if (!texture_name || texture_name[0] == '\0')
    {
        return 0;
    }

    auto existing = texture_ids_by_name_.find(texture_name);
    if (existing != texture_ids_by_name_.end())
    {
        return existing->second;
    }

    LoadedTextureData texture_data;
    if (!LoadTextureRgba(texture_name, texture_data))
    {
        texture_ids_by_name_.emplace(texture_name, 0u);
        return 0;
    }

    gpu::ImagePtr texture;
    try
    {
        texture = CreateTextureImage(device, cmd_buffer, image_layouts, texture_data.width, texture_data.height,
            gpu::ImageFormat::kRGBA8_UNorm, texture_data.mip_pixels);
    }
    catch (std::exception const& error)
    {
        Warning("render_next: failed to create texture '%s' (%ux%u, %zu bytes): %s\n",
            texture_name, texture_data.width, texture_data.height,
            texture_data.mip_pixels.empty() ? 0 : texture_data.mip_pixels[0].size(), error.what());
        texture_ids_by_name_.emplace(texture_name, 0u);
        return 0;
    }
    catch (...)
    {
        Warning("render_next: failed to create texture '%s' (%ux%u, %zu bytes): unknown exception\n",
            texture_name, texture_data.width, texture_data.height,
            texture_data.mip_pixels.empty() ? 0 : texture_data.mip_pixels[0].size());
        texture_ids_by_name_.emplace(texture_name, 0u);
        return 0;
    }

    if (!texture)
    {
        Warning("render_next: CreateTextureImage returned null for texture '%s' (%ux%u, %zu bytes)\n",
            texture_name, texture_data.width, texture_data.height,
            texture_data.mip_pixels.empty() ? 0 : texture_data.mip_pixels[0].size());
        texture_ids_by_name_.emplace(texture_name, 0u);
        return 0;
    }

    uint32_t texture_id = static_cast<uint32_t>(textures_.size());
    textures_.push_back(std::move(texture));
    texture_ids_by_name_.emplace(texture_name, texture_id);
    return texture_id;
}

uint32_t SourceTextureManager::GetTextureCount() const
{
    return static_cast<uint32_t>(textures_.size());
}

gpu::ImagePtr const& SourceTextureManager::GetTexture(uint32_t texture_id) const
{
    if (textures_.empty())
    {
        static gpu::ImagePtr empty_texture;
        return empty_texture;
    }

    if (texture_id >= textures_.size() || !textures_[texture_id])
    {
        return textures_[0];
    }

    return textures_[texture_id];
}

void SourceTextureManager::BuildDescriptorArray(uint32_t count, std::vector<gpu::ImageDescriptor>& out_descriptors) const
{
    out_descriptors.resize(count);
    for (uint32_t texture_index = 0; texture_index < count; ++texture_index)
    {
        gpu::ImagePtr const& image = GetTexture(texture_index);
        gpu::ImageView full_view = {};
        if (image)
        {
            full_view.mip_count = image->GetMipCount();
        }

        out_descriptors[texture_index] = gpu::ImageDescriptor{image.get(), full_view};
    }
}
