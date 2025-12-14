#include "bsp_loader.h"
#include "bspfile.h"

#include <fstream>
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
    // Compute UV in "texel space" (not normalized to 0..1)
    Vector uAxis(ti.textureVecsTexelsPerWorldUnits[0][0], ti.textureVecsTexelsPerWorldUnits[0][1], ti.textureVecsTexelsPerWorldUnits[0][2]);
    Vector vAxis(ti.textureVecsTexelsPerWorldUnits[1][0], ti.textureVecsTexelsPerWorldUnits[1][1], ti.textureVecsTexelsPerWorldUnits[1][2]);

    uv[0] = p.Dot(uAxis) + ti.textureVecsTexelsPerWorldUnits[0][3];
    uv[1] = p.Dot(vAxis) + ti.textureVecsTexelsPerWorldUnits[1][3];
}

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices)
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

    if (!ReadLump(f, hdr.lumps[LUMP_VERTEXES], vertexes))   return;
    if (!ReadLump(f, hdr.lumps[LUMP_EDGES], edges))         return;
    if (!ReadLump(f, hdr.lumps[LUMP_SURFEDGES], surfedges)) return;
    if (!ReadLump(f, hdr.lumps[LUMP_FACES], faces))         return;
    if (!ReadLump(f, hdr.lumps[LUMP_TEXINFO], texinfo))     return;

    // Triangulate each face using triangle fan around vertex0
    // face polygon vertices are traced via surfedges -> edges -> vertex indices.
    for (int fi = 0; fi < (int)faces.size(); ++fi)
    {
        const dface_t& face = faces[fi];

        // Skip invalid or tiny faces
        if (face.numedges < 3) continue;

        // Skip displacements (optional)
        if (face.dispinfo != -1) continue;

        if (face.texinfo < 0 || face.texinfo >= (int)texinfo.size())
            continue;

        const texinfo_t& tex = texinfo[face.texinfo];

        if (tex.flags & (SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP | SURF_TRIGGER))
            continue;

        const int first = face.firstedge;
        const int count = face.numedges;

        if (first < 0 || first + count >(int)surfedges.size())
            continue;

        // Triangle fan around first vertex
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
            float uv1[2];
            CalcUV(v1, tex, &uv1[0]);
            float uv2[2];
            CalcUV(v2, tex, &uv2[0]);

            // Emit the triangle
            out_vertices.push_back({ v0, Vector(1, 0, 0), uv0[0], uv0[1] });
            out_vertices.push_back({ v1, Vector(0, 1, 0), uv1[0], uv1[1] });
            out_vertices.push_back({ v2, Vector(0, 0, 1), uv2[0], uv2[1] });
        }
    }

}
