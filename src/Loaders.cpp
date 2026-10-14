// ModelThumbLite - format loaders (original code; method mirrors NCrewsShellExtensions).
#define CGLTF_IMPLEMENTATION
#include "../thirdparty/cgltf.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "../thirdparty/tiny_obj_loader.h"
#include "../thirdparty/miniz.h"
#include "Loaders.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <sstream>

#include "../thirdparty/ufbx.h"
#include "../thirdparty/laszip/laszip_api.h"

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring ToLowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

static std::wstring FileExtensionOf(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/"); 
    if (dot == std::wstring::npos || (sep != std::wstring::npos && sep > dot)) return L"";
    return ToLowerW(path.substr(dot));
}

// ---------------- content sniffing (stream case) ----------------
std::wstring SniffModelExtension(const char* data, size_t size) {
    if (!data || size == 0) return L"";
    auto startsWith = [&](const char* tok) {
        size_t n = strlen(tok);
        if (size < n) return false;
        for (size_t i = 0; i < n; i++)
            if (tolower((unsigned char)data[i]) != tolower((unsigned char)tok[i])) return false;
        return true;
    };
    if (size >= 4 && memcmp(data, "glTF", 4) == 0) return L".glb";
    if (size >= 4 && (unsigned char)data[0] == 0x50 && (unsigned char)data[1] == 0x4B &&
        (unsigned char)data[2] == 0x03 && (unsigned char)data[3] == 0x04) return L".3mf";
    size_t i = 0;
    if (size >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) i = 3;
    while (i < size && isspace((unsigned char)data[i])) i++;
    if (i < size && data[i] == '{') return L".gltf";
    if (startsWith("solid")) {
        std::string head(data, data + (size < 4096 ? size : 4096));
        std::string low = head;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find("facet normal") != std::string::npos || low.find("endsolid") != std::string::npos)
            return L".stl";
    }
    if (size >= 84) {
        uint32_t n = 0; memcpy(&n, data + 80, 4);
        uint64_t expect = 84ULL + (uint64_t)n * 50ULL;
        if (expect == size || ((size - 84ULL) % 50ULL) == 0) return L".stl";
    }
    // OBJ tokens in first lines
    {
        size_t lines = 0, p = i;
        char line[256];
        while (p < size && lines < 32) {
            size_t e = p;
            while (e < size && data[e] != '\r' && data[e] != '\n') e++;
            size_t len = e - p < 255 ? e - p : 255;
            memcpy(line, data + p, len); line[len] = 0;
            char* t = line;
            while (*t == ' ' || *t == '\t') t++;
            if (*t && *t != '#') {
                if (!strncmp(t, "v ", 2) || !strncmp(t, "v\t", 2) || !strncmp(t, "f ", 2) ||
                    !strncmp(t, "o ", 2) || !strncmp(t, "mtllib ", 7) || !strncmp(t, "usemtl ", 7))
                    return L".obj";
            }
            p = e + 1; lines++;
            while (p < size && (data[p] == '\r' || data[p] == '\n')) p++;
        }
    }
    return L"";
}

// ---------------- STL ----------------
static const char* FastFloat(const char* p, const char* end, float& out) {
    // fast decimal parser (MSVC sscanf %f is ~100x too slow for bulk mesh text)
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    bool neg = false;
    if (p < end && (*p == '-' || *p == '+')) { neg = (*p == '-'); p++; }
    double val = 0;
    bool any = false;
    while (p < end && *p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; any = true; }
    if (p < end && *p == '.') {
        p++;
        double frac = 0, base = 1;
        while (p < end && *p >= '0' && *p <= '9') { frac = frac * 10 + (*p - '0'); base *= 10; p++; any = true; }
        val += frac / base;
    }
    if (!any) return nullptr;
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        bool eneg = false;
        if (p < end && (*p == '-' || *p == '+')) { eneg = (*p == '-'); p++; }
        int exp = 0; bool eany = false;
        while (p < end && *p >= '0' && *p <= '9') { exp = exp * 10 + (*p - '0'); p++; eany = true; }
        if (eany) {
            double mult = 1;
            for (int i = 0; i < exp && i < 308; i++) mult *= 10;
            val = eneg ? val / mult : val * mult;
        }
    }
    out = (float)(neg ? -val : val);
    return p;
}

