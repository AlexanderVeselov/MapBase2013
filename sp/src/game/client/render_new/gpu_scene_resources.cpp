#include "cbase.h"
#include "gpu_scene_resources.h"

#include "filesystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "texture_group_names.h"
#include "tier1/utlbuffer.h"
#include "vtf/vtf.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

extern IMaterialSystem* materials;

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;

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
    gpu::ImageLayout& current_layout = image_layouts[image.get()];
    if (current_layout == desired_layout)
    {
        return;
    }

    cmd_buffer.TransitionBarrier(image, current_layout, desired_layout);
    current_layout = desired_layout;
}

gpu::ImagePtr CreateTextureImage(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, uint32_t width, uint32_t height, void const* data, size_t data_size)
{
    gpu::ImagePtr image =
        device->CreateImage(width, height, gpu::ImageFormat::kRGBA8_UNorm, gpu::ImageFlags::kShaderResource);
    TransitionImage(cmd_buffer, image_layouts, image, gpu::ImageLayout::kCopyDst);
    cmd_buffer.UploadImage(image, data, data_size);
    TransitionImage(cmd_buffer, image_layouts, image, gpu::ImageLayout::kShaderRead);
    return image;
}

IMaterial* FindNamedMaterial(char const* material_name)
{
    if (!material_name || material_name[0] == '\0')
    {
        return nullptr;
    }

    IMaterial* material = materials->FindMaterial(material_name, TEXTURE_GROUP_WORLD, false);
    if (material && !IsErrorMaterial(material))
    {
        return material;
    }

    material = materials->FindMaterial(material_name, TEXTURE_GROUP_MODEL, false);
    if (material && !IsErrorMaterial(material))
    {
        return material;
    }

    return nullptr;
}

bool ResolveBaseTextureName(char const* material_name, std::string& out_texture_name)
{
    if (!material_name || material_name[0] == '\0')
    {
        return false;
    }

    IMaterial* material = FindNamedMaterial(material_name);
    if (!material)
    {
        return false;
    }

    bool found = false;
    IMaterialVar* base_texture_var = material->FindVar("$basetexture", &found, false);
    if (!found || !base_texture_var)
    {
        base_texture_var = material->FindVar("%tooltexture", &found, false);
        if (!found || !base_texture_var)
        {
            return false;
        }
    }

    ITexture* texture = base_texture_var->GetTextureValue();
    if (texture && !texture->IsError())
    {
        out_texture_name = StripExtension(texture->GetName());
        std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
        return !out_texture_name.empty();
    }

    char const* base_texture_name = base_texture_var->GetStringValue();
    if (!base_texture_name[0])
    {
        return false;
    }

    out_texture_name = StripExtension(base_texture_name);
    std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
    return !out_texture_name.empty();
}

bool LoadTextureRgba(char const* texture_name, std::vector<uint8_t>& out_pixels, uint32_t& out_width, uint32_t& out_height)
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
        size_t image_size = static_cast<size_t>(ImageLoader::GetMemRequired(width, height, 1, kDstFormat, false));
        out_pixels.resize(image_size);

        if (!ImageLoader::ConvertImageFormat(vtf_texture->ImageData(0, 0, 0), vtf_texture->Format(), out_pixels.data(),
                kDstFormat, width, height, 0, 0))
        {
            out_pixels.clear();
            break;
        }

        out_width = static_cast<uint32_t>(width);
        out_height = static_cast<uint32_t>(height);
        success = true;
    } while (false);

    DestroyVTFTexture(vtf_texture);
    return success;
}

void UploadVertices(gpu::DevicePtr const& device, std::vector<Vertex> const& vertices, RenderSceneGpu& out_gpu_scene)
{
    out_gpu_scene.vertex_count = static_cast<uint32_t>(vertices.size());
    out_gpu_scene.vertex_buffer = device->CreateBuffer(sizeof(Vertex) * vertices.size(), sizeof(Vertex),
        gpu::BufferFlags::kCpuAccess);

    if (!vertices.empty())
    {
        void* mapped_data = out_gpu_scene.vertex_buffer->Map();
        std::memcpy(mapped_data, vertices.data(), sizeof(Vertex) * vertices.size());
        out_gpu_scene.vertex_buffer->Unmap();
    }
}

