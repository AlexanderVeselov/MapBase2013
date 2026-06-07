#pragma once

#include "../render_scene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SourceModelPlacement
{
    std::string model_name;
    Vector origin;
    QAngle angles;
    int skin = 0;
};

struct SourceModelInstanceRange
{
    uint32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    uint32_t material_index = 0;
};

struct SourceModelInstanceData
{
    std::vector<SourceModelInstanceRange> mesh_ranges;
};

struct SourceModelSceneCache
{
    std::unordered_map<std::string, uint32_t> material_indices;
    std::unordered_map<std::string, SourceModelInstanceData> instance_data_by_key;
    bool is_initialized = false;

    void Reset()
    {
        material_indices.clear();
        instance_data_by_key.clear();
        is_initialized = false;
    }
};

class SourceModelManager
{
public:
    bool AppendModelByName(char const* model_name, int skin, matrix3x4_t const& model_to_world, RenderSceneCpu& io_scene,
        SourceModelSceneCache* io_scene_cache = nullptr);
    void AppendModelPlacements(std::vector<SourceModelPlacement> const& placements, RenderSceneCpu& io_scene);

private:
    bool AppendLoadedModelGeometry(char const* model_name, int skin, RenderSceneCpu& io_scene, SourceModelSceneCache& io_scene_cache,
        SourceModelInstanceData& out_instance_data);
};