static bool LoadStl(const std::wstring& path, LiteMesh& m) {
    std::string u8 = WideToUtf8(path);
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsize < 84) { fclose(f); return false; }
    std::vector<char> buf((size_t)fsize);
    if (fread(buf.data(), 1, (size_t)fsize, f) != (size_t)fsize) { fclose(f); return false; }
    fclose(f);
    uint32_t n = 0; memcpy(&n, buf.data() + 80, 4);
    bool binary = (84ULL + (uint64_t)n * 50ULL == (uint64_t)fsize);
    if (!binary) {
        // could still be binary starting with "solid"; exact-size check decides
        // (ASCII files essentially never match the size equation)
    }
    if (binary && n > 0 && n < 100000000) {
        m.verts.reserve((size_t)n * 9); m.tris.reserve((size_t)n * 3);
        const char* p = buf.data() + 84;
        for (uint32_t t = 0; t < n; t++, p += 50) {
            uint32_t base = (uint32_t)(m.verts.size() / 3);
            float v[9]; memcpy(v, p + 12, 36);
            // basic sanity: reject NaN storms
            bool ok = true;
            for (int k = 0; k < 9; k++) if (!(v[k] == v[k])) { ok = false; break; }
            if (!ok) continue;
            for (int k = 0; k < 9; k++) m.verts.push_back(v[k]);
            m.tris.push_back(base); m.tris.push_back(base + 1); m.tris.push_back(base + 2);
        }
        return !m.tris.empty();
    }
    // ASCII: fast line scan for "vertex x y z" lines; every 3 = 1 tri.
    // (Previous char-walk version stalled on large relief files.)
    const char* p = buf.data();
    const char* end = p + fsize;
    float v[9]; int vi = 0;
    size_t triCount = 0;
    const size_t kMaxFacets = 4000000; // thumbnails need no more
    m.verts.reserve(30000); m.tris.reserve(10000);
    while (p < end && triCount < kMaxFacets) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        const char* lineEnd = nl ? nl : end;
        const char* q = p;
        while (q < lineEnd && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
        if ((size_t)(lineEnd - q) > 7 && !strncmp(q, "vertex", 6) &&
            (q[6] == ' ' || q[6] == '\t' || q[6] == '\r')) {
            float x, y, z;
            const char* r = FastFloat(q + 6, lineEnd, x);
            r = r ? FastFloat(r, lineEnd, y) : nullptr;
            r = r ? FastFloat(r, lineEnd, z) : nullptr;
            if (r) {
                if (x == x && y == y && z == z) {
                    v[vi*3] = x; v[vi*3+1] = y; v[vi*3+2] = z; vi++;
                    if (vi == 3) {
                        vi = 0; triCount++;
                        uint32_t base = (uint32_t)(m.verts.size() / 3);
                        for (int k = 0; k < 9; k++) m.verts.push_back(v[k]);
                        m.tris.push_back(base); m.tris.push_back(base+1); m.tris.push_back(base+2);
                    }
                }
            }
        }
        p = nl ? nl + 1 : end;
    }
    return !m.tris.empty();
}

static void MeshVertsToCloud(const LiteMesh& m, LitePoints& c) {
    size_t nv = m.vertCount();
    if (!nv) return;
    const size_t kMax = 300000;
    size_t step = (nv + kMax - 1) / kMax;
    if (!step) step = 1;
    for (size_t i = 0; i < nv; i += step) {
        c.xyz.push_back(m.verts[i*3]);
        c.xyz.push_back(m.verts[i*3+1]);
        c.xyz.push_back(m.verts[i*3+2]);
        c.val.push_back(0.5f);
    }
}

// ---------------- OBJ (tinyobjloader) ----------------
static bool LoadObj(const std::wstring& path, LiteMesh& m, LitePoints& c) {
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(WideToUtf8(path))) return false;
    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    if (attrib.vertices.empty()) return false;
    m.verts = attrib.vertices; // already xyz xyz...
    size_t totalIdx = 0;
    for (auto& s : shapes)
        for (size_t f = 0; f < s.mesh.num_face_vertices.size(); f++)
            totalIdx += (s.mesh.num_face_vertices[f] >= 3) ? (s.mesh.num_face_vertices[f] - 2) * 3 : 0;
    if (totalIdx == 0) { MeshVertsToCloud(m, c); return !c.empty(); } // vertex-only scan
    m.tris.reserve(totalIdx);
    for (auto& s : shapes) {
        size_t idxOff = 0;
        for (size_t f = 0; f < s.mesh.num_face_vertices.size(); f++) {
            size_t fv = s.mesh.num_face_vertices[f];
            if (fv >= 3) {
                int v0 = s.mesh.indices[idxOff].vertex_index;
                for (size_t k = 1; k + 1 < fv; k++) {
                    int v1 = s.mesh.indices[idxOff + k].vertex_index;
                    int v2 = s.mesh.indices[idxOff + k + 1].vertex_index;
                    if (v0 < 0 || v1 < 0 || v2 < 0) continue;
                    m.tris.push_back((uint32_t)v0); m.tris.push_back((uint32_t)v1); m.tris.push_back((uint32_t)v2);
                }
            }
            idxOff += fv;
        }
    }
    return !m.tris.empty();
}

