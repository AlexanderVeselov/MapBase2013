#include "bsp_loader.h"
#include "render_scene.h"
#include "source_adapter/source_model_manager.h"
#include "source_adapter/source_scene_utils.h"
#include "bspfile.h"
#include "gamebspfile.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

template<typename T>
bool ReadLump(std::ifstream& f, lump_t const& l, std::vector<T>& out)
{
    if (l.fileofs <= 0 || l.filelen <= 0) return true; // empty is ok
    if (l.filelen % (int)sizeof(T) != 0) return false;

    const size_t count = (size_t)l.filelen / sizeof(T);
    out.resize(count);

    f.seekg(l.fileofs, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), (std::streamsize)l.filelen);
    return f.good() && !out.empty();
}

inline void CalcUV(const Vector& p, const texinfo_t& ti, float uv[2])
{
    Vector uAxis(ti.textureVecsTexelsPerWorldUnits[0][0], ti.textureVecsTexelsPerWorldUnits[0][1], ti.textureVecsTexelsPerWorldUnits[0][2]);
    Vector vAxis(ti.textureVecsTexelsPerWorldUnits[1][0], ti.textureVecsTexelsPerWorldUnits[1][1], ti.textureVecsTexelsPerWorldUnits[1][2]);

    uv[0] = p.Dot(uAxis) + ti.textureVecsTexelsPerWorldUnits[0][3];
    uv[1] = p.Dot(vAxis) + ti.textureVecsTexelsPerWorldUnits[1][3];
}

inline void CalcLightmapUV(const Vector& p, const texinfo_t& ti, const dface_t& face, float uv[2])
{
    Vector uAxis(ti.lightmapVecsLuxelsPerWorldUnits[0][0], ti.lightmapVecsLuxelsPerWorldUnits[0][1], ti.lightmapVecsLuxelsPerWorldUnits[0][2]);
    Vector vAxis(ti.lightmapVecsLuxelsPerWorldUnits[1][0], ti.lightmapVecsLuxelsPerWorldUnits[1][1], ti.lightmapVecsLuxelsPerWorldUnits[1][2]);

    uv[0] = p.Dot(uAxis) + ti.lightmapVecsLuxelsPerWorldUnits[0][3] - static_cast<float>(face.m_LightmapTextureMinsInLuxels[0]);
    uv[1] = p.Dot(vAxis) + ti.lightmapVecsLuxelsPerWorldUnits[1][3] - static_cast<float>(face.m_LightmapTextureMinsInLuxels[1]);
}

struct PackedLightmapRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

PackedLightmapRect PackLightmapRect(int width, int height, int max_row_width, int& cursor_x, int& cursor_y,
    int& current_row_height, int& atlas_width, int& atlas_height)
{
    PackedLightmapRect rect;
    if (width <= 0 || height <= 0)
    {
        return rect;
    }

    if (cursor_x + width > max_row_width)
    {
        cursor_x = 0;
        cursor_y += current_row_height;
        current_row_height = 0;
    }

    rect.x = cursor_x;
    rect.y = cursor_y;
    rect.width = width;
    rect.height = height;
    rect.valid = true;

    cursor_x += width;
    current_row_height = (std::max)(current_row_height, height);
    atlas_width = (std::max)(atlas_width, cursor_x);
    atlas_height = (std::max)(atlas_height, cursor_y + current_row_height);
    return rect;
}

float DecodeLightmapComponent(const ColorRGBExp32& lightmap_color, int component_index)
{
    int component = component_index == 0 ? lightmap_color.r : (component_index == 1 ? lightmap_color.g : lightmap_color.b);
    return TexLightToLinear(component, lightmap_color.exponent);
}

void WriteAtlasPixel(std::vector<uint8_t>& atlas_pixels, int atlas_width, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    size_t pixel_index = static_cast<size_t>(y * atlas_width + x) * 4;
    atlas_pixels[pixel_index + 0] = r;
    atlas_pixels[pixel_index + 1] = g;
    atlas_pixels[pixel_index + 2] = b;
    atlas_pixels[pixel_index + 3] = 255;
}

void WriteAtlasPixel(std::vector<float>& atlas_pixels, int atlas_width, int x, int y, float r, float g, float b)
{
    size_t pixel_index = static_cast<size_t>(y * atlas_width + x) * 4;
    atlas_pixels[pixel_index + 0] = r;
    atlas_pixels[pixel_index + 1] = g;
    atlas_pixels[pixel_index + 2] = b;
    atlas_pixels[pixel_index + 3] = 1.0f;
}

char const* GetTexdataString(std::vector<int32_t> const& string_table, std::vector<char> const& string_data,
    int name_string_table_id)
{
    if (name_string_table_id < 0 || name_string_table_id >= static_cast<int>(string_table.size()))
    {
        return nullptr;
    }

    int string_offset = string_table[name_string_table_id];
    if (string_offset < 0 || string_offset >= static_cast<int>(string_data.size()))
    {
        return nullptr;
    }

    return string_data.data() + string_offset;
}

bool GetFaceQuadPoints(dface_t const& face, std::vector<int32_t> const& surfedges, std::vector<dedge_t> const& edges,
    std::vector<dvertex_t> const& vertexes, Vector out_points[4])
{
    if (face.numedges != 4)
    {
        return false;
    }

    if (face.firstedge < 0 || face.firstedge + face.numedges > static_cast<int>(surfedges.size()))
    {
        return false;
    }

    for (int point_index = 0; point_index < 4; ++point_index)
    {
        int32_t surfedge = surfedges[face.firstedge + point_index];
        int edge_index = std::abs(surfedge);
        if (edge_index < 0 || edge_index >= static_cast<int>(edges.size()))
        {
            return false;
        }

        int vertex_slot = surfedge < 0 ? 1 : 0;
        uint16_t vertex_index = edges[edge_index].v[vertex_slot];
        if (vertex_index >= vertexes.size())
        {
            return false;
        }

        out_points[point_index] = vertexes[vertex_index].point;
    }

    return true;
}

