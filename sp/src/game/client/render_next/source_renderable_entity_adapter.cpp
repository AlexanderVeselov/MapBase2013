#include "cbase.h"
#include "source_renderable_entity_adapter.h"

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

void SourceRenderableEntityAdapter::Reset()
{
    renderable_entities_.clear();
}

void SourceRenderableEntityAdapter::UpdateRenderableEntities(RenderSceneCpu& scene, SourceSceneBuildCache const& build_cache)
{
    scene.transforms = build_cache.base_transforms;
    scene.instances = build_cache.base_instances;
    renderable_entities_.clear();

    if (build_cache.brush_model_ranges.empty() || !cl_entitylist || !modelinfo)
    {
        return;
    }

    std::unordered_map<int, std::vector<BrushModelSourceRange const*>> brush_range_lookup;
    brush_range_lookup.reserve(build_cache.brush_model_ranges.size());
    for (BrushModelSourceRange const& brush_range : build_cache.brush_model_ranges)
    {
        brush_range_lookup[brush_range.submodel_index].push_back(&brush_range);
    }

    int highest_entity_index = cl_entitylist->GetHighestEntityIndex();
    if (highest_entity_index < 0)
    {
        return;
    }

    for (int entity_index = 0; entity_index <= highest_entity_index; ++entity_index)
    {
        IClientEntity* entity = cl_entitylist->GetClientEntity(entity_index);
        if (!entity || entity->IsDormant())
        {
            continue;
        }

        IClientRenderable* renderable = entity->GetClientRenderable();
        if (!renderable || !renderable->ShouldDraw() || renderable->IsTransparent())
        {
            continue;
        }

        model_t const* model = renderable->GetModel();
        int submodel_index = 0;
        if (!model || modelinfo->GetModelType(model) != mod_brush
            || !TryParseBrushSubmodelIndex(modelinfo->GetModelName(model), submodel_index))
        {
            continue;
        }

        auto submodel_it = brush_range_lookup.find(submodel_index);
        if (submodel_it == brush_range_lookup.end() || submodel_it->second.empty())
        {
            continue;
        }

        matrix3x4_t model_to_world;
        AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
        uint32_t transform_index = static_cast<uint32_t>(scene.transforms.size());
        scene.transforms.push_back(MakeSceneTransform(model_to_world));

        uint32_t first_instance = static_cast<uint32_t>(scene.instances.size());
        uint32_t instance_count = 0;
        for (BrushModelSourceRange const* brush_range : submodel_it->second)
        {
            if (!brush_range || brush_range->first_vertex > scene.vertices.size()
                || brush_range->first_index + brush_range->index_count > scene.indices.size())
            {
                continue;
            }

            AddRenderInstance(scene.instances, brush_range->first_vertex, brush_range->first_index, brush_range->index_count,
                brush_range->material_index, transform_index);
            ++instance_count;
        }

        if (instance_count == 0)
        {
            scene.transforms.pop_back();
            continue;
        }

        renderable_entities_.push_back({entity_index, submodel_index, transform_index, first_instance, instance_count});
    }
}