// ---------------- 3MF (miniz + tag scan) ----------------
static void Scan3mfModel(const char* xml, size_t len, LiteMesh& m) {
    // collect vertices
    const char* p = xml; const char* end = xml + len;
    auto parseFloats = [](const char* tag, const char* tagEnd, float& x, float& y, float& z) {
        auto findAttr = [&](char a) -> const char* {
            for (const char* q = tag; q < tagEnd; q++) {
                if (*q == a && (q == tag || q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r') && q + 2 < tagEnd && q[1] == '=') {
                    const char* v = q + 2;
                    if (*v == '"' || *v == '\'') v++;
                    return v;
                }
            }
            return nullptr;
        };
        const char* xs = findAttr('x'); const char* ys = findAttr('y'); const char* zs = findAttr('z');
        if (!xs || !ys || !zs) return false;
        x = (float)atof(xs); y = (float)atof(ys); z = (float)atof(zs);
        return true;
    };
    auto parseIdx = [](const char* tag, const char* tagEnd, char a) -> long {
        for (const char* q = tag; q < tagEnd; q++) {
            if (*q == a && (q == tag || q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r') && q + 2 < tagEnd && q[1] == '=') {
                const char* v = q + 2;
                if (*v == '"' || *v == '\'') v++;
                return atol(v);
            }
        }
        return -1;
    };
    size_t vbase = m.verts.size() / 3;
    while (p < end) {
        const char* lt = (const char*)memchr(p, '<', (size_t)(end - p));
        if (!lt) break;
        const char* gt = (const char*)memchr(lt, '>', (size_t)(end - lt));
        if (!gt) break;
        size_t tlen = (size_t)(gt - lt - 1);
        const char* tag = lt + 1;
        while (tlen && (*tag == ' ' || *tag == '\t' || *tag == '\n' || *tag == '\r')) { tag++; tlen--; }
        if (tlen >= 6 && !strncmp(tag, "vertex", 6) && (tlen == 6 || tag[6]==' '||tag[6]=='\t'||tag[6]=='/'||tag[6]=='\n'||tag[6]=='\r')) {
            float x, y, z;
            if (parseFloats(tag, gt, x, y, z)) { m.verts.push_back(x); m.verts.push_back(y); m.verts.push_back(z); }
        } else if (tlen >= 8 && !strncmp(tag, "triangle", 8) && (tlen == 8 || tag[8]==' '||tag[8]=='\t'||tag[8]=='/'||tag[8]=='\n'||tag[8]=='\r')) {
            long a = parseIdx(tag, gt, '1'); // v1 / honour both spellings below
            // triangle uses v1= v2= v3=
            long v1 = -1, v2 = -1, v3 = -1;
            for (const char* q = tag; q < gt; q++) {
                if ((q == tag || q[-1]==' '||q[-1]=='\t'||q[-1]=='\n'||q[-1]=='\r') && q+3 < gt && q[0]=='v' && q[2]=='=') {
                    const char* val = q + 3; if (*val=='"'||*val=='\'') val++;
                    long idx = atol(val);
                    if (q[1]=='1') v1 = idx; else if (q[1]=='2') v2 = idx; else if (q[1]=='3') v3 = idx;
                }
            }
            (void)a;
            if (v1 >= 0 && v2 >= 0 && v3 >= 0) {
                m.tris.push_back((uint32_t)(vbase + v1));
                m.tris.push_back((uint32_t)(vbase + v2));
                m.tris.push_back((uint32_t)(vbase + v3));
            }
        }
        p = gt + 1;
    }
}

static bool Load3mf(const std::wstring& path, LiteMesh& m) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, WideToUtf8(path).c_str(), 0)) return false;
    bool ok = false;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; i++) {
        char name[512];
        if (mz_zip_reader_get_filename(&zip, i, name, sizeof(name)) == 0) continue;
        size_t L = strlen(name);
        if (L > 6 && _stricmp(name + L - 6, ".model") == 0) {
            size_t ulen = 0;
            void* data = mz_zip_reader_extract_file_to_heap(&zip, name, &ulen, 0);
            if (data && ulen) { Scan3mfModel((const char*)data, ulen, m); ok = !m.tris.empty(); }
            mz_free(data);
            if (ok) break;
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

// ---------------- glTF/GLB (cgltf) ----------------
static void CgltfPushPrimitive(cgltf_primitive* prim, const float* mat, LiteMesh& m) {
    cgltf_accessor* posAcc = nullptr;
    for (size_t a = 0; a < prim->attributes_count; a++)
        if (prim->attributes[a].type == cgltf_attribute_type_position) { posAcc = prim->attributes[a].data; break; }
    if (!posAcc || posAcc->type != cgltf_type_vec3) return;
    size_t nv = posAcc->count;
    uint32_t base = (uint32_t)(m.verts.size() / 3);
    for (size_t i = 0; i < nv; i++) {
        cgltf_float v[3] = {0, 0, 0};
        if (!cgltf_accessor_read_float(posAcc, i, v, 3)) return;
        float x = v[0], y = v[1], z = v[2];
        // column-major mat
        m.verts.push_back(mat[0]*x + mat[4]*y + mat[8]*z + mat[12]);
        m.verts.push_back(mat[1]*x + mat[5]*y + mat[9]*z + mat[13]);
        m.verts.push_back(mat[2]*x + mat[6]*y + mat[10]*z + mat[14]);
    }
    auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a < nv && b < nv && c < nv) {
            m.tris.push_back(base + a); m.tris.push_back(base + b); m.tris.push_back(base + c);
        }
    };
    if (prim->indices) {
        size_t nic = prim->indices->count;
        auto ridx = [&](size_t k) -> uint32_t { return (uint32_t)cgltf_accessor_read_index(prim->indices, k); };
        if (prim->type == cgltf_primitive_type_triangles) {
            for (size_t i = 0; i + 2 < nic; i += 3) pushTri(ridx(i), ridx(i+1), ridx(i+2));
        } else if (prim->type == cgltf_primitive_type_triangle_strip) {
            for (size_t i = 0; i + 2 < nic; i++) {
                if (i & 1) pushTri(ridx(i), ridx(i+2), ridx(i+1));
                else pushTri(ridx(i), ridx(i+1), ridx(i+2));
            }
        } else if (prim->type == cgltf_primitive_type_triangle_fan) {
            uint32_t i0 = ridx(0);
            for (size_t i = 1; i + 1 < nic; i++) pushTri(i0, ridx(i), ridx(i+1));
        }
    } else {
        if (prim->type == cgltf_primitive_type_triangles) {
            for (size_t i = 0; i + 2 < nv; i += 3) pushTri((uint32_t)i, (uint32_t)i+1, (uint32_t)i+2);
        }
    }
}

