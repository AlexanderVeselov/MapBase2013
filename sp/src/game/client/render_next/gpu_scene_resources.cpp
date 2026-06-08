#include "cbase.h"
#include "gpu_scene_resources.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace
{
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
}

void RenderScene::EnsureFallbackTextures(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
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

void UploadSkyboxTexturesToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::array<std::string, 6> const& skybox_texture_names,
    SourceTextureManager& texture_manager, RenderScene& out_scene)
{
    out_scene.EnsureFallbackTextures(device, cmd_buffer, image_layouts);

    out_scene.skybox_texture_ids.fill(0);
    for (size_t face_index = 0; face_index < skybox_texture_names.size(); ++face_index)
    {
        out_scene.skybox_texture_ids[face_index] =
            texture_manager.LoadTexture(device, cmd_buffer, image_layouts, skybox_texture_names[face_index].c_str());
    }

}

std::vector<uint32_t> BuildMaterialIds(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderScene const& scene,
    SourceTextureManager& texture_manager, SourceMaterialManager& material_manager)
{
    std::vector<uint32_t> material_ids(scene.materials.size() + 1, 0);
    for (size_t material_index = 0; material_index < scene.materials.size(); ++material_index)
    {
        material_ids[material_index + 1] = material_manager.LoadMaterial(
            device, cmd_buffer, image_layouts, texture_manager, scene.materials[material_index].material_name.c_str());
    }

    return material_ids;
}

std::vector<Material> BuildUploadedMaterials(std::vector<uint32_t> const& material_ids,
    SourceMaterialManager const& material_manager)
{
    std::vector<Material> upload_materials(material_ids.size());
    for (size_t material_index = 0; material_index < material_ids.size(); ++material_index)
    {
        upload_materials[material_index] = material_manager.GetMaterial(material_ids[material_index]);
    }

    return upload_materials;
}

void SyncRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderScene& scene,
    SourceTextureManager& texture_manager, SourceMaterialManager& material_manager)
{
    scene.EnsureFallbackTextures(device, cmd_buffer, image_layouts);

    if (!scene.lightmap_atlas.pixels.empty() && scene.lightmap_atlas.width > 0 && scene.lightmap_atlas.height > 0)
    {
        gpu::ImageFormat lightmap_format = scene.lightmap_atlas.format == LightmapAtlas::Format::kRGBA32Float
            ? gpu::ImageFormat::kRGBA32_Float
            : gpu::ImageFormat::kRGBA8_UNorm;
        scene.lightmap_texture = CreateTextureImage(device, cmd_buffer, image_layouts,
            static_cast<uint32_t>(scene.lightmap_atlas.width), static_cast<uint32_t>(scene.lightmap_atlas.height),
            lightmap_format, scene.lightmap_atlas.pixels.data(), scene.lightmap_atlas.pixels.size());
    }
    else
    {
        scene.lightmap_texture = scene.fallback_lightmap_texture;
    }

    scene.material_ids = BuildMaterialIds(device, cmd_buffer, image_layouts, scene, texture_manager, material_manager);
    scene.gpu_materials.Clear();
    scene.gpu_materials.Append(BuildUploadedMaterials(scene.material_ids, material_manager));

    scene.transforms.Sync(device, cmd_buffer);
    scene.instances.Sync(device, cmd_buffer);
    scene.vertex_colors.Sync(device, cmd_buffer);
    scene.gpu_materials.Sync(device, cmd_buffer);
    scene.vertices.Sync(device, cmd_buffer);
    scene.indices.Sync(device, cmd_buffer);
}
