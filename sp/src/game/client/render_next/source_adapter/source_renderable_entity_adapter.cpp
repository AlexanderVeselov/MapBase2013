#include "cbase.h"
#include "source_renderable_entity_adapter.h"

#include "c_baseanimating.h"
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

void SourceRenderableEntityAdapter::UpdateRenderableEntities(RenderScene& scene, SourceSceneBuildCache const& build_cache,
    SourceModelManager& model_manager, gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager)
{
    scene.transforms.Clear();
    scene.transforms.Append(build_cache.static_transforms);
    scene.bones.Clear();
    scene.vertex_colors.Clear();
    scene.vertex_colors.Append(build_cache.static_vertex_colors);
    scene.instances.Clear();
    scene.instances.Append(build_cache.static_instances);

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
        if (!model)
        {
            continue;
        }

        modtype_t model_type = static_cast<modtype_t>(modelinfo->GetModelType(model));
        uint32_t transform_index = 0;
        uint32_t instance_count = 0;
        int submodel_index = 0;

        if (model_type == mod_studio)
        {
            char const* model_name = modelinfo->GetModelName(model);
            if (!model_name || model_name[0] == '\0')
            {
                continue;
            }

            matrix3x4_t model_to_world;
            AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
            uint32_t bone_offset = RenderInstance::kInvalidBoneOffset;
            uint32_t bone_count = 0;
            IClientUnknown* client_unknown = renderable->GetIClientUnknown();
            C_BaseEntity* base_entity = client_unknown ? client_unknown->GetBaseEntity() : nullptr;
            if (base_entity)
            {
                if (C_BaseAnimating* base_animating = base_entity->GetBaseAnimating())
                {
                    CStudioHdr* studio_hdr = base_animating->GetModelPtr();
                    if (studio_hdr)
                    {
                        matrix3x4_t bone_to_world[MAXSTUDIOBONES];
                        if (base_animating->SetupBones(bone_to_world, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime))
                        {
                            bone_count = static_cast<uint32_t>((std::min)(studio_hdr->numbones(), static_cast<int>(MAXSTUDIOBONES)));
                            bone_offset = scene.bones.Size();
                            for (uint32_t bone_index = 0; bone_index < bone_count; ++bone_index)
                            {
                                scene.bones.Append(MakeSceneBoneMatrix(bone_to_world[bone_index]));
                            }
                        }
                    }
                }
            }

            if (!model_manager.LoadModel(model_name, renderable->GetSkin(), model_to_world, false, scene, bone_offset, bone_count, device, cmd_buffer,
                    image_layouts, texture_manager, material_manager))
            {
                continue;
            }
        }
        else if (model_type == mod_brush)
        {
            if (!TryParseBrushSubmodelIndex(modelinfo->GetModelName(model), submodel_index))
            {
                continue;
            }

            auto submodel_it = brush_range_lookup.find(submodel_index);
            if (submodel_it == brush_range_lookup.end() || submodel_it->second.empty())
            {
                continue;
            }

            bool has_valid_range = false;
            for (BrushModelSourceRange const* brush_range : submodel_it->second)
            {
                if (brush_range && brush_range->first_vertex <= scene.vertices.Size()
                    && brush_range->first_index + brush_range->index_count <= scene.indices.Size())
                {
                    has_valid_range = true;
                    break;
                }
            }

            if (!has_valid_range)
            {
                continue;
            }

            matrix3x4_t model_to_world;
            AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
            transform_index = scene.transforms.Append(MakeSceneTransform(model_to_world)).offset;

            for (BrushModelSourceRange const* brush_range : submodel_it->second)
            {
                if (!brush_range || brush_range->first_vertex > scene.vertices.Size()
                    || brush_range->first_index + brush_range->index_count > scene.indices.Size())
                {
                    continue;
                }

                AddRenderInstance(scene.instances, brush_range->first_vertex, brush_range->first_index,
                    brush_range->index_count, brush_range->material_index, transform_index);
                ++instance_count;
            }

            if (instance_count == 0)
            {
                continue;
            }
        }
    }
}
