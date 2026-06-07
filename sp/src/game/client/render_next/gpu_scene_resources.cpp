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
#include <array>
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

gpu::ImagePtr CreateTextureImage(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, uint32_t width, uint32_t height,
    gpu::ImageFormat format, void const* data, size_t data_size)
{
    gpu::ImagePtr image =
        device->CreateImage(width, height, format, gpu::ImageFlags::kShaderResource);
    gpu::ImageLayout& current_layout = image_layouts[image.get()];
    if (current_layout != gpu::ImageLayout::kCopyDst)
    {
        cmd_buffer.TransitionBarrier(image, current_layout, gpu::ImageLayout::kCopyDst);
        current_layout = gpu::ImageLayout::kCopyDst;
    }
    cmd_buffer.UploadImage(image, data, data_size);
    if (current_layout != gpu::ImageLayout::kShaderRead)
    {
        cmd_buffer.TransitionBarrier(image, current_layout, gpu::ImageLayout::kShaderRead);
        current_layout = gpu::ImageLayout::kShaderRead;
    }
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

}

void RenderSceneGpu::EnsureFallbackTextures(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts)
{
    if (!fallback_lightmap_texture)
    {
        std::array<uint8_t, 4> fallback_lightmap_pixels = {255, 255, 255, 255};
        fallback_lightmap_texture =
            CreateTextureImage(device, cmd_buffer, image_layouts, 1, 1, gpu::ImageFormat::kRGBA8_UNorm,
                fallback_lightmap_pixels.data(), fallback_lightmap_pixels.size());
    }
}

