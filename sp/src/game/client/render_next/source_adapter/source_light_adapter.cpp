#include "cbase.h"
#include "source_light_adapter.h"

#include "../render_scene.h"
#include "bspfile.h"
#include "filesystem.h"
#include "mathlib/vector.h"
#include "tier1/utlbuffer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
template<typename T>
bool ReadLump(uint8_t const* file_data, size_t file_size, lump_t const& lump, std::vector<T>& out)
{
    if (lump.fileofs <= 0 || lump.filelen <= 0)
    {
        out.clear();
        return true;
    }

    if (lump.filelen % static_cast<int>(sizeof(T)) != 0)
    {
        return false;
    }

    if (static_cast<size_t>(lump.fileofs) + static_cast<size_t>(lump.filelen) > file_size)
    {
        return false;
    }

    size_t count = static_cast<size_t>(lump.filelen) / sizeof(T);
    out.resize(count);
    std::memcpy(out.data(), file_data + lump.fileofs, static_cast<size_t>(lump.filelen));
    return true;
}

std::vector<std::string> GetBspFilesystemCandidates(char const* filename)
{
    std::vector<std::string> candidates;
    if (!filename || filename[0] == '\0')
    {
        return candidates;
    }

    std::string name = filename;
    candidates.push_back(name);

    bool has_maps_prefix = name.rfind("maps/", 0) == 0 || name.rfind("maps\\", 0) == 0;
    if (!has_maps_prefix)
    {
        candidates.push_back("maps/" + name);
    }

    bool has_extension = name.size() >= 4 && _stricmp(name.c_str() + name.size() - 4, ".bsp") == 0;
    if (!has_extension)
    {
        candidates.push_back(name + ".bsp");
        if (!has_maps_prefix)
        {
            candidates.push_back("maps/" + name + ".bsp");
        }
    }

    return candidates;
}

bool TryReadBspFile(char const* filename, CUtlBuffer& out_buffer)
{
    std::vector<std::string> candidates = GetBspFilesystemCandidates(filename);
    for (std::string const& candidate : candidates)
    {
        out_buffer.Clear();
        if (g_pFullFileSystem->ReadFile(candidate.c_str(), "GAME", out_buffer))
        {
            return true;
        }
    }

    return false;
}

bool LoadBspHeader(uint8_t const* file_data, size_t file_size, dheader_t& out_header)
{
    if (!file_data || file_size < sizeof(dheader_t))
    {
        return false;
    }

    std::memcpy(&out_header, file_data, sizeof(dheader_t));
    if (out_header.ident != IDBSPHEADER)
    {
        return false;
    }

    return out_header.version >= MINBSPVERSION && out_header.version <= BSPVERSION;
}

uint32_t ConvertLightType(emittype_t light_type, bool& out_supported)
{
    out_supported = true;
    switch (light_type)
    {
    case emit_skylight:
        return SceneLight::kDirectional;
    case emit_point:
        return SceneLight::kPoint;
    case emit_spotlight:
        return SceneLight::kSpot;
    case emit_surface:
        return SceneLight::kSurface;
    default:
        out_supported = false;
        return SceneLight::kDirectional;
    }
}

SceneLight ConvertWorldLight(dworldlight_t const& world_light)
{
    SceneLight scene_light;
    bool supported = false;
    scene_light.type = ConvertLightType(world_light.type, supported);
    if (!supported)
    {
        scene_light.type = UINT32_MAX;
        return scene_light;
    }

    scene_light.radius = world_light.radius;
    scene_light.stopdot = world_light.stopdot;
    scene_light.stopdot2 = world_light.stopdot2;
    scene_light.radiance[0] = world_light.intensity.x;
    scene_light.radiance[1] = world_light.intensity.y;
    scene_light.radiance[2] = world_light.intensity.z;
    scene_light.exponent = world_light.exponent;
    scene_light.position[0] = world_light.origin.x;
    scene_light.position[1] = world_light.origin.y;
    scene_light.position[2] = world_light.origin.z;
    scene_light.constant_attn = world_light.constant_attn;

    Vector direction = world_light.normal;
    if (direction.LengthSqr() > 1e-12f)
    {
        VectorNormalize(direction);
    }
    else
    {
        direction.Init(0.0f, 0.0f, -1.0f);
    }

    scene_light.direction[0] = direction.x;
    scene_light.direction[1] = direction.y;
    scene_light.direction[2] = direction.z;
    scene_light.linear_attn = world_light.linear_attn;
    scene_light.quadratic_attn = world_light.quadratic_attn;
    return scene_light;
}
}

void SourceLightAdapter::LoadWorldLights(char const* level_name, RenderScene& io_scene) const
{
    io_scene.lights.Clear();

    CUtlBuffer bsp_buffer;
    if (!TryReadBspFile(level_name, bsp_buffer))
    {
        return;
    }

    uint8_t const* file_data = static_cast<uint8_t const*>(bsp_buffer.Base());
    size_t file_size = static_cast<size_t>(bsp_buffer.TellPut());
    dheader_t header = {};
    if (!LoadBspHeader(file_data, file_size, header))
    {
        return;
    }

    std::vector<dworldlight_t> world_lights;
    if (!ReadLump(file_data, file_size, header.lumps[LUMP_WORLDLIGHTS_HDR], world_lights) || world_lights.empty())
    {
        if (!ReadLump(file_data, file_size, header.lumps[LUMP_WORLDLIGHTS], world_lights))
        {
            return;
        }
    }

    for (dworldlight_t const& world_light : world_lights)
    {
        SceneLight scene_light = ConvertWorldLight(world_light);
        if (scene_light.type == UINT32_MAX)
        {
            continue;
        }

        io_scene.lights.Append(scene_light);
    }
}
