// ModelThumbLite - format loaders. See Geometry.h header note for attribution.
#pragma once
#include "Geometry.h"
#include <string>

// Loads any supported file into either a mesh or a point cloud.
// Returns true on success (exactly one of outMesh/outCloud is filled).
bool LoadModelFile(const std::wstring& path, LiteMesh& outMesh, LitePoints& outCloud);

// Content sniffing for IInitializeWithStream (mirrors the NCrews approach:
// GLB magic, ZIP/3MF signature, JSON, ASCII STL, binary STL size, OBJ tokens).
// Returns extension like L".glb", or empty when unknown.
std::wstring SniffModelExtension(const char* data, size_t size);
