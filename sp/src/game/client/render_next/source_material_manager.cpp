#include "cbase.h"
#include "source_material_manager.h"

#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "texture_group_names.h"

#include <algorithm>

extern IMaterialSystem* materials;

namespace
{
std::string StripExtension(std::string path)
{
    size_t extension_pos = path.find_last_of('.');
    if (extension_pos != std::string::npos)
    {
        path.erase(extension_pos);
    }
    return path;
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

bool ResolveSupportedAlbedoTextureName(char const* material_name, std::string& out_texture_name)
{
    IMaterial* material = FindNamedMaterial(material_name);
    if (!material)
    {
        return false;
    }

    char const* shader_name = material->GetShaderName();
    if (!shader_name || (V_stricmp(shader_name, "LightmappedGeneric") != 0
            && V_stricmp(shader_name, "VertexLitGeneric") != 0))
    {
        return false;
    }

    bool found = false;
    IMaterialVar* base_texture_var = material->FindVar("$basetexture", &found, false);
    if (!found || !base_texture_var)
    {
        return false;
    }

    ITexture* texture = base_texture_var->GetTextureValue();
    if (texture && !texture->IsError())
    {
        out_texture_name = StripExtension(texture->GetName());
        std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
        return !out_texture_name.empty();
    }

    char const* base_texture_name = base_texture_var->GetStringValue();
    if (!base_texture_name || base_texture_name[0] == '\0')
    {
        return false;
    }

    out_texture_name = StripExtension(base_texture_name);
    std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
    return !out_texture_name.empty();
}
}

void SourceMaterialManager::EnsureFallbackMaterial()
{
    if (!materials_.empty())
    {
        return;
    }

    materials_.push_back(Material{0});
}

uint32_t SourceMaterialManager::LoadMaterial(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, TextureManager& texture_manager, char const* material_name)
{
    EnsureFallbackMaterial();

    if (!material_name || material_name[0] == '\0')
    {
        return 0;
    }

    auto existing = material_ids_by_name_.find(material_name);
    if (existing != material_ids_by_name_.end())
    {
        return existing->second;
    }

    Material material = {};
    std::string albedo_texture_name;
    if (ResolveSupportedAlbedoTextureName(material_name, albedo_texture_name))
    {
        material.albedo_texture_id =
            texture_manager.LoadTexture(device, cmd_buffer, image_layouts, albedo_texture_name.c_str());
    }

    uint32_t material_id = static_cast<uint32_t>(materials_.size());
    materials_.push_back(material);
    material_ids_by_name_.emplace(material_name, material_id);
    return material_id;
}

Material const& SourceMaterialManager::GetMaterial(uint32_t material_id) const
{
    static Material fallback_material = {};

    if (material_id >= materials_.size())
    {
        return materials_.empty() ? fallback_material : materials_[0];
    }

    return materials_[material_id];
}