int FindNearestQuadCorner(Vector const& start_position, Vector const points[4])
{
    int best_index = 0;
    float best_distance_sq = FLT_MAX;
    for (int point_index = 0; point_index < 4; ++point_index)
    {
        Vector delta = points[point_index] - start_position;
        float distance_sq = delta.Dot(delta);
        if (distance_sq < best_distance_sq)
        {
            best_distance_sq = distance_sq;
            best_index = point_index;
        }
    }

    return best_index;
}

Vector BilerpVector(Vector const& p00, Vector const& p01, Vector const& p10, Vector const& p11, float s, float t)
{
    return p00 * ((1.0f - s) * (1.0f - t))
        + p01 * ((1.0f - s) * t)
        + p10 * (s * (1.0f - t))
        + p11 * (s * t);
}

void BilerpUV(float const uv00[2], float const uv01[2], float const uv10[2], float const uv11[2], float s, float t, float out_uv[2])
{
    out_uv[0] = uv00[0] * ((1.0f - s) * (1.0f - t))
        + uv01[0] * ((1.0f - s) * t)
        + uv10[0] * (s * (1.0f - t))
        + uv11[0] * (s * t);
    out_uv[1] = uv00[1] * ((1.0f - s) * (1.0f - t))
        + uv01[1] * ((1.0f - s) * t)
        + uv10[1] * (s * (1.0f - t))
        + uv11[1] * (s * t);
}

struct BspLoadData
{
    std::vector<dvertex_t> vertexes;
    std::vector<dedge_t> edges;
    std::vector<int32_t> surfedges;
    std::vector<dface_t> faces;
    std::vector<dmodel_t> models;
    std::vector<texinfo_t> texinfo;
    std::vector<dtexdata_t> texdata;
    std::vector<ddispinfo_t> dispinfo;
    std::vector<CDispVert> dispverts;
    std::vector<CDispTri> disptris;
    std::vector<ColorRGBExp32> lighting;
    std::vector<ColorRGBExp32> lighting_hdr;
    std::vector<int32_t> texdata_string_table;
    std::vector<char> texdata_string_data;
};

bool LoadBspHeader(std::ifstream& file, dheader_t& out_header)
{
    file.read(reinterpret_cast<char*>(&out_header), sizeof(dheader_t));
    if (!file.good())
    {
        return false;
    }

    if (out_header.ident != IDBSPHEADER)
    {
        return false;
    }

    return out_header.version >= MINBSPVERSION && out_header.version <= BSPVERSION;
}

bool LoadBspGeometryLumps(std::ifstream& file, dheader_t const& header, BspLoadData& out_data)
{
    if (!ReadLump(file, header.lumps[LUMP_VERTEXES], out_data.vertexes)) return false;
    if (!ReadLump(file, header.lumps[LUMP_EDGES], out_data.edges)) return false;
    if (!ReadLump(file, header.lumps[LUMP_SURFEDGES], out_data.surfedges)) return false;
    if (!ReadLump(file, header.lumps[LUMP_FACES], out_data.faces)) return false;
    if (!ReadLump(file, header.lumps[LUMP_MODELS], out_data.models)) return false;
    return true;
}

bool LoadBspMaterialLumps(std::ifstream& file, dheader_t const& header, BspLoadData& out_data)
{
    if (!ReadLump(file, header.lumps[LUMP_TEXINFO], out_data.texinfo)) return false;
    if (!ReadLump(file, header.lumps[LUMP_TEXDATA], out_data.texdata)) return false;
    if (!ReadLump(file, header.lumps[LUMP_TEXDATA_STRING_TABLE], out_data.texdata_string_table)) return false;
    if (!ReadLump(file, header.lumps[LUMP_TEXDATA_STRING_DATA], out_data.texdata_string_data)) return false;
    return true;
}

bool LoadBspDisplacementLumps(std::ifstream& file, dheader_t const& header, BspLoadData& out_data)
{
    if (!ReadLump(file, header.lumps[LUMP_DISPINFO], out_data.dispinfo)) return false;
    if (!ReadLump(file, header.lumps[LUMP_DISP_VERTS], out_data.dispverts)) return false;
    if (!ReadLump(file, header.lumps[LUMP_DISP_TRIS], out_data.disptris)) return false;
    return true;
}

bool LoadBspLightingLumps(std::ifstream& file, dheader_t const& header, BspLoadData& out_data)
{
    if (!ReadLump(file, header.lumps[LUMP_LIGHTING], out_data.lighting)) return false;
    if (!ReadLump(file, header.lumps[LUMP_LIGHTING_HDR], out_data.lighting_hdr)) return false;
    return true;
}

void InitializeFallbackLightmapAtlas(LightmapAtlas& out_lightmap_atlas)
{
    out_lightmap_atlas.width = 1;
    out_lightmap_atlas.height = 1;
    out_lightmap_atlas.format = LightmapAtlas::Format::kRGBA8;
    out_lightmap_atlas.pixels = {255, 255, 255, 255};
}