static void CgltfPushNode(cgltf_node* node, const float* parent, LiteMesh& m) {
    float local[16], world[16];
    cgltf_node_transform_local(node, local);
    // world = parent * local (column-major)
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
        world[c*4+r] = parent[0*4+r]*local[c*4+0] + parent[1*4+r]*local[c*4+1] +
                       parent[2*4+r]*local[c*4+2] + parent[3*4+r]*local[c*4+3];
    }
    if (node->mesh) {
        for (size_t p = 0; p < node->mesh->primitives_count; p++)
            CgltfPushPrimitive(&node->mesh->primitives[p], world, m);
    }
    for (size_t c = 0; c < node->children_count; c++)
        CgltfPushNode(node->children[c], world, m);
}

static bool LoadGltf(const std::wstring& path, LiteMesh& m) {
    cgltf_options opt{}; cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, WideToUtf8(path).c_str(), &data) != cgltf_result_success) return false;
    bool ok = false;
    if (cgltf_load_buffers(&opt, data, WideToUtf8(path).c_str()) == cgltf_result_success) {
        float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        if (data->scene) {
            for (size_t i = 0; i < data->scene->nodes_count; i++)
                CgltfPushNode(data->scene->nodes[i], ident, m);
        } else {
            for (size_t i = 0; i < data->nodes_count; i++)
                if (!data->nodes[i].parent) CgltfPushNode(&data->nodes[i], ident, m);
        }
        ok = !m.tris.empty();
    }
    cgltf_free(data);
    return ok;
}

