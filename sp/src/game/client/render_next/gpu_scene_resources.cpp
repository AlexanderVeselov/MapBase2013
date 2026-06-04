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
    gpu::ImageFormat format, void const* data, size_t data_size)
{
    gpu::ImagePtr image =
        device->CreateImage(width, height, format, gpu::ImageFlags::kShaderResource);
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

gpu::ImagePtr LoadTextureImageInternal(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, char const* texture_name)
{
    std::vector<uint8_t> rgba_pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!LoadTextureRgba(texture_name, rgba_pixels, width, height))
    {
        return {};
    }

    return CreateTextureImage(device, cmd_buffer, image_layouts, width, height, gpu::ImageFormat::kRGBA8_UNorm,
        rgba_pixels.data(), rgba_pixels.size());
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

void BuildMaterialResources(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::vector<RenderMaterial> const& scene_materials,
    gpu::ImagePtr const& fallback_texture, RenderSceneGpu& out_gpu_scene)
{
    out_gpu_scene.material_textures.clear();
    out_gpu_scene.material_textures.resize(scene_materials.size() + 1);
    out_gpu_scene.material_textures[0] = fallback_texture;

    for (size_t material_index = 0; material_index < scene_materials.size(); ++material_index)
    {
        if (material_index + 1 >= kMaxMaterialTextures)
        {
            break;
        }

        std::string base_texture_name;
        gpu::ImagePtr texture_image = fallback_texture;
        if (ResolveBaseTextureName(scene_materials[material_index].material_name.c_str(), base_texture_name))
        {
            gpu::ImagePtr loaded_texture = LoadTextureImageInternal(device, cmd_buffer, image_layouts, base_texture_name.c_str());
            if (loaded_texture)
            {
                texture_image = std::move(loaded_texture);
            }
        }

        out_gpu_scene.material_textures[material_index + 1] = texture_image;
    }
}

}

gpu::ImagePtr LoadTextureImage(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, char const* texture_name)
{
    return LoadTextureImageInternal(device, cmd_buffer, image_layouts, texture_name);
}

void RenderSceneGpu::EnsureFallbackTextures(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts)
{
    if (!fallback_texture)
    {
        std::array<uint8_t, 16> fallback_pixels = MakeFallbackTexturePixels();
        fallback_texture = CreateTextureImage(device, cmd_buffer, image_layouts, 2, 2, gpu::ImageFormat::kRGBA8_UNorm,
            fallback_pixels.data(), fallback_pixels.size());
    }

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

    if (!skybox_texture_ids_buffer)
    {
        std::array<uint32_t, 6> fallback_skybox_texture_ids = {};
        skybox_texture_ids_buffer = device->CreateBuffer(sizeof(uint32_t) * fallback_skybox_texture_ids.size(), sizeof(uint32_t),
            gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
        void* skybox_texture_ids_data = skybox_texture_ids_buffer->Map();
        std::memcpy(skybox_texture_ids_data, fallback_skybox_texture_ids.data(), sizeof(uint32_t) * fallback_skybox_texture_ids.size());
        skybox_texture_ids_buffer->Unmap();
    }
}

void UploadSkyboxTexturesToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::array<std::string, 6> const& skybox_texture_names,
    RenderSceneGpu& out_gpu_scene)
{
    out_gpu_scene.EnsureFallbackTextures(device, cmd_buffer, image_layouts);
    out_gpu_scene.EnsureFallbackSceneBuffers(device);

    if (out_gpu_scene.material_textures.empty())
    {
        out_gpu_scene.material_textures.push_back(out_gpu_scene.fallback_texture);
    }
    else
    {
        out_gpu_scene.material_textures[0] = out_gpu_scene.fallback_texture;
    }

    out_gpu_scene.skybox_texture_ids.fill(0);
    for (size_t face_index = 0; face_index < skybox_texture_names.size(); ++face_index)
    {
        if (out_gpu_scene.material_textures.size() >= kMaxMaterialTextures)
        {
            out_gpu_scene.skybox_texture_ids[face_index] = 0;
            continue;
        }

        gpu::ImagePtr sky_face = out_gpu_scene.fallback_texture;
        if (!skybox_texture_names[face_index].empty())
        {
            gpu::ImagePtr loaded_texture = LoadTextureImageInternal(device, cmd_buffer, image_layouts, skybox_texture_names[face_index].c_str());
            if (loaded_texture)
            {
                sky_face = std::move(loaded_texture);
            }
        }

        out_gpu_scene.skybox_texture_ids[face_index] = static_cast<uint32_t>(out_gpu_scene.material_textures.size());
        out_gpu_scene.material_textures.push_back(std::move(sky_face));
    }

    if (!out_gpu_scene.skybox_texture_ids_buffer)
    {
        out_gpu_scene.skybox_texture_ids_buffer = device->CreateBuffer(sizeof(uint32_t) * out_gpu_scene.skybox_texture_ids.size(), sizeof(uint32_t),
            gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    }

    void* skybox_texture_ids_data = out_gpu_scene.skybox_texture_ids_buffer->Map();
    std::memcpy(skybox_texture_ids_data, out_gpu_scene.skybox_texture_ids.data(), sizeof(uint32_t) * out_gpu_scene.skybox_texture_ids.size());
    out_gpu_scene.skybox_texture_ids_buffer->Unmap();
}

void UploadRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu const& scene, RenderSceneGpu& out_gpu_scene)
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

    std::vector<RenderInstance> upload_instances = scene.instances;
    out_gpu_scene.instance_count = static_cast<uint32_t>(upload_instances.size());
    for (RenderInstance& instance : upload_instances)
    {
        if (instance.material_index >= kMaxMaterialTextures || instance.material_index >= scene.materials.size() + 1)
        {
            instance.material_index = 0;
        }
        if (instance.transform_index >= scene.transforms.size())
        {
            instance.transform_index = 0;
        }
        if (instance.vertex_color_offset != RenderInstance::kInvalidVertexColorOffset
            && instance.vertex_color_offset + instance.vertex_count > scene.vertex_colors.size())
        {
            instance.vertex_color_offset = RenderInstance::kInvalidVertexColorOffset;
        }
    }
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

    std::vector<Vertex> upload_vertices = scene.vertices;
    BuildMaterialResources(device, cmd_buffer, image_layouts, scene.materials, out_gpu_scene.fallback_texture, out_gpu_scene);
    UploadVertices(device, upload_vertices, out_gpu_scene);
}