std::vector<PackedLightmapRect> BuildLightmapAtlas(BspLoadData const& bsp, LightmapAtlas& out_lightmap_atlas)
{
    std::vector<ColorRGBExp32> const& active_lighting = bsp.lighting_hdr.empty() ? bsp.lighting : bsp.lighting_hdr;
    bool const use_hdr_lightmaps = !bsp.lighting_hdr.empty();

    std::vector<PackedLightmapRect> face_lightmap_rects(bsp.faces.size());
    constexpr int kAtlasMaxRowWidth = 2048;
    int atlas_cursor_x = 1;
    int atlas_cursor_y = 0;
    int atlas_row_height = 0;
    int atlas_width = 1;
    int atlas_height = 1;

    for (size_t face_index = 0; face_index < bsp.faces.size(); ++face_index)
    {
        dface_t const& face = bsp.faces[face_index];
        if (face.lightofs < 0 || face.styles[0] == 255)
        {
            continue;
        }

        int lightmap_width = face.m_LightmapTextureSizeInLuxels[0] + 1;
        int lightmap_height = face.m_LightmapTextureSizeInLuxels[1] + 1;
        if (lightmap_width <= 0 || lightmap_height <= 0)
        {
            continue;
        }

        face_lightmap_rects[face_index] = PackLightmapRect(lightmap_width + 2, lightmap_height + 2, kAtlasMaxRowWidth,
            atlas_cursor_x, atlas_cursor_y, atlas_row_height, atlas_width, atlas_height);
    }

    std::vector<uint8_t> atlas_pixels_ldr;
    std::vector<float> atlas_pixels_hdr;
    if (use_hdr_lightmaps)
    {
        atlas_pixels_hdr.resize(static_cast<size_t>(atlas_width * atlas_height) * 4, 1.0f);
    }
    else
    {
        atlas_pixels_ldr.resize(static_cast<size_t>(atlas_width * atlas_height) * 4, 255);
    }

    for (size_t face_index = 0; face_index < bsp.faces.size(); ++face_index)
    {
        PackedLightmapRect const& packed_rect = face_lightmap_rects[face_index];
        if (!packed_rect.valid)
        {
            continue;
        }

        dface_t const& face = bsp.faces[face_index];
        int lightmap_width = face.m_LightmapTextureSizeInLuxels[0] + 1;
        int lightmap_height = face.m_LightmapTextureSizeInLuxels[1] + 1;
        size_t sample_offset = static_cast<size_t>(face.lightofs) / sizeof(ColorRGBExp32);
        size_t sample_count = static_cast<size_t>(lightmap_width * lightmap_height);
        if (sample_offset + sample_count > active_lighting.size())
        {
            continue;
        }

        for (int y = 0; y < lightmap_height; ++y)
        {
            for (int x = 0; x < lightmap_width; ++x)
            {
                ColorRGBExp32 const& lightmap_color = active_lighting[sample_offset + static_cast<size_t>(y * lightmap_width + x)];
                float r = DecodeLightmapComponent(lightmap_color, 0);
                float g = DecodeLightmapComponent(lightmap_color, 1);
                float b = DecodeLightmapComponent(lightmap_color, 2);
                if (use_hdr_lightmaps)
                {
                    WriteAtlasPixel(atlas_pixels_hdr, atlas_width, packed_rect.x + 1 + x, packed_rect.y + 1 + y, r, g, b);
                }
                else
                {
                    WriteAtlasPixel(atlas_pixels_ldr, atlas_width, packed_rect.x + 1 + x, packed_rect.y + 1 + y,
                        static_cast<uint8_t>((std::clamp)(r, 0.0f, 1.0f) * 255.0f + 0.5f),
                        static_cast<uint8_t>((std::clamp)(g, 0.0f, 1.0f) * 255.0f + 0.5f),
                        static_cast<uint8_t>((std::clamp)(b, 0.0f, 1.0f) * 255.0f + 0.5f));
                }
            }
        }

        for (int x = 0; x < lightmap_width; ++x)
        {
            size_t top_index = static_cast<size_t>((packed_rect.y + 1) * atlas_width + (packed_rect.x + 1 + x)) * 4;
            size_t bottom_index = static_cast<size_t>((packed_rect.y + lightmap_height) * atlas_width + (packed_rect.x + 1 + x)) * 4;
            if (use_hdr_lightmaps)
            {
                WriteAtlasPixel(atlas_pixels_hdr, atlas_width, packed_rect.x + 1 + x, packed_rect.y,
                    atlas_pixels_hdr[top_index + 0], atlas_pixels_hdr[top_index + 1], atlas_pixels_hdr[top_index + 2]);
                WriteAtlasPixel(atlas_pixels_hdr, atlas_width, packed_rect.x + 1 + x, packed_rect.y + lightmap_height + 1,
                    atlas_pixels_hdr[bottom_index + 0], atlas_pixels_hdr[bottom_index + 1], atlas_pixels_hdr[bottom_index + 2]);
            }
            else
            {
                WriteAtlasPixel(atlas_pixels_ldr, atlas_width, packed_rect.x + 1 + x, packed_rect.y,
                    atlas_pixels_ldr[top_index + 0], atlas_pixels_ldr[top_index + 1], atlas_pixels_ldr[top_index + 2]);
                WriteAtlasPixel(atlas_pixels_ldr, atlas_width, packed_rect.x + 1 + x, packed_rect.y + lightmap_height + 1,
                    atlas_pixels_ldr[bottom_index + 0], atlas_pixels_ldr[bottom_index + 1], atlas_pixels_ldr[bottom_index + 2]);
            }
        }

        for (int y = 0; y < lightmap_height + 2; ++y)
        {
            size_t left_index = static_cast<size_t>((packed_rect.y + y) * atlas_width + (packed_rect.x + 1)) * 4;
            size_t right_index = static_cast<size_t>((packed_rect.y + y) * atlas_width + (packed_rect.x + lightmap_width)) * 4;
            if (use_hdr_lightmaps)
            {
                WriteAtlasPixel(atlas_pixels_hdr, atlas_width, packed_rect.x, packed_rect.y + y,
                    atlas_pixels_hdr[left_index + 0], atlas_pixels_hdr[left_index + 1], atlas_pixels_hdr[left_index + 2]);
                WriteAtlasPixel(atlas_pixels_hdr, atlas_width, packed_rect.x + lightmap_width + 1, packed_rect.y + y,
                    atlas_pixels_hdr[right_index + 0], atlas_pixels_hdr[right_index + 1], atlas_pixels_hdr[right_index + 2]);
            }
            else
            {
                WriteAtlasPixel(atlas_pixels_ldr, atlas_width, packed_rect.x, packed_rect.y + y,
                    atlas_pixels_ldr[left_index + 0], atlas_pixels_ldr[left_index + 1], atlas_pixels_ldr[left_index + 2]);
                WriteAtlasPixel(atlas_pixels_ldr, atlas_width, packed_rect.x + lightmap_width + 1, packed_rect.y + y,
                    atlas_pixels_ldr[right_index + 0], atlas_pixels_ldr[right_index + 1], atlas_pixels_ldr[right_index + 2]);
            }
        }
    }

    out_lightmap_atlas.width = atlas_width;
    out_lightmap_atlas.height = atlas_height;
    out_lightmap_atlas.format = use_hdr_lightmaps ? LightmapAtlas::Format::kRGBA32Float : LightmapAtlas::Format::kRGBA8;
    if (use_hdr_lightmaps)
    {
        out_lightmap_atlas.pixels.resize(atlas_pixels_hdr.size() * sizeof(float));
        std::memcpy(out_lightmap_atlas.pixels.data(), atlas_pixels_hdr.data(), out_lightmap_atlas.pixels.size());
    }
    else
    {
        out_lightmap_atlas.pixels = std::move(atlas_pixels_ldr);
    }

    return face_lightmap_rects;
}

