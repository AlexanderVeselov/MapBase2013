#include "bsp_loader.h"
#include "bspfile.h"

#include <algorithm>
#include <cmath>
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

uint8_t EncodeLightmapComponent(const ColorRGBExp32& lightmap_color, int component_index)
{
    int component = component_index == 0 ? lightmap_color.r : (component_index == 1 ? lightmap_color.g : lightmap_color.b);
    float linear = TexLightToLinear(component, lightmap_color.exponent);
    linear = (std::clamp)(linear, 0.0f, 1.0f);
    return static_cast<uint8_t>(linear * 255.0f + 0.5f);
}

void WriteAtlasPixel(std::vector<uint8_t>& atlas_pixels, int atlas_width, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    size_t pixel_index = static_cast<size_t>(y * atlas_width + x) * 4;
    atlas_pixels[pixel_index + 0] = r;
    atlas_pixels[pixel_index + 1] = g;
    atlas_pixels[pixel_index + 2] = b;
    atlas_pixels[pixel_index + 3] = 255;
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

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices, std::vector<BspMaterial>& out_materials,
    BspLightmapAtlas& out_lightmap_atlas)
{
    std::ifstream f("sourcetest/" + std::string(filename), std::ios::binary);

    out_lightmap_atlas.width = 1;
    out_lightmap_atlas.height = 1;
    out_lightmap_atlas.rgba_pixels = {255, 255, 255, 255};

    if (!f.is_open())
        return;

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

    std::vector<dvertex_t> vertexes;
    std::vector<dedge_t>   edges;
    std::vector<int32_t>   surfedges;
    std::vector<dface_t>   faces;
    std::vector<texinfo_t> texinfo;
    std::vector<dtexdata_t> texdata;
    std::vector<ColorRGBExp32> lighting;
    std::vector<int32_t> texdata_string_table;
    std::vector<char> texdata_string_data;

    if (!ReadLump(f, hdr.lumps[LUMP_VERTEXES], vertexes))   return;
    if (!ReadLump(f, hdr.lumps[LUMP_EDGES], edges))         return;
    if (!ReadLump(f, hdr.lumps[LUMP_SURFEDGES], surfedges)) return;
    if (!ReadLump(f, hdr.lumps[LUMP_FACES], faces))         return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXINFO], texinfo))     return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXDATA], texdata))     return;
    if (!ReadLump(f, hdr.lumps[LUMP_LIGHTING], lighting))   return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXDATA_STRING_TABLE], texdata_string_table)) return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXDATA_STRING_DATA], texdata_string_data)) return;

    std::unordered_map<std::string, uint32_t> material_indices;
    std::vector<PackedLightmapRect> face_lightmap_rects(faces.size());

    constexpr int kAtlasMaxRowWidth = 2048;
    int atlas_cursor_x = 1;
    int atlas_cursor_y = 0;
    int atlas_row_height = 0;
    int atlas_width = 1;
    int atlas_height = 1;

    for (size_t face_index = 0; face_index < faces.size(); ++face_index)
    {
        dface_t const& face = faces[face_index];
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

    std::vector<uint8_t> atlas_pixels(static_cast<size_t>(atlas_width * atlas_height) * 4, 255);
    for (size_t face_index = 0; face_index < faces.size(); ++face_index)
    {
        PackedLightmapRect const& packed_rect = face_lightmap_rects[face_index];
        if (!packed_rect.valid)
        {
            continue;
        }

        dface_t const& face = faces[face_index];
        int lightmap_width = face.m_LightmapTextureSizeInLuxels[0] + 1;
        int lightmap_height = face.m_LightmapTextureSizeInLuxels[1] + 1;
        size_t sample_offset = static_cast<size_t>(face.lightofs) / sizeof(ColorRGBExp32);
        size_t sample_count = static_cast<size_t>(lightmap_width * lightmap_height);
        if (sample_offset + sample_count > lighting.size())
        {
            continue;
        }

        for (int y = 0; y < lightmap_height; ++y)
        {
            for (int x = 0; x < lightmap_width; ++x)
            {
                ColorRGBExp32 const& lightmap_color = lighting[sample_offset + static_cast<size_t>(y * lightmap_width + x)];
                uint8_t r = EncodeLightmapComponent(lightmap_color, 0);
                uint8_t g = EncodeLightmapComponent(lightmap_color, 1);
                uint8_t b = EncodeLightmapComponent(lightmap_color, 2);
                WriteAtlasPixel(atlas_pixels, atlas_width, packed_rect.x + 1 + x, packed_rect.y + 1 + y, r, g, b);
            }
        }

        for (int x = 0; x < lightmap_width; ++x)
        {
            size_t top_index = static_cast<size_t>((packed_rect.y + 1) * atlas_width + (packed_rect.x + 1 + x)) * 4;
            size_t bottom_index = static_cast<size_t>((packed_rect.y + lightmap_height) * atlas_width + (packed_rect.x + 1 + x)) * 4;
            WriteAtlasPixel(atlas_pixels, atlas_width, packed_rect.x + 1 + x, packed_rect.y,
                atlas_pixels[top_index + 0], atlas_pixels[top_index + 1], atlas_pixels[top_index + 2]);
            WriteAtlasPixel(atlas_pixels, atlas_width, packed_rect.x + 1 + x, packed_rect.y + lightmap_height + 1,
                atlas_pixels[bottom_index + 0], atlas_pixels[bottom_index + 1], atlas_pixels[bottom_index + 2]);
        }

        for (int y = 0; y < lightmap_height + 2; ++y)
        {
            size_t left_index = static_cast<size_t>((packed_rect.y + y) * atlas_width + (packed_rect.x + 1)) * 4;
            size_t right_index = static_cast<size_t>((packed_rect.y + y) * atlas_width + (packed_rect.x + lightmap_width)) * 4;
            WriteAtlasPixel(atlas_pixels, atlas_width, packed_rect.x, packed_rect.y + y,
                atlas_pixels[left_index + 0], atlas_pixels[left_index + 1], atlas_pixels[left_index + 2]);
            WriteAtlasPixel(atlas_pixels, atlas_width, packed_rect.x + lightmap_width + 1, packed_rect.y + y,
                atlas_pixels[right_index + 0], atlas_pixels[right_index + 1], atlas_pixels[right_index + 2]);
        }
    }

    out_lightmap_atlas.width = atlas_width;
    out_lightmap_atlas.height = atlas_height;
    out_lightmap_atlas.rgba_pixels = std::move(atlas_pixels);

    for (int fi = 0; fi < (int)faces.size(); ++fi)
    {
        const dface_t& face = faces[fi];

        if (face.numedges < 3) continue;
        if (face.dispinfo != -1) continue;

        if (face.texinfo < 0 || face.texinfo >= (int)texinfo.size())
            continue;

        const texinfo_t& tex = texinfo[face.texinfo];
        if (tex.texdata < 0 || tex.texdata >= static_cast<int>(texdata.size()))
            continue;

        dtexdata_t const& face_texdata = texdata[tex.texdata];
        char const* material_name_ptr =
            GetTexdataString(texdata_string_table, texdata_string_data, face_texdata.nameStringTableID);
        if (!material_name_ptr || material_name_ptr[0] == '\0')
            continue;

        if (tex.flags & (SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP | SURF_TRIGGER))
            continue;

        std::string material_name = material_name_ptr;
        uint32_t texture_index = 0;
        auto [it, inserted] = material_indices.emplace(material_name, 0);
        if (inserted)
        {
            out_materials.push_back(BspMaterial{material_name, face_texdata.view_width, face_texdata.view_height});
            texture_index = static_cast<uint32_t>(out_materials.size());
            it->second = texture_index;
        }
        else
        {
            texture_index = it->second;
        }

        float uv_scale_u = face_texdata.view_width > 0 ? 1.0f / static_cast<float>(face_texdata.view_width) : 1.0f;
        float uv_scale_v = face_texdata.view_height > 0 ? 1.0f / static_cast<float>(face_texdata.view_height) : 1.0f;

        const int first = face.firstedge;
        const int count = face.numedges;

        if (first < 0 || first + count >(int)surfedges.size())
            continue;

        int32_t first_surfedge = surfedges[first];
        uint16_t first_index = edges[abs(first_surfedge)].v[(first_surfedge < 0)];

        for (int se_idx = 1; se_idx < count; ++se_idx)
        {
            int32_t surfedge = surfedges[first + se_idx];
            int32_t edge_vertex_idx = (surfedge < 0);
            uint16_t index0 = edges[abs(surfedge)].v[edge_vertex_idx];
            uint16_t index1 = edges[abs(surfedge)].v[1 - edge_vertex_idx];

            // Triangle: (first_index, index0, index1)
            Vector v0 = vertexes[first_index].point;
            Vector v1 = vertexes[index0].point;
            Vector v2 = vertexes[index1].point;

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

            float lightmap_uv0[2] = {0.5f / static_cast<float>(out_lightmap_atlas.width), 0.5f / static_cast<float>(out_lightmap_atlas.height)};
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
                    out_lightmap_uv[0] = (static_cast<float>(packed_rect.x + 1) + s + 0.5f) / static_cast<float>(out_lightmap_atlas.width);
                    out_lightmap_uv[1] = (static_cast<float>(packed_rect.y + 1) + t + 0.5f) / static_cast<float>(out_lightmap_atlas.height);
                };

                normalize_lightmap_uv(face_lightmap_uv0, lightmap_uv0);
                normalize_lightmap_uv(face_lightmap_uv1, lightmap_uv1);
                normalize_lightmap_uv(face_lightmap_uv2, lightmap_uv2);
            }

            Vector normal = (v2 - v0).Cross(v1 - v0).Normalized();

            out_vertices.push_back({v0, normal, {uv0[0], uv0[1]}, {lightmap_uv0[0], lightmap_uv0[1]}, texture_index});
            out_vertices.push_back({v1, normal, {uv1[0], uv1[1]}, {lightmap_uv1[0], lightmap_uv1[1]}, texture_index});
            out_vertices.push_back({v2, normal, {uv2[0], uv2[1]}, {lightmap_uv2[0], lightmap_uv2[1]}, texture_index});
        }
    }
}
