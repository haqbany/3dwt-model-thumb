// ModelThumbLite - dependency-free software rasterizer -> HBITMAP.
#pragma once
#include "Geometry.h"
#include <windows.h>

// Renders at min(requested, 512)px. label (e.g. L"STL") is drawn as a
// small badge in the corner; nullptr skips it. Returns nullptr on failure.
HBITMAP RenderMeshThumb(const LiteMesh& mesh, UINT requested, const wchar_t* label = nullptr);
HBITMAP RenderCloudThumb(const LitePoints& cloud, UINT requested, const wchar_t* label = nullptr);