// ---------------- FBX (ufbx) ----------------
static bool LoadFbx(const std::wstring& path, LiteMesh& m) {
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(WideToUtf8(path).c_str(), nullptr, &err);
    if (!scene) return false;
    for (size_t ni = 0; ni < scene->nodes.count; ni++) {
        ufbx_node* node = scene->nodes.data[ni];
        if (!node->mesh) continue;
        ufbx_mesh* mesh = node->mesh;
        if (mesh->faces.count == 0) continue;
        uint32_t base = (uint32_t)(m.verts.size() / 3);
        // copy unique vertices
        for (size_t vi = 0; vi < mesh->vertices.count; vi++) {
            ufbx_vec3 p = mesh->vertices.data[vi];
            // node_to_world: cols
            float x = (float)(node->node_to_world.cols[0].x*p.x + node->node_to_world.cols[1].x*p.y + node->node_to_world.cols[2].x*p.z + node->node_to_world.cols[3].x);
            float y = (float)(node->node_to_world.cols[0].y*p.x + node->node_to_world.cols[1].y*p.y + node->node_to_world.cols[2].y*p.z + node->node_to_world.cols[3].y);
            float z = (float)(node->node_to_world.cols[0].z*p.x + node->node_to_world.cols[1].z*p.y + node->node_to_world.cols[2].z*p.z + node->node_to_world.cols[3].z);
            m.verts.push_back(x); m.verts.push_back(y); m.verts.push_back(z);
        }
        for (size_t fi = 0; fi < mesh->faces.count; fi++) {
            ufbx_face f = mesh->faces.data[fi];
            if (f.num_indices < 3) continue;
            uint32_t i0 = mesh->vertex_indices.data[f.index_begin];
            for (uint32_t k = 1; k + 1 < f.num_indices; k++) {
                uint32_t i1 = mesh->vertex_indices.data[f.index_begin + k];
                uint32_t i2 = mesh->vertex_indices.data[f.index_begin + k + 1];
                m.tris.push_back(base + i0); m.tris.push_back(base + i1); m.tris.push_back(base + i2);
            }
        }
    }
    ufbx_free_scene(scene);
    return !m.tris.empty();
}

