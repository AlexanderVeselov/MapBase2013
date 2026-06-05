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
    brush_entities_initialized_ = false;
}

void SourceRenderableEntityAdapter::PopulateRenderableEntities(RenderSceneCpu& scene, SourceSceneBuildCache const& build_cache)
{
    if (brush_entities_initialized_ || build_cache.brush_model_ranges.empty())
    {
        return;
    }
    renderable_entities_.clear();

    std::unordered_map<int, std::vector<BrushModelSourceRange const*>> brush_range_lookup;
    brush_range_lookup.reserve(build_cache.brush_model_ranges.size());
    for (BrushModelSourceRange const& brush_range : build_cache.brush_model_ranges)
    {
        brush_range_lookup[brush_range.submodel_index].push_back(&brush_range);
    }

    int populated_renderable_entities = 0;
    for (auto const& brush_submodel_entry : brush_range_lookup)
    {
        int submodel_index = brush_submodel_entry.first;
        std::vector<BrushModelSourceRange const*> const& submodel_ranges = brush_submodel_entry.second;
        if (submodel_ranges.empty())
        {
            continue;
        }

        uint32_t transform_index = static_cast<uint32_t>(scene.transforms.size());
        scene.transforms.push_back(MakeIdentitySceneTransform());
        uint32_t first_instance = static_cast<uint32_t>(scene.instances.size());
        uint32_t instance_count = 0;

        for (BrushModelSourceRange const* brush_range : submodel_ranges)
        {
            if (!brush_range || brush_range->first_vertex > scene.vertices.size()
                || brush_range->first_index + brush_range->index_count > scene.indices.size())
            {
                continue;
            }

            uint32_t instance_index = AddRenderInstance(scene.instances, brush_range->first_vertex, brush_range->first_index, brush_range->index_count,
                brush_range->material_index, transform_index);
            scene.instances[instance_index].is_visible = RenderInstance::kHidden;
            ++instance_count;
        }

        if (instance_count == 0)
        {
            scene.transforms.pop_back();
            continue;
        }

        renderable_entities_.push_back({-1, submodel_index, transform_index, first_instance, instance_count});

        ++populated_renderable_entities;
    }

    Msg("render_next: populated brush renderable entities=%d ranges=%d\n", populated_renderable_entities,
        build_cache.brush_model_ranges.size());
    brush_entities_initialized_ = true;
}

void SourceRenderableEntityAdapter::UpdateRenderableEntities(RenderSceneCpu& scene)
{
    if (scene.transforms.empty() || renderable_entities_.empty() || !cl_entitylist)
    {
        return;
    }

    for (RenderableEntity& renderable_entity : renderable_entities_)
    {
        renderable_entity.entity_index = -1;
        if (renderable_entity.transform_index >= scene.transforms.size())
        {
            continue;
        }

        SetRenderableEntityVisibility(scene, renderable_entity, RenderInstance::kHidden);
        scene.transforms[renderable_entity.transform_index] = MakeIdentitySceneTransform();
    }

    if (!modelinfo)
    {
        return;
    }

    int highest_entity_index = cl_entitylist->GetHighestEntityIndex();
    if (highest_entity_index < 0)
    {
        return;
    }

    std::unordered_map<int, RenderableEntity*> renderable_lookup;
    renderable_lookup.reserve(renderable_entities_.size());
    for (RenderableEntity& renderable_entity : renderable_entities_)
    {
        renderable_lookup[renderable_entity.submodel_index] = &renderable_entity;
    }

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
        int submodel_index = 0;
        if (!model || modelinfo->GetModelType(model) != mod_brush
            || !TryParseBrushSubmodelIndex(modelinfo->GetModelName(model), submodel_index))
        {
            continue;
        }

        auto renderable_it = renderable_lookup.find(submodel_index);
        if (renderable_it == renderable_lookup.end() || !renderable_it->second)
        {
            continue;
        }

        RenderableEntity& renderable_entity = *renderable_it->second;
        if (renderable_entity.entity_index != -1 || renderable_entity.transform_index >= scene.transforms.size())
        {
            continue;
        }

        matrix3x4_t model_to_world;
        AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
        renderable_entity.entity_index = entity_index;
        SetRenderableEntityVisibility(scene, renderable_entity, RenderInstance::kVisible);
        scene.transforms[renderable_entity.transform_index] = MakeSceneTransform(model_to_world);
    }
}

void SourceRenderableEntityAdapter::SetRenderableEntityVisibility(RenderSceneCpu& scene,
    RenderableEntity const& renderable_entity, uint32_t visibility)
{
    uint32_t end_instance = renderable_entity.first_instance + renderable_entity.instance_count;
    if (renderable_entity.first_instance >= scene.instances.size() || end_instance > scene.instances.size())
    {
        return;
    }

    for (uint32_t instance_index = renderable_entity.first_instance; instance_index < end_instance; ++instance_index)
    {
        scene.instances[instance_index].is_visible = visibility;
    }
}
