#include "bsp_loader.h"
#include "bspfile.h"

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

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices, std::vector<BspMaterial>& out_materials)
{
    std::ifstream f("sourcetest/" + std::string(filename), std::ios::binary);

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
    std::vector<int32_t> texdata_string_table;
    std::vector<char> texdata_string_data;

    if (!ReadLump(f, hdr.lumps[LUMP_VERTEXES], vertexes))   return;
    if (!ReadLump(f, hdr.lumps[LUMP_EDGES], edges))         return;
    if (!ReadLump(f, hdr.lumps[LUMP_SURFEDGES], surfedges)) return;
    if (!ReadLump(f, hdr.lumps[LUMP_FACES], faces))         return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXINFO], texinfo))     return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXDATA], texdata))     return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXDATA_STRING_TABLE], texdata_string_table)) return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXDATA_STRING_DATA], texdata_string_data)) return;

    std::unordered_map<std::string, uint32_t> material_indices;

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

            Vector normal = (v2 - v0).Cross(v1 - v0).Normalized();

            out_vertices.push_back({v0, normal, {uv0[0], uv0[1]}, texture_index});
            out_vertices.push_back({v1, normal, {uv1[0], uv1[1]}, texture_index});
            out_vertices.push_back({v2, normal, {uv2[0], uv2[1]}, texture_index});
        }
    }
}