// ---------------- PLY ----------------
static bool LoadPly(const std::wstring& path, LiteMesh& m, LitePoints& c) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return false;
    char line[1024];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
    if (strncmp(line, "ply", 3)) { fclose(f); return false; }
    bool binaryLE = false;
    long nVerts = 0, nFaces = 0;
    struct Prop { int size; int listCountSize; int listIndexSize; std::string name; };
    std::vector<Prop> vProps, fProps;
    bool inVertex = false, inFace = false;
    auto typeSize = [](const std::string& t) -> int {
        if (t=="int8"||t=="uint8"||t=="uchar"||t=="char") return 1;
        if (t=="int16"||t=="uint16"||t=="ushort"||t=="short") return 2;
        if (t=="int32"||t=="uint32"||t=="int"||t=="uint"||t=="float"||t=="float32") return 4;
        if (t=="float64"||t=="double") return 8;
        return 0;
    };
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "format ascii", 12)) binaryLE = false;
        else if (!strncmp(line, "format binary_little_endian", 27)) binaryLE = true;
        else if (!strncmp(line, "format binary_big_endian", 24)) { fclose(f); return false; }
        else if (!strncmp(line, "element vertex", 14)) { nVerts = atol(line + 14); inVertex = true; inFace = false; }
        else if (!strncmp(line, "element face", 12)) { nFaces = atol(line + 12); inVertex = false; inFace = true; }
        else if (!strncmp(line, "element ", 8)) { inVertex = inFace = false; }
        else if (!strncmp(line, "property list", 13)) {
            std::istringstream ss(line);
            std::string w, ct, it, nm; ss >> w >> w >> ct >> it >> nm;
            if (!nm.empty() && nm.back() == '\r') nm.pop_back(); // CRLF headers
            Prop p{0, typeSize(ct), typeSize(it), nm};
            if (inFace) fProps.push_back(p);
        }
        else if (!strncmp(line, "property", 8)) {
            std::istringstream ss(line);
            std::string w, t, nm; ss >> w >> t >> nm;
            if (!nm.empty() && nm.back() == '\r') nm.pop_back(); // CRLF headers
            Prop p{typeSize(t), 0, 0, nm};
            if (inVertex) vProps.push_back(p);
            else if (inFace) fProps.push_back(p);
        }
        else if (!strncmp(line, "end_header", 10)) break;
    }
    auto findIdx = [](const std::vector<Prop>& v, const char* n) -> int {
        for (size_t i = 0; i < v.size(); i++) if (v[i].name == n) return (int)i;
        return -1;
    };
    int vX = findIdx(vProps, "x"), vY = findIdx(vProps, "y"), vZ = findIdx(vProps, "z");
    if (nVerts <= 0 || vX < 0 || vY < 0 || vZ < 0) { fclose(f); return false; }
    int vStride = 0, vOff[3] = {0, 0, 0}, vSize[3] = {4, 4, 4};
    {
        int off = 0;
        for (size_t i = 0; i < vProps.size(); i++) {
            if ((int)i == vX) { vOff[0] = off; vSize[0] = vProps[i].size; }
            if ((int)i == vY) { vOff[1] = off; vSize[1] = vProps[i].size; }
            if ((int)i == vZ) { vOff[2] = off; vSize[2] = vProps[i].size; }
            off += vProps[i].size;
        }
        vStride = off;
    }
    if (!vStride) { fclose(f); return false; }
    m.verts.reserve((size_t)nVerts * 3);
    if (!binaryLE) {
        for (long i = 0; i < nVerts; i++) {
            if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
            // pick x/y/z tokens by position among whitespace tokens is unreliable;
            // parse all floats, take positions vX/vY/vZ
            float vals[64]; int nv = 0;
            const char* q = line;
            while (nv < 64) {
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                if (!*q) break;
                char* e = nullptr; float v = strtof(q, &e);
                if (e == q) break;
                vals[nv++] = v; q = e;
            }
            if (vX >= nv || vY >= nv || vZ >= nv) { fclose(f); return false; }
            m.verts.push_back(vals[vX]); m.verts.push_back(vals[vY]); m.verts.push_back(vals[vZ]);
        }
        for (long i = 0; i < nFaces; i++) {
            if (!fgets(line, sizeof(line), f)) break;
            int cnt = 0; long idx[64];
            const char* q = line; char* e = nullptr;
            cnt = (int)strtol(q, &e, 10); q = e;
            int got = 0;
            while (got < cnt && got < 64) {
                while (*q==' '||*q=='\t') q++;
                if (!*q || *q=='\n' || *q=='\r') break;
                idx[got++] = strtol(q, &e, 10); q = e;
            }
            if (got >= 3) {
                for (int k = 1; k + 1 < got; k++) {
                    m.tris.push_back((uint32_t)idx[0]);
                    m.tris.push_back((uint32_t)idx[k]);
                    m.tris.push_back((uint32_t)idx[k+1]);
                }
            }
        }
    } else {
        auto readCoord = [](const char* base, int off, int sz) -> float {
            if (sz == 8) { double d = 0; memcpy(&d, base + off, 8); return (float)d; }
            float f = 0; memcpy(&f, base + off, 4); return f;
        };
        std::vector<char> row((size_t)vStride);
        for (long i = 0; i < nVerts; i++) {
            if (fread(row.data(), 1, row.size(), f) != row.size()) { fclose(f); return false; }
            m.verts.push_back(readCoord(row.data(), vOff[0], vSize[0]));
            m.verts.push_back(readCoord(row.data(), vOff[1], vSize[1]));
            m.verts.push_back(readCoord(row.data(), vOff[2], vSize[2]));
        }
        // face list property + any trailing per-face fields
        int listPos = -1, listCS = 1, listIS = 4, trailBytes = 0;
        for (size_t i = 0; i < fProps.size(); i++) {
            if (fProps[i].listCountSize) {
                listPos = (int)i; listCS = fProps[i].listCountSize; listIS = fProps[i].listIndexSize;
            } else if (listPos >= 0) {
                trailBytes += fProps[i].size;
            }
        }
        if (listPos < 0 || !listCS || !listIS) {
            // no faces (e.g. vertex-only cloud): fall through to cloud fallback
            fclose(f);
            if (!m.verts.empty()) MeshVertsToCloud(m, c);
            return !c.empty();
        }
        for (long i = 0; i < nFaces; i++) {
            // skip leading fixed fields (uncommon, but be safe)
            for (int k = 0; k < listPos; k++) {
                if (fProps[(size_t)k].size <= 0) { fclose(f); return false; }
                if (fseek(f, fProps[(size_t)k].size, SEEK_CUR)) { fclose(f); return false; }
            }
            unsigned char cb[4] = {0, 0, 0, 0};
            if (fread(cb, 1, (size_t)listCS, f) != (size_t)listCS) break;
            uint32_t cnt = listCS == 1 ? cb[0] : (listCS == 2 ? (uint32_t)(cb[0] | (cb[1] << 8))
                : (uint32_t)(cb[0] | (cb[1] << 8) | (cb[2] << 16) | ((uint32_t)cb[3] << 24)));
            if (cnt > 1024) { fclose(f); return false; }
            std::vector<unsigned char> ib((size_t)cnt * (size_t)listIS);
            if (cnt && fread(ib.data(), 1, ib.size(), f) != ib.size()) break;
            auto getIdx = [&](uint32_t k) -> uint32_t {
                const unsigned char* p = ib.data() + (size_t)k * (size_t)listIS;
                if (listIS == 1) return p[0];
                if (listIS == 2) return (uint32_t)(p[0] | (p[1] << 8));
                return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
            };
            if (cnt >= 3) {
                uint32_t i0 = getIdx(0);
                if (i0 >= (uint32_t)nVerts) { fclose(f); return false; }
                for (uint32_t k = 1; k + 1 < cnt; k++) {
                    uint32_t a = getIdx(k), b = getIdx(k + 1);
                    if (a >= (uint32_t)nVerts || b >= (uint32_t)nVerts) { fclose(f); return false; }
                    m.tris.push_back(i0); m.tris.push_back(a); m.tris.push_back(b);
                }
            }
            if (trailBytes && fseek(f, trailBytes, SEEK_CUR)) { fclose(f); return false; }
        }
    }
    fclose(f);
    if (m.tris.empty() && !m.verts.empty()) MeshVertsToCloud(m, c); // vertex-only cloud
    return !m.tris.empty() || !c.empty();
}