struct BspGeometryBuilder
{
    BspLoadData const& bsp;
    std::vector<PackedLightmapRect> const& face_lightmap_rects;
    std::vector<Vertex>& out_vertices;
    std::vector<uint32_t>& out_indices;
    RenderScene& scene;
    gpu::DevicePtr const& device;
    gpu::CommandBuffer& cmd_buffer;
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts;
    SourceTextureManager& texture_manager;
    SourceMaterialManager& material_manager;
    LightmapAtlas const& lightmap_atlas;
    std::unordered_map<std::string, uint32_t> material_indices;

    uint32_t GetTextureIndex(std::string const& material_name, dtexdata_t const& face_texdata)
    {
        (void)face_texdata;

        uint32_t texture_index = 0u;
        auto [it, inserted] = material_indices.emplace(material_name, 0u);
        if (inserted)
        {
            texture_index = material_manager.LoadMaterial(device, cmd_buffer, image_layouts, scene, texture_manager,
                material_name.c_str());
            it->second = texture_index;
        }
        else
        {
            texture_index = it->second;
        }

        return texture_index;
    }

    void BuildDisplacementGeometry(int fi, dface_t const& face, texinfo_t const& tex, uint32_t texture_index, uint32_t face_first_vertex)
    {
        if (face.dispinfo < 0 || face.dispinfo >= static_cast<int>(bsp.dispinfo.size()))
        {
            return;
        }

        ddispinfo_t const& face_dispinfo = bsp.dispinfo[face.dispinfo];
        int subdivision_count = 1 << face_dispinfo.power;
        int row_vertex_count = subdivision_count + 1;
        int disp_vertex_count = row_vertex_count * row_vertex_count;
        if (face_dispinfo.power < MIN_MAP_DISP_POWER || face_dispinfo.power > MAX_MAP_DISP_POWER)
        {
            return;
        }

        if (face_dispinfo.m_iDispVertStart < 0 || face_dispinfo.m_iDispVertStart + disp_vertex_count > static_cast<int>(bsp.dispverts.size()))
        {
            return;
        }

        Vector face_points[4];
        if (!GetFaceQuadPoints(face, bsp.surfedges, bsp.edges, bsp.vertexes, face_points))
        {
            return;
        }

        int start_corner = FindNearestQuadCorner(face_dispinfo.startPosition, face_points);
        Vector disp_points[4] = {
            face_points[start_corner],
            face_points[(start_corner + 1) % 4],
            face_points[(start_corner + 3) % 4],
            face_points[(start_corner + 2) % 4],
        };

        dtexdata_t const& face_texdata = bsp.texdata[tex.texdata];
        float uv_scale_u = face_texdata.view_width > 0 ? 1.0f / static_cast<float>(face_texdata.view_width) : 1.0f;
        float uv_scale_v = face_texdata.view_height > 0 ? 1.0f / static_cast<float>(face_texdata.view_height) : 1.0f;

        float disp_uv_corners[4][2];
        for (int point_index = 0; point_index < 4; ++point_index)
        {
            CalcUV(disp_points[point_index], tex, disp_uv_corners[point_index]);
            disp_uv_corners[point_index][0] *= uv_scale_u;
            disp_uv_corners[point_index][1] *= uv_scale_v;
        }

        PackedLightmapRect const& packed_rect = face_lightmap_rects[fi];
        int lightmap_width = face.m_LightmapTextureSizeInLuxels[0] + 1;
        int lightmap_height = face.m_LightmapTextureSizeInLuxels[1] + 1;

        struct DispVertexData
        {
            Vector position;
            Vector normal;
            float uv[2];
            float lightmap_uv[2];
        };
        struct DispTriangle
        {
            int a;
            int b;
            int c;
        };

        std::vector<DispVertexData> disp_vertex_data(disp_vertex_count);
        std::vector<DispTriangle> disp_triangles;
        disp_triangles.reserve(subdivision_count * subdivision_count * 2);

        auto grid_index = [row_vertex_count](int x, int y)
        {
            return y * row_vertex_count + x;
        };

        auto normalize_lightmap_uv = [&](float const* face_lightmap_uv, float* out_lightmap_uv)
        {
            out_lightmap_uv[0] = 0.5f / static_cast<float>(lightmap_atlas.width);
            out_lightmap_uv[1] = 0.5f / static_cast<float>(lightmap_atlas.height);
            if (!packed_rect.valid)
            {
                return;
            }

            float s = std::clamp(face_lightmap_uv[0], 0.0f, static_cast<float>(lightmap_width - 1));
            float t = std::clamp(face_lightmap_uv[1], 0.0f, static_cast<float>(lightmap_height - 1));
            out_lightmap_uv[0] = (static_cast<float>(packed_rect.x + 1) + s + 0.5f) / static_cast<float>(lightmap_atlas.width);
            out_lightmap_uv[1] = (static_cast<float>(packed_rect.y + 1) + t + 0.5f) / static_cast<float>(lightmap_atlas.height);
        };

        for (int y = 0; y < row_vertex_count; ++y)
        {
            float t = subdivision_count > 0 ? static_cast<float>(y) / static_cast<float>(subdivision_count) : 0.0f;
            for (int x = 0; x < row_vertex_count; ++x)
            {
                float s = subdivision_count > 0 ? static_cast<float>(x) / static_cast<float>(subdivision_count) : 0.0f;
                int current_index = grid_index(x, y);
                int disp_vertex_index = face_dispinfo.m_iDispVertStart + current_index;

                Vector base_position = BilerpVector(disp_points[0], disp_points[1], disp_points[2], disp_points[3], s, t);
                CDispVert const& disp_vertex = bsp.dispverts[disp_vertex_index];
                disp_vertex_data[current_index].position = base_position + disp_vertex.m_vVector * disp_vertex.m_flDist;

                BilerpUV(disp_uv_corners[0], disp_uv_corners[1], disp_uv_corners[2], disp_uv_corners[3], s, t,
                    disp_vertex_data[current_index].uv);

                float face_lightmap_uv[2] = {
                    s * static_cast<float>(lightmap_width - 1),
                    t * static_cast<float>(lightmap_height - 1),
                };
                normalize_lightmap_uv(face_lightmap_uv, disp_vertex_data[current_index].lightmap_uv);
                disp_vertex_data[current_index].normal = Vector(0.0f, 0.0f, 0.0f);
            }
        }

        Vector base_surface_normal = (disp_points[1] - disp_points[0]).Cross(disp_points[2] - disp_points[0]);
        if (base_surface_normal.Dot(base_surface_normal) <= 0.0f)
        {
            return;
        }

        auto add_disp_triangle = [&](int a, int b, int c, int disp_triangle_index)
        {
            if (disp_triangle_index >= 0 && disp_triangle_index < static_cast<int>(bsp.disptris.size())
                && (bsp.disptris[disp_triangle_index].m_uiTags & DISPTRI_TAG_REMOVE) != 0)
            {
                return;
            }

            Vector triangle_normal = (disp_vertex_data[b].position - disp_vertex_data[a].position)
                .Cross(disp_vertex_data[c].position - disp_vertex_data[a].position);
            if (triangle_normal.Dot(base_surface_normal) < 0.0f)
            {
                std::swap(b, c);
                triangle_normal = (disp_vertex_data[b].position - disp_vertex_data[a].position)
                    .Cross(disp_vertex_data[c].position - disp_vertex_data[a].position);
            }

            if (triangle_normal.Dot(triangle_normal) <= 0.0f)
            {
                return;
            }

            disp_vertex_data[a].normal += triangle_normal;
            disp_vertex_data[b].normal += triangle_normal;
            disp_vertex_data[c].normal += triangle_normal;
            disp_triangles.push_back({a, b, c});
        };

        for (int y = 0; y < subdivision_count; ++y)
        {
            for (int x = 0; x < subdivision_count; ++x)
            {
                int a = grid_index(x, y);
                int b = grid_index(x + 1, y);
                int c = grid_index(x, y + 1);
                int d = grid_index(x + 1, y + 1);
                int disp_triangle_start = face_dispinfo.m_iDispTriStart + ((y * subdivision_count + x) * 2);
                add_disp_triangle(a, b, d, disp_triangle_start);
                add_disp_triangle(a, d, c, disp_triangle_start + 1);
            }
        }

        for (DispVertexData& vertex_data : disp_vertex_data)
        {
            if (vertex_data.normal.Dot(vertex_data.normal) > 0.0f)
            {
                vertex_data.normal = vertex_data.normal.Normalized();
            }
            else
            {
                vertex_data.normal = base_surface_normal.Normalized();
            }
        }

        for (DispTriangle const& triangle : disp_triangles)
        {
            DispVertexData const& vertex0 = disp_vertex_data[triangle.a];
            DispVertexData const& vertex1 = disp_vertex_data[triangle.b];
            DispVertexData const& vertex2 = disp_vertex_data[triangle.c];
            uint32_t local_base_index = static_cast<uint32_t>(out_vertices.size() - face_first_vertex);
            out_vertices.push_back({vertex0.position, vertex0.normal, {vertex0.uv[0], vertex0.uv[1]},
                {vertex0.lightmap_uv[0], vertex0.lightmap_uv[1]}});
            out_vertices.push_back({vertex1.position, vertex1.normal, {vertex1.uv[0], vertex1.uv[1]},
                {vertex1.lightmap_uv[0], vertex1.lightmap_uv[1]}});
            out_vertices.push_back({vertex2.position, vertex2.normal, {vertex2.uv[0], vertex2.uv[1]},
                {vertex2.lightmap_uv[0], vertex2.lightmap_uv[1]}});
            out_indices.push_back(local_base_index + 0);
            out_indices.push_back(local_base_index + 1);
            out_indices.push_back(local_base_index + 2);
        }
    }

