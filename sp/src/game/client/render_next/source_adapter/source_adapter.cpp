#include "cbase.h"
#include "source_adapter.h"

#include "../bsp_loader.h"
#include "engine/ivmodelinfo.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "movevars_shared.h"
#include "texture_group_names.h"
#include "tier2/tier2.h"

#include <algorithm>
#include <array>
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

char const* SourceAdapter::GetLevelName() const
{
    return engine ? engine->GetLevelName() : "";
}

char const* SourceAdapter::GetSkyName() const
{
    return sv_skyname.GetString();
}

void SourceAdapter::GetSkyboxTextureNames(std::array<std::string, 6>& out_texture_names) const
{
    out_texture_names.fill({});

    std::string sky_name = GetSkyName();
    if (sky_name.empty())
    {
        return;
    }

    for (size_t face_index = 0; face_index < out_texture_names.size(); ++face_index)
    {
        out_texture_names[face_index] = ResolveSkyFaceTextureName(("skybox/" + sky_name + kSkyFaceSuffixes[face_index]).c_str());
    }
}

void SourceAdapter::BuildWorldScene(char const* level_name, RenderSceneCpu& out_scene)
{
    char const* resolved_level_name = (level_name && level_name[0]) ? level_name : GetLevelName();
    world_loader_.BuildBaseScene(resolved_level_name, out_scene, build_cache_);

    std::vector<StaticPropInstance> static_props;
    LoadStaticProps(resolved_level_name, static_props);
    if (!static_props.empty())
    {
        std::vector<SourceModelPlacement> model_placements;
        model_placements.reserve(static_props.size());
        for (StaticPropInstance const& static_prop : static_props)
        {
            model_placements.push_back({static_prop.model_name, static_prop.origin, static_prop.angles, static_prop.skin});
        }

        model_manager_.AppendModelPlacements(model_placements, out_scene);
    }

    build_cache_.base_transforms = out_scene.transforms;
    build_cache_.base_instances = out_scene.instances;
}

void SourceAdapter::UpdateRenderableEntities(RenderSceneCpu& scene)
{
    renderable_entity_adapter_.UpdateRenderableEntities(scene, build_cache_);
}