// ---------------- LAS (uncompressed, hand parser) ----------------
static bool LoadLas(const std::wstring& path, LitePoints& c) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return false;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "LASF", 4)) { fclose(f); return false; }
    fseek(f, 24, SEEK_SET);
    unsigned char verMaj = 0, verMin = 0;
    fread(&verMaj, 1, 1, f); fread(&verMin, 1, 1, f);
    fseek(f, 94, SEEK_SET);
    uint16_t headerSize = 0; uint32_t dataOffset = 0;
    fread(&headerSize, 2, 1, f); fread(&dataOffset, 4, 1, f);
    fseek(f, 104, SEEK_SET);
    unsigned char fmt = 0; uint16_t recLen = 0; uint32_t count32 = 0;
    fread(&fmt, 1, 1, f); fread(&recLen, 2, 1, f); fread(&count32, 4, 1, f);
    fseek(f, 131, SEEK_SET);
    double sx=0, sy=0, sz=0, ox=0, oy=0, oz=0;
    fread(&sx, 8, 1, f); fread(&sy, 8, 1, f); fread(&sz, 8, 1, f);
    fread(&ox, 8, 1, f); fread(&oy, 8, 1, f); fread(&oz, 8, 1, f);
    uint64_t count = count32;
    if (verMaj == 1 && verMin == 4) {
        fseek(f, 247, SEEK_SET);
        fread(&count, 8, 1, f);
    }
    if (count == 0 || recLen < 20 || sx == 0) { fclose(f); return false; }
    const size_t kMax = 300000;
    uint64_t step = (count + kMax - 1) / kMax;
    if (!step) step = 1;
    size_t n = (size_t)((count + step - 1) / step);
    c.xyz.reserve(n * 3); c.val.reserve(n);
    std::vector<char> rec(recLen);
    double minI = 1e300, maxI = -1e300;
    std::vector<uint16_t> intens; intens.reserve(n);
    for (uint64_t i = 0; i < count; i += step) {
        fseek(f, (long)(dataOffset + i * recLen), SEEK_SET);
        if (fread(rec.data(), 1, recLen, f) != recLen) break;
        int32_t X, Y, Z; uint16_t inten;
        memcpy(&X, rec.data(), 4); memcpy(&Y, rec.data()+4, 4); memcpy(&Z, rec.data()+8, 4);
        memcpy(&inten, rec.data()+12, 2);
        c.xyz.push_back((float)(X*sx+ox)); c.xyz.push_back((float)(Y*sy+oy)); c.xyz.push_back((float)(Z*sz+oz));
        intens.push_back(inten);
        if (inten < minI) minI = inten; if (inten > maxI) maxI = inten;
    }
    fclose(f);
    if (c.xyz.empty()) return false;
    double span = maxI - minI;
    for (auto v : intens) c.val.push_back(span > 0 ? (float)((v - minI) / span) : 0.5f);
    return true;
}

