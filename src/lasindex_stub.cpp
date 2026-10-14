// ModelThumbLite - stub for LASindex (spatial .lax indexing).
// Upstream LASzip master's lasindex.cpp is stale (references the removed
// lasreader.hpp framework) and only serves rectangle-query paths this
// thumbnail provider never uses (plain sequential seek+read). These no-op
// definitions satisfy the linker; indexed reads report "no index".
#define LASZIPDLL_EXPORTS
#include "../thirdparty/laszip/lasindex.hpp"
#include "../thirdparty/laszip/lasreadpoint.hpp"

LASindex::LASindex() {}
LASindex::~LASindex() {}
void LASindex::prepare(LASquadtree*, I32) {}
BOOL LASindex::add(const F64, const F64, const U64) { return TRUE; }
void LASindex::complete(U32, I32) {}
BOOL LASindex::read(const char*) { return FALSE; }
BOOL LASindex::write(const char*) const { return FALSE; }
BOOL LASindex::intersect_rectangle(const F64, const F64, const F64, const F64) { return FALSE; }
BOOL LASindex::seek_next(LASreadPoint*, I64&) { return FALSE; }
