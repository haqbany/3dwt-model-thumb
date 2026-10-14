// ModelThumbLite - lightweight Explorer thumbnail provider.
// COM architecture follows the method of NCrewsShellExtensions by Nathan Crews
// (BSD-style licensed, https://github.com/nathancrews/NCrewsShellExtensions):
//   in-proc COM DLL, IThumbnailProvider + IInitializeWithFile/Stream/Item,
//   per-extension shellex registration under HKCU. All rendering code here is
//   original and dependency-free (no Open3D). See ATTRIBUTION.md.
#pragma once
#include <cstdint>
#include <vector>

struct LiteMesh {
    std::vector<float> verts;   // xyz xyz ...
    std::vector<uint32_t> tris; // i0 i1 i2 ...
    size_t triCount() const { return tris.size() / 3; }
    size_t vertCount() const { return verts.size() / 3; }
    void clear() { verts.clear(); tris.clear(); }
    bool empty() const { return tris.empty(); }
};

struct LitePoints {
    std::vector<float> xyz; // xyz xyz ...
    std::vector<float> val; // 0..1 per point (intensity or height), same count
    size_t count() const { return xyz.size() / 3; }
    void clear() { xyz.clear(); val.clear(); }
    bool empty() const { return xyz.empty(); }
};