    void BuildPolygonFaceGeometry(int fi, dface_t const& face, texinfo_t const& tex, uint32_t face_first_vertex)
    {
        dtexdata_t const& face_texdata = bsp.texdata[tex.texdata];
        float uv_scale_u = face_texdata.view_width > 0 ? 1.0f / static_cast<float>(face_texdata.view_width) : 1.0f;
        float uv_scale_v = face_texdata.view_height > 0 ? 1.0f / static_cast<float>(face_texdata.view_height) : 1.0f;

        const int first = face.firstedge;
        const int count = face.numedges;

        if (first < 0 || first + count > static_cast<int>(bsp.surfedges.size()))
        {
            return;
        }

        int32_t first_surfedge = bsp.surfedges[first];
        uint16_t first_index = bsp.edges[std::abs(first_surfedge)].v[(first_surfedge < 0)];

        for (int se_idx = 1; se_idx < count; ++se_idx)
        {
            int32_t surfedge = bsp.surfedges[first + se_idx];
            int32_t edge_vertex_idx = (surfedge < 0);
            uint16_t index0 = bsp.edges[std::abs(surfedge)].v[edge_vertex_idx];
            uint16_t index1 = bsp.edges[std::abs(surfedge)].v[1 - edge_vertex_idx];

            Vector v0 = bsp.vertexes[first_index].point;
            Vector v1 = bsp.vertexes[index0].point;
            Vector v2 = bsp.vertexes[index1].point;

            float uv0[2];
            CalcUV(v0, tex, &uv0[0]);
            uv0[0] *= uv_scale_u;
            uv0[1] *= uv_scale_v;
            float uv1[2];
            CalcUV(v1, tex, &uv1[0]);
            uv1[0] *= uv_scale_u;
            uv1[1] *= uv_scale_v;
            float uv2[2];
            CalcUV(v2, tex, &uv2[0]);
            uv2[0] *= uv_scale_u;
            uv2[1] *= uv_scale_v;

            float lightmap_uv0[2] = {0.5f / static_cast<float>(lightmap_atlas.width), 0.5f / static_cast<float>(lightmap_atlas.height)};
            float lightmap_uv1[2] = {lightmap_uv0[0], lightmap_uv0[1]};
            float lightmap_uv2[2] = {lightmap_uv0[0], lightmap_uv0[1]};

            PackedLightmapRect const& packed_rect = face_lightmap_rects[fi];
            if (packed_rect.valid)
            {
                int lightmap_width = face.m_LightmapTextureSizeInLuxels[0] + 1;
                int lightmap_height = face.m_LightmapTextureSizeInLuxels[1] + 1;
                float face_lightmap_uv0[2];
                float face_lightmap_uv1[2];
                float face_lightmap_uv2[2];
                CalcLightmapUV(v0, tex, face, &face_lightmap_uv0[0]);
                CalcLightmapUV(v1, tex, face, &face_lightmap_uv1[0]);
                CalcLightmapUV(v2, tex, face, &face_lightmap_uv2[0]);

                auto normalize_lightmap_uv = [&](float const* face_lightmap_uv, float* out_lightmap_uv)
                {
                    float s = std::clamp(face_lightmap_uv[0], 0.0f, static_cast<float>(lightmap_width - 1));
                    float t = std::clamp(face_lightmap_uv[1], 0.0f, static_cast<float>(lightmap_height - 1));
                    out_lightmap_uv[0] = (static_cast<float>(packed_rect.x + 1) + s + 0.5f) / static_cast<float>(lightmap_atlas.width);
                    out_lightmap_uv[1] = (static_cast<float>(packed_rect.y + 1) + t + 0.5f) / static_cast<float>(lightmap_atlas.height);
                };

                normalize_lightmap_uv(face_lightmap_uv0, lightmap_uv0);
                normalize_lightmap_uv(face_lightmap_uv1, lightmap_uv1);
                normalize_lightmap_uv(face_lightmap_uv2, lightmap_uv2);
            }

            Vector normal = (v2 - v0).Cross(v1 - v0).Normalized();

            uint32_t local_base_index = static_cast<uint32_t>(out_vertices.size() - face_first_vertex);
            out_vertices.push_back({v0, normal, {uv0[0], uv0[1]}, {lightmap_uv0[0], lightmap_uv0[1]}});
            out_vertices.push_back({v1, normal, {uv1[0], uv1[1]}, {lightmap_uv1[0], lightmap_uv1[1]}});
            out_vertices.push_back({v2, normal, {uv2[0], uv2[1]}, {lightmap_uv2[0], lightmap_uv2[1]}});
            out_indices.push_back(local_base_index + 0);
            out_indices.push_back(local_base_index + 1);
            out_indices.push_back(local_base_index + 2);
        }
    }

