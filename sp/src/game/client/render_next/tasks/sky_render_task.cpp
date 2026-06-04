#include "cbase.h"
#include "sky_render_task.h"

#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/itexture.h"
#include "texture_group_names.h"
#include "tier2/tier2.h"

#include <algorithm>
#include <string>

extern IMaterialSystem* materials;

namespace
{
constexpr std::array<char const*, 6> kSkyFaceSuffixes = {"rt", "lf", "bk", "ft", "up", "dn"};

std::string StripExtension(std::string path)
{
    size_t extension_pos = path.find_last_of('.');
    if (extension_pos != std::string::npos)
    {
        path.erase(extension_pos);
    }
    return path;
}

bool TryResolveMaterialTexture(IMaterial* material, char const* var_name, std::string& out_texture_name)
{
    if (!material || !var_name)
    {
        return false;
    }

    bool found = false;
    IMaterialVar* texture_var = material->FindVar(var_name, &found, false);
    if (!found || !texture_var)
    {
        return false;
    }

    ITexture* texture = texture_var->GetTextureValue();
    if (texture && !texture->IsError())
    {
        out_texture_name = StripExtension(texture->GetName());
        std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
        return !out_texture_name.empty();
    }

    char const* texture_name = texture_var->GetStringValue();
    if (!texture_name || texture_name[0] == '\0')
    {
        return false;
    }

    out_texture_name = StripExtension(texture_name);
    std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
    return !out_texture_name.empty();
}

std::string ResolveSkyFaceTextureName(char const* material_name)
{
    if (!material_name || material_name[0] == '\0')
    {
        return {};
    }

    IMaterial* material = materials->FindMaterial(material_name, TEXTURE_GROUP_SKYBOX, false);
    if (!material || IsErrorMaterial(material))
    {
        return StripExtension(material_name);
    }

    std::string texture_name;
    HDRType_t hdr_type = g_pMaterialSystemHardwareConfig ? g_pMaterialSystemHardwareConfig->GetHDRType() : HDR_TYPE_NONE;
    if (hdr_type != HDR_TYPE_NONE)
    {
        if (TryResolveMaterialTexture(material, "$hdrbasetexture", texture_name))
        {
            return texture_name;
        }

        if (TryResolveMaterialTexture(material, "$hdrcompressedtexture", texture_name))
        {
            return texture_name;
        }
    }

    if (TryResolveMaterialTexture(material, "$basetexture", texture_name))
    {
        return texture_name;
    }

    return StripExtension(material_name);
}
}

void SkyRenderTask::Initialize(gpu::DevicePtr const& device, RenderBackendResources const& backend_resources,
    RenderSceneGpu const& gpu_scene)
{
    pipeline_ = device->CreateComputePipeline("render_sky.cs");

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    sky_sampler_ = device->GetSampler(sampler_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
    for (gpu::ImagePtr& sky_face : sky_faces_)
    {
        sky_face = gpu_scene.fallback_texture;
    }
    UpdateBindings(backend_resources, gpu_scene);
}

void SkyRenderTask::LoadSky(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderBackendResources const& backend_resources,
    RenderSceneGpu const& gpu_scene, char const* sky_name)
{
    for (gpu::ImagePtr& sky_face : sky_faces_)
    {
        sky_face = gpu_scene.fallback_texture;
    }

    std::string sky_name_string = sky_name ? sky_name : "";
    if (!sky_name_string.empty())
    {
        for (size_t face_index = 0; face_index < sky_faces_.size(); ++face_index)
        {
            std::string texture_name = ResolveSkyFaceTextureName(("skybox/" + sky_name_string + kSkyFaceSuffixes[face_index]).c_str());
            gpu::ImagePtr sky_face = LoadTextureImage(device, cmd_buffer, image_layouts, texture_name.c_str());
            if (sky_face)
            {
                sky_faces_[face_index] = std::move(sky_face);
            }
        }
    }

    UpdateBindings(backend_resources, gpu_scene);
}

void SkyRenderTask::UpdateBindings(RenderBackendResources const& backend_resources, RenderSceneGpu const& gpu_scene)
{
    std::vector<gpu::ImageDescriptor> image_descriptors(sky_faces_.size());
    for (size_t face_index = 0; face_index < sky_faces_.size(); ++face_index)
    {
        gpu::ImagePtr const& face_image = sky_faces_[face_index] ? sky_faces_[face_index] : gpu_scene.fallback_texture;
        image_descriptors[face_index] = gpu::ImageDescriptor{face_image.get(), {}};
    }

    descriptor_set_->Clear();
    descriptor_set_->BindBuffer(*backend_resources.inverse_view_proj_buffer, 0);
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*sky_sampler_, 0, 2);
    descriptor_set_->BindImage(*backend_resources.color_texture, 6);
}

char const* SkyRenderTask::GetName() const
{
    return "RenderSky";
}

void SkyRenderTask::Execute(RenderTaskContext& context)
{
    TransitionRenderImage(context.backend, context.backend_resources.color_texture, gpu::ImageLayout::kShaderReadWrite);
    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->Dispatch((context.viewport_width + 15) / 16, (context.viewport_height + 15) / 16, 1);
    context.backend.cmd_buffer->StorageBarrier(context.backend_resources.color_texture);
}

