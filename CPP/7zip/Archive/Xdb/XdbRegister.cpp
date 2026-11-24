// XdbRegister.cpp

#include "StdAfx.h"

#include "CpuArch.h"

#include "Windows/PropVariant.h"

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

/*
  Describes the properties of SetProperties() for the Tosox/7zip fork, which
  shows them as controls in the "Add to Archive" dialog. See DOC/format-options.md
  in https://github.com/Tosox/7zip. Stock 7-Zip never calls this.
*/
static const char k_FormatOptions[] =
    "version=1\n"
    "[dbver]\n"
    "type=enum\n"
    "values=auto|xdb|2947ww|2947ru|2945|2215|11xx\n"
    "labels=Auto|xdb (Clear Sky, Call of Pripyat, mods)|2947ww (Shadow of Chernobyl)"
        "|2947ru (Shadow of Chernobyl, Russian)|2945|2215|11xx\n"
    "label=DB version\n"
    "tip=Auto keeps the version of an existing archive and uses xdb for new ones\n"
    "[compress]\n"
    "type=enum\n"
    "values=auto|on|off\n"
    "labels=Auto|Always|Never\n"
    "label=Compression\n"
    "tip=Auto follows the archive being updated; new archives are compressed\n"
    "[xdb_ud]\n"
    "type=file\n"
    "label=Userdata file\n"
    "filter=LTX files|*.ltx\n"
    "tip=Its contents become the userdata block of an xdb archive\n"
    "[gen_xdb_ud]\n"
    "type=bool\n"
    "default=on\n"
    "label=Generate userdata for new xdb archives\n";

} // namespace NXdb
} // namespace NArchive

STDAPI GetFormatOptions(UInt32 formatIndex, PROPVARIANT *value)
{
  NWindows::NCOM::CPropVariant prop;
  if (formatIndex == 0)
    prop = NArchive::NXdb::k_FormatOptions;
  prop.Detach(value);
  return S_OK;
}