    void BuildFaceGeometry(int fi, uint32_t face_first_vertex, uint32_t* out_material_index)
    {
        dface_t const& face = bsp.faces[fi];

        if (face.numedges < 3)
        {
            return;
        }

        if (face.texinfo < 0 || face.texinfo >= static_cast<int>(bsp.texinfo.size()))
        {
            return;
        }

        texinfo_t const& tex = bsp.texinfo[face.texinfo];
        if (tex.texdata < 0 || tex.texdata >= static_cast<int>(bsp.texdata.size()))
        {
            return;
        }

        dtexdata_t const& face_texdata = bsp.texdata[tex.texdata];
        char const* material_name_ptr =
            GetTexdataString(bsp.texdata_string_table, bsp.texdata_string_data, face_texdata.nameStringTableID);
        if (!material_name_ptr || material_name_ptr[0] == '\0')
        {
            return;
        }

        if (tex.flags & (SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP | SURF_TRIGGER))
        {
            return;
        }

        std::string material_name = material_name_ptr;
        uint32_t texture_index = GetTextureIndex(material_name, face_texdata);
        if (out_material_index)
        {
            *out_material_index = texture_index;
        }

        if (face.dispinfo != -1)
        {
            BuildDisplacementGeometry(fi, face, tex, texture_index, face_first_vertex);
            return;
        }

        BuildPolygonFaceGeometry(fi, face, tex, face_first_vertex);
    }

