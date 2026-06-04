#include "cbase.h"
#include "source_brush_entity_adapter.h"

#include "cliententitylist.h"
#include "engine/ivmodelinfo.h"
#include "icliententity.h"
#include "model_types.h"
#include "source_scene_utils.h"

#include <cstdlib>
#include <unordered_map>

namespace
{
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
}

void SourceBrushEntityAdapter::Reset()
{
    dynamic_bindings_.clear();
    brush_entities_initialized_ = false;
}

void SourceBrushEntityAdapter::InitializeBrushEntities(RenderSceneCpu& scene, SourceSceneBuildCache const& build_cache)
{
    if (brush_entities_initialized_ || build_cache.brush_model_ranges.empty() || !cl_entitylist || !modelinfo)
    {
        return;
    }

    int highest_entity_index = cl_entitylist->GetHighestEntityIndex();
    if (highest_entity_index < 0)
    {
        return;
    }

    scene.vertices = build_cache.base_vertices;
    scene.transforms = build_cache.base_transforms;
    scene.instances = build_cache.base_instances;
    dynamic_bindings_.clear();

    std::unordered_map<int, std::vector<BrushModelSourceRange const*>> brush_range_lookup;
    brush_range_lookup.reserve(build_cache.brush_model_ranges.size());
    for (BrushModelSourceRange const& brush_range : build_cache.brush_model_ranges)
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
            if (!brush_range || brush_range->first_vertex + brush_range->vertex_count > build_cache.brush_model_vertices.size())
            {
                continue;
            }

            uint32_t first_vertex = static_cast<uint32_t>(scene.vertices.size());
            for (uint32_t vertex_offset = 0; vertex_offset < brush_range->vertex_count; ++vertex_offset)
            {
                scene.vertices.push_back(build_cache.brush_model_vertices[brush_range->first_vertex + vertex_offset]);
            }

            AddRenderInstance(scene.instances, scene.vertices, first_vertex, brush_range->vertex_count, brush_range->material_index, transform_index);
        }

        ++rendered_brush_entities;
    }

    Msg("render_next: brush entities rendered=%d ranges=%d\n", rendered_brush_entities, build_cache.brush_model_ranges.size());
    brush_entities_initialized_ = true;
}

void SourceBrushEntityAdapter::UpdateDynamicSceneTransforms(RenderSceneCpu& scene)
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