void RebuildMaterialBindings(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::vector<BspMaterial> const& bsp_materials,
    std::vector<Vertex>& vertices, gpu::BufferPtr const& view_proj_buffer, gpu::BufferPtr const& scene_transform_buffer,
    gpu::SamplerPtr const& texture_sampler, gpu::SamplerPtr const& lightmap_sampler, gpu::ImagePtr const& fallback_texture,
    gpu::ImagePtr const& fallback_lightmap_texture, gpu::DescriptorSetPtr const& pipeline_descriptor_set, RenderSceneGpu& out_gpu_scene)
{
    out_gpu_scene.material_textures.clear();
    out_gpu_scene.material_textures.resize(bsp_materials.size() + 1);
    out_gpu_scene.material_textures[0] = fallback_texture;

    for (size_t material_index = 0; material_index < bsp_materials.size(); ++material_index)
    {
        if (material_index + 1 >= kMaxMaterialTextures)
        {
            break;
        }

        std::string base_texture_name;
        std::vector<uint8_t> rgba_pixels;
        uint32_t width = 0;
        uint32_t height = 0;

        gpu::ImagePtr texture_image = fallback_texture;
        if (ResolveBaseTextureName(bsp_materials[material_index].material_name.c_str(), base_texture_name)
            && LoadTextureRgba(base_texture_name.c_str(), rgba_pixels, width, height))
        {
            texture_image = CreateTextureImage(device, cmd_buffer, image_layouts, width, height, rgba_pixels.data(), rgba_pixels.size());
        }

        out_gpu_scene.material_textures[material_index + 1] = texture_image;
    }

    for (Vertex& vertex : vertices)
    {
        if (vertex.texture_index >= kMaxMaterialTextures || vertex.texture_index >= out_gpu_scene.material_textures.size())
        {
            vertex.texture_index = 0;
        }
    }

    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    for (uint32_t texture_index = 0; texture_index < kMaxMaterialTextures; ++texture_index)
    {
        gpu::ImagePtr const& image = texture_index < out_gpu_scene.material_textures.size() && out_gpu_scene.material_textures[texture_index]
            ? out_gpu_scene.material_textures[texture_index]
            : fallback_texture;
        image_descriptors[texture_index] = gpu::ImageDescriptor{image.get(), {}};
    }

    pipeline_descriptor_set->Clear();
    pipeline_descriptor_set->BindBuffer(*view_proj_buffer, 0);
    pipeline_descriptor_set->BindBuffer(*scene_transform_buffer, 1);
    pipeline_descriptor_set->BindImageArray(image_descriptors, 0, 1);
    pipeline_descriptor_set->BindSampler(*texture_sampler, 0, 2);
    pipeline_descriptor_set->BindSampler(*lightmap_sampler, 1, 2);
    pipeline_descriptor_set->BindImage(*(out_gpu_scene.lightmap_texture ? out_gpu_scene.lightmap_texture : fallback_lightmap_texture), 0, 3);
}
}

void UploadRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu const& scene,
    gpu::BufferPtr const& view_proj_buffer, gpu::SamplerPtr const& texture_sampler, gpu::SamplerPtr const& lightmap_sampler,
    gpu::ImagePtr const& fallback_texture, gpu::ImagePtr const& fallback_lightmap_texture,
    gpu::DescriptorSetPtr const& pipeline_descriptor_set, RenderSceneGpu& out_gpu_scene)
{
    if (!scene.lightmap_atlas.rgba_pixels.empty() && scene.lightmap_atlas.width > 0 && scene.lightmap_atlas.height > 0)
    {
        out_gpu_scene.lightmap_texture = CreateTextureImage(device, cmd_buffer, image_layouts,
            static_cast<uint32_t>(scene.lightmap_atlas.width), static_cast<uint32_t>(scene.lightmap_atlas.height),
            scene.lightmap_atlas.rgba_pixels.data(), scene.lightmap_atlas.rgba_pixels.size());
    }
    else
    {
        out_gpu_scene.lightmap_texture = fallback_lightmap_texture;
    }

    out_gpu_scene.scene_transform_buffer = device->CreateBuffer(sizeof(SceneTransform) * scene.transforms.size(), sizeof(SceneTransform),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* transform_data = out_gpu_scene.scene_transform_buffer->Map();
    std::memcpy(transform_data, scene.transforms.data(), sizeof(SceneTransform) * scene.transforms.size());
    out_gpu_scene.scene_transform_buffer->Unmap();

    std::vector<Vertex> upload_vertices = scene.vertices;
    RebuildMaterialBindings(device, cmd_buffer, image_layouts, scene.materials, upload_vertices, view_proj_buffer,
        out_gpu_scene.scene_transform_buffer, texture_sampler, lightmap_sampler, fallback_texture, fallback_lightmap_texture,
        pipeline_descriptor_set, out_gpu_scene);
    UploadVertices(device, upload_vertices, out_gpu_scene);
}
