// XdbCodec.cpp

#include "StdAfx.h"

#include "lzokay.hpp"

#include "XdbCodec.h"
#include "XdbLzhuf.h"

namespace NArchive {
namespace NXdb {

bool UnpackItemData(EMethod method, const Byte *packed, size_t packSize, size_t size, CByteBuffer &data)
{
  switch (method)
  {
    case kMethod_Store:
      if (packSize != size)
        return false;
      data.CopyFrom(packed, size);
      return true;

    case kMethod_Lzhuf:
      return NLzhuf::Decode(packed, packSize, data, size) && data.Size() == size;

    case kMethod_Lzo:
    {
      data.Alloc(size);
      size_t outSize = 0;
      return lzokay::decompress(packed, packSize, data, size, outSize) == lzokay::EResult::Success
          && outSize == size;
    }
  }
  return false;
}

CItemPacker::CItemPacker():
    _lzoDict(new lzokay::Dict<>)
{
}

CItemPacker::~CItemPacker()
{
  delete static_cast<lzokay::Dict<> *>(_lzoDict);
}

bool CItemPacker::Pack(EMethod method, const Byte *data, size_t size, CByteBuffer &packed)
{
  if (size == 0)
    return false;

  switch (method)
  {
    case kMethod_Store:
      return false;

    case kMethod_Lzhuf:
      NLzhuf::Encode(data, size, packed);
      return packed.Size() < size;

    case kMethod_Lzo:
    {
      CByteBuffer buffer(lzokay::compress_worst_size(size));
      size_t packedSize = 0;
      if (lzokay::compress(data, size, buffer, buffer.Size(), packedSize, *_lzoDict) != lzokay::EResult::Success
          || packedSize >= size)
        return false;
      packed.CopyFrom(buffer, packedSize);
      return true;
    }
  }
  return false;
}

} // namespace NXdb
} // namespace NArchive