    template<typename TOnRangeBuilt>
    void BuildModelGeometry(dmodel_t const& model, TOnRangeBuilt&& on_range_built)
    {
        if (model.firstface < 0 || model.numfaces < 0 || model.firstface + model.numfaces > static_cast<int>(bsp.faces.size()))
        {
            return;
        }

        for (int face_offset = 0; face_offset < model.numfaces; ++face_offset)
        {
            uint32_t first_vertex = static_cast<uint32_t>(out_vertices.size());
            uint32_t first_index = static_cast<uint32_t>(out_indices.size());
            uint32_t material_index = 0;
            BuildFaceGeometry(model.firstface + face_offset, first_vertex, &material_index);
            uint32_t index_count = static_cast<uint32_t>(out_indices.size()) - first_index;
            if (index_count == 0)
            {
                continue;
            }

            on_range_built(first_vertex, first_index, index_count, material_index);
        }
    }

    void BuildWorldGeometry()
    {
        BuildModelGeometry(bsp.models[0], [&](uint32_t first_vertex, uint32_t first_index, uint32_t index_count, uint32_t material_index)
        {
            AddRenderInstance(scene.instances, first_vertex, first_index, index_count, material_index, 0);
        });
    }

    void BuildBrushModelGeometry(int submodel_index, std::vector<BrushModelSourceRange>& out_brush_model_ranges)
    {
        BuildModelGeometry(bsp.models[submodel_index], [&](uint32_t first_vertex, uint32_t first_index, uint32_t index_count, uint32_t material_index)
        {
            out_brush_model_ranges.push_back({submodel_index, first_vertex, first_index, index_count, material_index});
        });
    }
};

