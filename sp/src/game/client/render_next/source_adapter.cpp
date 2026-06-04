#include "cbase.h"
#include "source_adapter.h"

#include "render_scene.h"

#include "cliententitylist.h"
#include "engine/ivmodelinfo.h"
#include "icliententity.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "model_types.h"
#include "movevars_shared.h"
#include "texture_group_names.h"
#include "tier2/tier2.h"

#include <cstdlib>
#include <algorithm>
#include <unordered_map>

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

bool TryParseBrushSubmodelIndex(char const* model_name, int& out_submodel_index)
{
    if (!model_name || model_name[0] != '*')
    {
        return false;
    }

    char* parse_end = nullptr;
    long parsed_value = std::strtol(model_name + 1, &parse_end, 10);
    if (parse_end == model_name + 1 || !parse_end || *parse_end != '\0' || parsed_value <= 0)
    {
        return false;
    }

    out_submodel_index = static_cast<int>(parsed_value);
    return true;
}

uint32_t AddInstance(std::vector<RenderInstance>& out_instances, std::vector<Vertex>& out_vertices, uint32_t first_vertex,
    uint32_t vertex_count, uint32_t material_index, uint32_t transform_index)
{
    RenderInstance instance = {};
    instance.first_vertex = first_vertex;
    instance.vertex_count = vertex_count;
    instance.material_index = material_index;
    instance.transform_index = transform_index;

    uint32_t instance_id = static_cast<uint32_t>(out_instances.size());
    out_instances.push_back(instance);
    for (uint32_t vertex_offset = 0; vertex_offset < vertex_count; ++vertex_offset)
    {
        out_vertices[first_vertex + vertex_offset].instance_id = instance_id;
    }

    return instance_id;
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
    BuildRenderSceneCpu(resolved_level_name, out_scene, build_cache_);
    dynamic_bindings_.clear();
    brush_entities_initialized_ = false;
}

void SourceAdapter::InitializeBrushEntities(RenderSceneCpu& scene)
{
    if (brush_entities_initialized_ || build_cache_.brush_model_ranges.empty() || !cl_entitylist || !modelinfo)
    {
        return;
    }

    int highest_entity_index = cl_entitylist->GetHighestEntityIndex();
    if (highest_entity_index < 0)
    {
        return;
    }

    scene.vertices = build_cache_.base_vertices;
    scene.transforms = build_cache_.base_transforms;
    scene.instances = build_cache_.base_instances;
    dynamic_bindings_.clear();

    std::unordered_map<int, std::vector<BrushModelSourceRange const*>> brush_range_lookup;
    brush_range_lookup.reserve(build_cache_.brush_model_ranges.size());
    for (BrushModelSourceRange const& brush_range : build_cache_.brush_model_ranges)
    {
        brush_range_lookup[brush_range.submodel_index].push_back(&brush_range);
    }

    int rendered_brush_entities = 0;
    for (int entity_index = 0; entity_index <= highest_entity_index; ++entity_index)
    {
        IClientEntity* entity = cl_entitylist->GetClientEntity(entity_index);
        if (!entity)
        {
            continue;
        }

        IClientRenderable* renderable = entity->GetClientRenderable();
        if (!renderable || renderable->IsTransparent())
        {
            continue;
        }

        model_t const* model = renderable->GetModel();
        if (!model || modelinfo->GetModelType(model) != mod_brush)
        {
            continue;
        }

        int submodel_index = 0;
        if (!TryParseBrushSubmodelIndex(modelinfo->GetModelName(model), submodel_index))
        {
            continue;
        }

        auto submodel_it = brush_range_lookup.find(submodel_index);
        if (submodel_it == brush_range_lookup.end())
        {
            continue;
        }

        matrix3x4_t model_to_world;
        AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
        uint32_t transform_index = static_cast<uint32_t>(scene.transforms.size());
        scene.transforms.push_back(MakeSceneTransform(model_to_world));
        dynamic_bindings_.push_back({entity_index, transform_index});

        for (BrushModelSourceRange const* brush_range : submodel_it->second)
        {
            if (!brush_range || brush_range->first_vertex + brush_range->vertex_count > build_cache_.brush_model_vertices.size())
            {
                continue;
            }

            uint32_t first_vertex = static_cast<uint32_t>(scene.vertices.size());
            for (uint32_t vertex_offset = 0; vertex_offset < brush_range->vertex_count; ++vertex_offset)
            {
                scene.vertices.push_back(build_cache_.brush_model_vertices[brush_range->first_vertex + vertex_offset]);
            }

            AddInstance(scene.instances, scene.vertices, first_vertex, brush_range->vertex_count, brush_range->material_index, transform_index);
        }

        ++rendered_brush_entities;
    }

    Msg("render_next: brush entities rendered=%d ranges=%d\n", rendered_brush_entities, build_cache_.brush_model_ranges.size());
    brush_entities_initialized_ = true;
}

void SourceAdapter::UpdateDynamicSceneTransforms(RenderSceneCpu& scene)
{
    if (scene.transforms.empty() || dynamic_bindings_.empty() || !cl_entitylist)
    {
        return;
    }

    for (DynamicTransformBinding const& binding : dynamic_bindings_)
    {
        if (binding.transform_index >= scene.transforms.size())
        {
            continue;
        }

        IClientEntity* entity = cl_entitylist->GetClientEntity(binding.entity_index);
        if (!entity)
        {
            scene.transforms[binding.transform_index] = MakeIdentitySceneTransform();
            continue;
        }

        IClientRenderable* renderable = entity->GetClientRenderable();
        if (!renderable)
        {
            scene.transforms[binding.transform_index] = MakeIdentitySceneTransform();
            continue;
        }

        matrix3x4_t model_to_world;
        AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
        scene.transforms[binding.transform_index] = MakeSceneTransform(model_to_world);
    }
}