// ---------------- LAZ (LASzip C API) ----------------
static bool LoadLaz(const std::wstring& path, LitePoints& c) {
    laszip_POINTER reader = nullptr;
    if (laszip_create(&reader)) return false;
    laszip_BOOL compressed = 0;
    bool ok = false;
    if (laszip_open_reader(reader, WideToUtf8(path).c_str(), &compressed) == 0) {
        laszip_header* hdr = nullptr;
        laszip_get_header_pointer(reader, &hdr);
        if (hdr) {
            uint64_t count = hdr->extended_number_of_point_records ?
                hdr->extended_number_of_point_records : hdr->number_of_point_records;
            if (count) {
                const size_t kMax = 300000;
                uint64_t step = (count + kMax - 1) / kMax;
                if (!step) step = 1;
                size_t n = (size_t)((count + step - 1) / step);
                c.xyz.reserve(n * 3); c.val.reserve(n);
                std::vector<uint16_t> intens; intens.reserve(n);
                double minI = 1e300, maxI = -1e300;
                for (uint64_t i = 0; i < count; i += step) {
                    if (laszip_seek_point(reader, (laszip_I64)i)) break;
                    if (laszip_read_point(reader)) break;
                    laszip_point* p = nullptr;
                    laszip_get_point_pointer(reader, &p);
                    if (!p) break;
                    c.xyz.push_back((float)(p->X*hdr->x_scale_factor+hdr->x_offset));
                    c.xyz.push_back((float)(p->Y*hdr->y_scale_factor+hdr->y_offset));
                    c.xyz.push_back((float)(p->Z*hdr->z_scale_factor+hdr->z_offset));
                    intens.push_back(p->intensity);
                    if (p->intensity < minI) minI = p->intensity;
                    if (p->intensity > maxI) maxI = p->intensity;
                }
                if (!c.xyz.empty()) {
                    double span = maxI - minI;
                    for (auto v : intens) c.val.push_back(span > 0 ? (float)((v - minI) / span) : 0.5f);
                    ok = true;
                }
            }
        }
        laszip_close_reader(reader);
    }
    laszip_destroy(reader);
    return ok;
}

// ---------------- dispatch ----------------
bool LoadModelFile(const std::wstring& path, LiteMesh& outMesh, LitePoints& outCloud) {
    outMesh.clear(); outCloud.clear();
    std::wstring ext = FileExtensionOf(path);
    if (ext == L".stl") return LoadStl(path, outMesh);
    if (ext == L".obj") return LoadObj(path, outMesh, outCloud);
    if (ext == L".3mf") return Load3mf(path, outMesh);
    if (ext == L".glb" || ext == L".gltf") return LoadGltf(path, outMesh);
    if (ext == L".fbx") return LoadFbx(path, outMesh);
    if (ext == L".ply") return LoadPly(path, outMesh, outCloud);
    if (ext == L".las") return LoadLas(path, outCloud);
    if (ext == L".laz") return LoadLaz(path, outCloud);
    // unknown extension: try content sniffing fallback
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
        char head[8192]; size_t n = fread(head, 1, sizeof(head), f); fclose(f);
        std::wstring sniff = SniffModelExtension(head, n);
        if (sniff == L".stl") return LoadStl(path, outMesh);
        if (sniff == L".obj") return LoadObj(path, outMesh, outCloud);
        if (sniff == L".3mf") return Load3mf(path, outMesh);
        if (sniff == L".glb" || sniff == L".gltf") return LoadGltf(path, outMesh);
    }
    return false;
}