void LoadBsp(char const* filename, RenderScene& io_scene, gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager, SourceModelManager& model_manager, LightmapAtlas& out_lightmap_atlas,
    std::vector<BrushModelSourceRange>& out_brush_model_ranges)
{
    std::ifstream f("sourcetest/" + std::string(filename), std::ios::binary);
    out_brush_model_ranges.clear();
    InitializeFallbackLightmapAtlas(out_lightmap_atlas);

    if (!f.is_open())
        return;

    dheader_t hdr;
    if (!LoadBspHeader(f, hdr))
    {
        return;
    }

    BspLoadData bsp = {};
    if (!LoadBspGeometryLumps(f, hdr, bsp)) return;
    if (!LoadBspMaterialLumps(f, hdr, bsp)) return;
    if (!LoadBspDisplacementLumps(f, hdr, bsp)) return;
    if (!LoadBspLightingLumps(f, hdr, bsp)) return;

    std::vector<PackedLightmapRect> face_lightmap_rects = BuildLightmapAtlas(bsp, out_lightmap_atlas);
    if (bsp.models.empty())
    {
        return;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    BspGeometryBuilder geometry_builder{
        bsp,
        face_lightmap_rects,
        vertices,
        indices,
        io_scene,
        device,
        cmd_buffer,
        image_layouts,
        texture_manager,
        material_manager,
        out_lightmap_atlas,
        {}
    };

    geometry_builder.BuildWorldGeometry();
    for (int submodel_index = 1; submodel_index < static_cast<int>(bsp.models.size()); ++submodel_index)
    {
        geometry_builder.BuildBrushModelGeometry(submodel_index, out_brush_model_ranges);
    }

    io_scene.vertices.Append(vertices);
    io_scene.indices.Append(indices);

    std::vector<StaticPropInstance> static_props;
    LoadStaticProps(filename, static_props);

    for (StaticPropInstance const& static_prop : static_props)
    {
        matrix3x4_t model_to_world;
        AngleMatrix(static_prop.angles, static_prop.origin, model_to_world);
        model_manager.LoadModel(static_prop.model_name.c_str(), static_prop.skin, model_to_world, true, io_scene, device, cmd_buffer,
            image_layouts, texture_manager, material_manager);
    }
}

void LoadStaticProps(char const* filename, std::vector<StaticPropInstance>& out_static_props)
{
    out_static_props.clear();

    std::ifstream f("sourcetest/" + std::string(filename), std::ios::binary);
    if (!f.is_open())
    {
        return;
    }

    dheader_t hdr;
    f.read(reinterpret_cast<char*>(&hdr), sizeof(dheader_t));

    if (hdr.ident != IDBSPHEADER)
    {
        return;
    }

    if (hdr.version < MINBSPVERSION || hdr.version > BSPVERSION)
    {
        return;
    }

    lump_t const& game_lump = hdr.lumps[LUMP_GAME_LUMP];
    if (game_lump.fileofs <= 0 || game_lump.filelen <= 0)
    {
        return;
    }

    std::vector<uint8_t> game_lump_bytes(static_cast<size_t>(game_lump.filelen));
    f.seekg(game_lump.fileofs, std::ios::beg);
    f.read(reinterpret_cast<char*>(game_lump_bytes.data()), static_cast<std::streamsize>(game_lump_bytes.size()));
    if (!f.good())
    {
        return;
    }

    if (game_lump_bytes.size() < sizeof(dgamelumpheader_t))
    {
        return;
    }

    dgamelumpheader_t const* game_lump_header = reinterpret_cast<dgamelumpheader_t const*>(game_lump_bytes.data());
    size_t directory_bytes = sizeof(dgamelumpheader_t) + static_cast<size_t>(game_lump_header->lumpCount) * sizeof(dgamelump_t);
    if (directory_bytes > game_lump_bytes.size())
    {
        return;
    }

    dgamelump_t const* game_lump_directory =
        reinterpret_cast<dgamelump_t const*>(game_lump_bytes.data() + sizeof(dgamelumpheader_t));

    dgamelump_t const* static_prop_lump_info = nullptr;
    for (int lump_index = 0; lump_index < game_lump_header->lumpCount; ++lump_index)
    {
        if (game_lump_directory[lump_index].id == GAMELUMP_STATIC_PROPS)
        {
            static_prop_lump_info = &game_lump_directory[lump_index];
            break;
        }
    }

    if (!static_prop_lump_info)
    {
        return;
    }

    if (static_prop_lump_info->version < 4 || static_prop_lump_info->version > GAMELUMP_STATIC_PROPS_VERSION)
    {
        return;
    }

    if (static_prop_lump_info->fileofs < 0 || static_prop_lump_info->filelen <= 0)
    {
        return;
    }

    std::vector<uint8_t> static_prop_bytes(static_cast<size_t>(static_prop_lump_info->filelen));
    f.seekg(static_prop_lump_info->fileofs, std::ios::beg);
    f.read(reinterpret_cast<char*>(static_prop_bytes.data()), static_cast<std::streamsize>(static_prop_bytes.size()));
    if (!f.good())
    {
        return;
    }

    uint8_t const* cursor = static_prop_bytes.data();
    uint8_t const* end = static_prop_bytes.data() + static_prop_bytes.size();

    auto read_int = [&](int& out_value) -> bool
    {
        if (cursor + sizeof(int) > end)
        {
            return false;
        }

        std::memcpy(&out_value, cursor, sizeof(int));
        cursor += sizeof(int);
        return true;
    };

    auto read_bytes = [&](void* out_data, size_t size) -> bool
    {
        if (cursor + size > end)
        {
            return false;
        }

        std::memcpy(out_data, cursor, size);
        cursor += size;
        return true;
    };

    int dict_count = 0;
    if (!read_int(dict_count) || dict_count < 0)
    {
        return;
    }

    std::vector<StaticPropDictLump_t> dict_entries(static_cast<size_t>(dict_count));
    if (!dict_entries.empty() && !read_bytes(dict_entries.data(), dict_entries.size() * sizeof(StaticPropDictLump_t)))
    {
        return;
    }

    int leaf_count = 0;
    if (!read_int(leaf_count) || leaf_count < 0)
    {
        return;
    }

    size_t leaf_bytes = static_cast<size_t>(leaf_count) * sizeof(StaticPropLeafLump_t);
    if (cursor + leaf_bytes > end)
    {
        return;
    }
    cursor += leaf_bytes;

    int prop_count = 0;
    if (!read_int(prop_count) || prop_count < 0)
    {
        return;
    }

    out_static_props.reserve(static_cast<size_t>(prop_count));
    for (int prop_index = 0; prop_index < prop_count; ++prop_index)
    {
        StaticPropInstance instance;
        unsigned short prop_type = 0;

        if (static_prop_lump_info->version == 4)
        {
            StaticPropLumpV4_t prop_data;
            if (!read_bytes(&prop_data, sizeof(prop_data)))
            {
                out_static_props.clear();
                return;
            }

            prop_type = prop_data.m_PropType;
            instance.origin = prop_data.m_Origin;
            instance.angles = prop_data.m_Angles;
            instance.skin = prop_data.m_Skin;
        }
        else if (static_prop_lump_info->version == 5)
        {
            StaticPropLumpV5_t prop_data;
            if (!read_bytes(&prop_data, sizeof(prop_data)))
            {
                out_static_props.clear();
                return;
            }

            prop_type = prop_data.m_PropType;
            instance.origin = prop_data.m_Origin;
            instance.angles = prop_data.m_Angles;
            instance.skin = prop_data.m_Skin;
        }
        else
        {
            StaticPropLump_t prop_data;
            if (!read_bytes(&prop_data, sizeof(prop_data)))
            {
                out_static_props.clear();
                return;
            }

            prop_type = prop_data.m_PropType;
            instance.origin = prop_data.m_Origin;
            instance.angles = prop_data.m_Angles;
            instance.skin = prop_data.m_Skin;
        }

        if (prop_type >= dict_entries.size())
        {
            continue;
        }

        instance.model_name = dict_entries[prop_type].m_Name;
        if (!instance.model_name.empty())
        {
            out_static_props.push_back(instance);
        }
    }
}