void RenderSceneGpu::EnsureFallbackSceneBuffers(gpu::DevicePtr const& device)
{
    if (!scene_transform_buffer)
    {
        SceneTransform identity_transform = MakeIdentitySceneTransform();
        scene_transform_buffer = device->CreateBuffer(sizeof(SceneTransform), sizeof(SceneTransform),
            gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
        void* transform_data = scene_transform_buffer->Map();
        std::memcpy(transform_data, &identity_transform, sizeof(SceneTransform));
        scene_transform_buffer->Unmap();
    }

    if (!scene_instance_buffer)
    {
        RenderInstance fallback_instance = {};
        scene_instance_buffer = device->CreateBuffer(sizeof(RenderInstance), sizeof(RenderInstance),
            gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
        void* instance_data = scene_instance_buffer->Map();
        std::memcpy(instance_data, &fallback_instance, sizeof(RenderInstance));
        scene_instance_buffer->Unmap();
    }

    if (!scene_vertex_color_buffer)
    {
        VertexColorData fallback_vertex_color = {};
        scene_vertex_color_buffer = device->CreateBuffer(sizeof(VertexColorData), sizeof(VertexColorData),
            gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
        void* vertex_color_data = scene_vertex_color_buffer->Map();
        std::memcpy(vertex_color_data, &fallback_vertex_color, sizeof(VertexColorData));
        scene_vertex_color_buffer->Unmap();
    }

}

void UploadSkyboxTexturesToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::array<std::string, 6> const& skybox_texture_names,
    TextureManager& texture_manager, RenderSceneGpu& out_gpu_scene)
{
    out_gpu_scene.EnsureFallbackTextures(device, cmd_buffer, image_layouts);
    out_gpu_scene.EnsureFallbackSceneBuffers(device);

    out_gpu_scene.skybox_texture_ids.fill(0);
    for (size_t face_index = 0; face_index < skybox_texture_names.size(); ++face_index)
    {
        out_gpu_scene.skybox_texture_ids[face_index] =
            texture_manager.LoadTexture(device, cmd_buffer, image_layouts, skybox_texture_names[face_index].c_str());
    }

}

std::vector<uint32_t> BuildMaterialTextureIds(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu const& scene, TextureManager& texture_manager)
{
    std::vector<uint32_t> material_texture_ids(scene.materials.size() + 1, 0);
    for (size_t material_index = 0; material_index < scene.materials.size(); ++material_index)
    {
        std::string base_texture_name;
        if (ResolveBaseTextureName(scene.materials[material_index].material_name.c_str(), base_texture_name))
        {
            material_texture_ids[material_index + 1] =
                texture_manager.LoadTexture(device, cmd_buffer, image_layouts, base_texture_name.c_str());
        }
    }

    return material_texture_ids;
}

std::vector<RenderInstance> BuildUploadedInstances(RenderSceneCpu const& scene, std::vector<uint32_t> const& material_texture_ids)
{
    std::vector<RenderInstance> upload_instances = scene.instances;
    for (RenderInstance& instance : upload_instances)
    {
        if (instance.material_index >= material_texture_ids.size())
        {
            instance.material_index = 0;
        }
        else
        {
            instance.material_index = material_texture_ids[instance.material_index];
        }

        if (instance.transform_index >= scene.transforms.size())
        {
            instance.transform_index = 0;
        }
        if (instance.index_offset + instance.index_count > scene.geometry.IndexCount())
        {
            instance.index_offset = 0;
            instance.index_count = 0;
        }
        if (instance.vertex_color_offset != RenderInstance::kInvalidVertexColorOffset
            && instance.index_count > 0)
        {
            uint32_t required_vertex_color_count = instance.padding0;
            if (instance.vertex_color_offset + required_vertex_color_count > scene.vertex_colors.size())
            {
                instance.vertex_color_offset = RenderInstance::kInvalidVertexColorOffset;
            }
        }
    }

    return upload_instances;
}

void UploadRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu& scene, TextureManager& texture_manager,
    RenderSceneGpu& out_gpu_scene)
{
    out_gpu_scene.EnsureFallbackTextures(device, cmd_buffer, image_layouts);
    out_gpu_scene.EnsureFallbackSceneBuffers(device);

    if (!scene.lightmap_atlas.pixels.empty() && scene.lightmap_atlas.width > 0 && scene.lightmap_atlas.height > 0)
    {
        gpu::ImageFormat lightmap_format = scene.lightmap_atlas.format == LightmapAtlas::Format::kRGBA32Float
            ? gpu::ImageFormat::kRGBA32_Float
            : gpu::ImageFormat::kRGBA8_UNorm;
        out_gpu_scene.lightmap_texture = CreateTextureImage(device, cmd_buffer, image_layouts,
            static_cast<uint32_t>(scene.lightmap_atlas.width), static_cast<uint32_t>(scene.lightmap_atlas.height),
            lightmap_format, scene.lightmap_atlas.pixels.data(), scene.lightmap_atlas.pixels.size());
    }
    else
    {
        out_gpu_scene.lightmap_texture = out_gpu_scene.fallback_lightmap_texture;
    }

    out_gpu_scene.scene_transform_buffer = device->CreateBuffer(sizeof(SceneTransform) * scene.transforms.size(), sizeof(SceneTransform),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* transform_data = out_gpu_scene.scene_transform_buffer->Map();
    std::memcpy(transform_data, scene.transforms.data(), sizeof(SceneTransform) * scene.transforms.size());
    out_gpu_scene.scene_transform_buffer->Unmap();

    out_gpu_scene.material_texture_ids =
        BuildMaterialTextureIds(device, cmd_buffer, image_layouts, scene, texture_manager);
    std::vector<RenderInstance> upload_instances =
        BuildUploadedInstances(scene, out_gpu_scene.material_texture_ids);
    out_gpu_scene.instance_count = static_cast<uint32_t>(upload_instances.size());
    out_gpu_scene.uploaded_instances = upload_instances;

    RenderInstance fallback_instance = {};
    size_t upload_instance_count = upload_instances.empty() ? 1 : upload_instances.size();
    out_gpu_scene.scene_instance_buffer = device->CreateBuffer(sizeof(RenderInstance) * upload_instance_count, sizeof(RenderInstance),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* instance_data = out_gpu_scene.scene_instance_buffer->Map();
    if (upload_instances.empty())
    {
        out_gpu_scene.uploaded_instances.clear();
        out_gpu_scene.instance_count = 0;
        std::memcpy(instance_data, &fallback_instance, sizeof(RenderInstance));
    }
    else
    {
        std::memcpy(instance_data, upload_instances.data(), sizeof(RenderInstance) * upload_instances.size());
    }
    out_gpu_scene.scene_instance_buffer->Unmap();

    size_t upload_vertex_color_count = scene.vertex_colors.empty() ? 1 : scene.vertex_colors.size();
    out_gpu_scene.scene_vertex_color_buffer = device->CreateBuffer(sizeof(VertexColorData) * upload_vertex_color_count, sizeof(VertexColorData),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* vertex_color_data = out_gpu_scene.scene_vertex_color_buffer->Map();
    if (scene.vertex_colors.empty())
    {
        VertexColorData fallback_vertex_color = {};
        std::memcpy(vertex_color_data, &fallback_vertex_color, sizeof(VertexColorData));
    }
    else
    {
        std::memcpy(vertex_color_data, scene.vertex_colors.data(), sizeof(VertexColorData) * scene.vertex_colors.size());
    }
    out_gpu_scene.scene_vertex_color_buffer->Unmap();

    scene.geometry.SyncToGpu(device);
}
