// ModelThumbLite - stubs for LAZ *write* path (LASwritePoint).
// Same rationale as arithmeticencoder_stub.cpp: never used, linker-only.
#include "../thirdparty/laszip/laswritepoint.hpp"

LASwritePoint::LASwritePoint() : outstream(nullptr), num_writers(0), writers(nullptr),
    writers_raw(nullptr), writers_compressed(nullptr), enc(nullptr),
    layered_las14_compression(FALSE), chunk_size(0), chunk_count(0), number_chunks(0),
    alloced_chunks(0), chunk_sizes(nullptr), chunk_bytes(nullptr),
    chunk_start_position(0), chunk_table_start_position(0) {}
LASwritePoint::~LASwritePoint() {}
BOOL LASwritePoint::setup(const U32, const LASitem*, const LASzip*) { return FALSE; }
BOOL LASwritePoint::init(ByteStreamOut*) { return FALSE; }
BOOL LASwritePoint::write(const U8* const*) { return FALSE; }
BOOL LASwritePoint::chunk() { return FALSE; }
BOOL LASwritePoint::done() { return FALSE; }
// Upstream LASzip master ships only the decoder implementation; the encoder
// is declared but never defined. This provider only ever decompresses, so
// these are unreachable no-ops that satisfy the linker.
#include "../thirdparty/laszip/arithmeticencoder.hpp"
#include "../thirdparty/laszip/arithmeticmodel.hpp"

ArithmeticEncoder::ArithmeticEncoder() : outstream(nullptr), outbuffer(nullptr),
    endbuffer(nullptr), outbyte(nullptr), endbyte(nullptr), base(0), length(0) {}
ArithmeticEncoder::~ArithmeticEncoder() {}
BOOL ArithmeticEncoder::init(ByteStreamOut* o) { outstream = o; return TRUE; }
void ArithmeticEncoder::done() {}
ArithmeticBitModel* ArithmeticEncoder::createBitModel() { return nullptr; }
void ArithmeticEncoder::initBitModel(ArithmeticBitModel*) {}
void ArithmeticEncoder::destroyBitModel(ArithmeticBitModel*) {}
ArithmeticModel* ArithmeticEncoder::createSymbolModel(U32) { return nullptr; }
void ArithmeticEncoder::initSymbolModel(ArithmeticModel*, U32*) {}
void ArithmeticEncoder::destroySymbolModel(ArithmeticModel*) {}
void ArithmeticEncoder::encodeBit(ArithmeticBitModel*, U32) {}
void ArithmeticEncoder::encodeSymbol(ArithmeticModel*, U32) {}
void ArithmeticEncoder::writeBit(U32) {}
void ArithmeticEncoder::writeBits(U32, U32) {}
void ArithmeticEncoder::writeByte(U8) {}
void ArithmeticEncoder::writeShort(U16) {}
void ArithmeticEncoder::writeInt(U32) {}
void ArithmeticEncoder::writeFloat(F32) {}
void ArithmeticEncoder::writeInt64(U64) {}
void ArithmeticEncoder::writeDouble(F64) {}
