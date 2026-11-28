// XdbCodec.h
// Compression of file data: LZO1X in most versions, LZHUF in 11xx.

#ifndef ZIP7_INC_XDB_CODEC_H
#define ZIP7_INC_XDB_CODEC_H

#include "Common/MyBuffer.h"

#include "XdbFormat.h"

namespace lzokay { class DictBase; }

namespace NArchive {
namespace NXdb {

// Decompresses (packed) into exactly (size) bytes. Returns false on corrupt data.
bool UnpackItemData(EMethod method, const Byte *packed, size_t packSize, size_t size, CByteBuffer &data);

// Compresses file data. Keeps the LZO dictionary (about 450 KB) between files.
class CItemPacker
{
  lzokay::DictBase *_lzoDict;

  CItemPacker(const CItemPacker &);
  CItemPacker &operator=(const CItemPacker &);

public:
  CItemPacker();
  ~CItemPacker();

  // Returns false if compression doesn't make the data strictly smaller;
  // readers only treat a file as compressed when its sizes differ.
  bool Pack(EMethod method, const Byte *data, size_t size, CByteBuffer &packed);
};

} // namespace NXdb
} // namespace NArchive

#endif
