// XdbRegister.cpp

#include "StdAfx.h"

#include "CpuArch.h"

#include "7zip/Common/RegisterArc.h"

#include "XdbHandler.h"

namespace NArchive {
namespace NXdb {

static const char k_Exts[] =
    "db0 db1 db2 db3 db4 db5 db6 db7 db8 db9 dba dbb dbc dbd dbe dbf db "
    "xdb0 xdb1 xdb2 xdb3 xdb4 xdb5 xdb6 xdb7 xdb8 xdb9 xdba xdbb xdbc xdbd xdbe xdbf xdb";

// The id of the first chunk, in each order that writers use.
static const Byte k_Signature[] =
{
  4, 0x00, 0x00, 0x00, 0x00, // data
  4, 0x9A, 0x02, 0x00, 0x00, // userdata (xdb)
  4, 0x01, 0x00, 0x00, 0x80, // header, compressed
  4, 0x01, 0x00, 0x00, 0x00  // header
};

static bool IsKnownChunkId(UInt32 id)
{
  return id == NChunkId::kData
      || id == NChunkId::kUserData
      || (id & ~NChunkId::kCompressedFlag) == NChunkId::kHeader;
}

API_FUNC_static_IsArc IsArc_Xdb(const Byte *p, size_t size)
{
  if (size < kChunkHeaderSize)
    return k_IsArc_Res_NEED_MORE;
  const UInt32 id = GetUi32(p);
  if (!IsKnownChunkId(id))
    return k_IsArc_Res_NO;
  if (GetUi32(p + 4) != 0)
    return k_IsArc_Res_YES;

  // An empty first chunk must be followed by another of ours. Two empty data
  // chunks in a row are rejected, so files that merely start with zeros are.
  if (size < 2 * kChunkHeaderSize)
    return k_IsArc_Res_NEED_MORE;
  const UInt32 nextId = GetUi32(p + kChunkHeaderSize);
  if (!IsKnownChunkId(nextId) || (id == NChunkId::kData && nextId == NChunkId::kData))
    return k_IsArc_Res_NO;
  return k_IsArc_Res_YES;
}
}

REGISTER_ARC_IO(
    "xdb", k_Exts, NULL, 0xB0,
    k_Signature,
    0,
    NArcInfoFlags::kMultiSignature,
    0,
    IsArc_Xdb)

} // namespace NXdb
} // namespace NArchive
