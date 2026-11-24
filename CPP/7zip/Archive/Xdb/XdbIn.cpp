// XdbIn.cpp

#include "StdAfx.h"

#include <string.h>

#include "CpuArch.h"

#include "7zip/Common/StreamUtils.h"

#include "XdbHeader.h"
#include "XdbIn.h"
#include "XdbLzhuf.h"
#include "XdbScrambler.h"

namespace NArchive {
namespace NXdb {

// Versions in the order they are tried. Each one is accepted only if its file
// table passes all checks (see docs/db-format.md, "Telling the versions apart").
static const EVersion kCandidates[] =
{
  kVersion_Xdb,
  kVersion_2945,
  kVersion_2215,
  kVersion_11xx,
  kVersion_2947ww,
  kVersion_2947ru
};

// Real archives have three chunks. A long chain means the file is something else.
static const unsigned kMaxNumChunks = 64;

// Limits that only corrupt archives exceed.
static const UInt32 kMaxHeaderChunkSize = (UInt32)1 << 28;
static const size_t kMaxHeaderSize = (size_t)1 << 28;
static const UInt32 kMaxUserDataSize = (UInt32)1 << 24;

void CInArchive::Clear()
{
  Items.Clear();
  Version = kVersion_Xdb;
  UserData.Free();
  UserDataDefined = false;
  PhySize = 0;
  HeadersError = false;
  _data = CChunk();
  _header = CChunk();
  _userData = CChunk();
}

HRESULT CInArchive::ReadChunks(IInStream *stream, UInt64 fileSize)
{
  UInt64 pos = 0;
  for (unsigned numChunks = 0; fileSize - pos >= kChunkHeaderSize; numChunks++)
  {
    if (numChunks == kMaxNumChunks)
      return S_FALSE;

    Byte buf[kChunkHeaderSize];
    RINOK(InStream_SeekSet(stream, pos))
    RINOK(ReadStream_FALSE(stream, buf, kChunkHeaderSize))

    CChunk chunk;
    chunk.Id = GetUi32(buf);
    chunk.Size = GetUi32(buf + 4);
    chunk.Pos = pos + kChunkHeaderSize;
    chunk.Found = true;
    if (chunk.Size > fileSize - chunk.Pos)
      break; // a truncated or foreign tail: the archive ends at the last whole chunk

    switch (chunk.Id & ~NChunkId::kCompressedFlag)
    {
      case NChunkId::kData:
        if (!_data.Found)
          _data = chunk;
        break;
      case NChunkId::kHeader:
        if (!_header.Found)
          _header = chunk;
        break;
      case NChunkId::kUserData:
        if (!_userData.Found)
          _userData = chunk;
        break;
    }

    pos = chunk.Pos + chunk.Size;
    PhySize = pos;
  }
  return (_data.Found && _header.Found) ? S_OK : S_FALSE;
}

bool CInArchive::DecodeHeader(const CByteBuffer &payload, bool isCompressed, const EVersion *forcedVersion)
{
  const UInt64 dataBegin = _data.Pos;
  const UInt64 dataEnd = _data.Pos + _data.Size;

  // All unscrambled versions share one decoded table.
  CByteBuffer plainTable;
  bool plainTableTried = false;
  bool plainTableValid = false;

  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kCandidates); i++)
  {
    const EVersion version = kCandidates[i];
    if (forcedVersion && version != *forcedVersion)
      continue;
    const CVersionInfo &info = GetVersionInfo(version);

    CByteBuffer scrambledTable;
    const CByteBuffer *table;
    if (info.IsScrambled)
    {
      // Only compressed headers are scrambled.
      if (!isCompressed)
        continue;
      CByteBuffer decrypted(payload);
      CScrambler(info.ScramblerKey).Decrypt(decrypted, decrypted.Size());
      if (!NLzhuf::Decode(decrypted, decrypted.Size(), scrambledTable, kMaxHeaderSize))
        continue;
      table = &scrambledTable;
    }
    else
    {
      if (!plainTableTried)
      {
        plainTableTried = true;
        if (isCompressed)
          plainTableValid = NLzhuf::Decode(payload, payload.Size(), plainTable, kMaxHeaderSize);
        else
        {
          plainTable = payload;
          plainTableValid = true;
        }
      }
      if (!plainTableValid)
        continue;
      table = &plainTable;
    }

    CObjectVector<CItem> items;
    if (ParseHeader(version, *table, table->Size(), items) && AreItemsInRange(items, dataBegin, dataEnd))
    {
      Items = items;
      Version = version;
      return true;
    }
  }
  return false;
}

// 11xx stores no real size for compressed files; it is the first field of
// each file's LZHUF stream.
HRESULT CInArchive::ReadLzhufSizes(IInStream *stream, IArchiveOpenCallback *callback)
{
  FOR_VECTOR (i, Items)
  {
    CItem &item = Items[i];
    if (item.Method != kMethod_Lzhuf || item.PackSize < NLzhuf::kSizePrefixSize)
      continue;
    Byte buf[NLzhuf::kSizePrefixSize];
    RINOK(InStream_SeekSet(stream, item.Offset))
    RINOK(ReadStream_FALSE(stream, buf, sizeof(buf)))
    item.Size = GetUi32(buf);

    if (callback && (i & 0xFF) == 0)
    {
      const UInt64 numFiles = i;
      RINOK(callback->SetCompleted(&numFiles, NULL))
    }
  }
  return S_OK;
}

HRESULT CInArchive::ReadUserData(IInStream *stream)
{
  if (!_userData.Found || _userData.Size > kMaxUserDataSize)
    return S_OK;
  UserData.Alloc(_userData.Size);
  RINOK(InStream_SeekSet(stream, _userData.Pos))
  RINOK(ReadStream_FALSE(stream, UserData, UserData.Size()))
  UserDataDefined = true;
  return S_OK;
}

HRESULT CInArchive::Open(IInStream *stream, const EVersion *forcedVersion, IArchiveOpenCallback *callback)
{
  Clear();

  UInt64 fileSize;
  RINOK(InStream_GetSize_SeekToEnd(stream, fileSize))
  if (callback)
  {
    RINOK(callback->SetTotal(NULL, &fileSize))
  }

  {
    const HRESULT res = ReadChunks(stream, fileSize);
    if (res != S_OK)
      return res;
  }

  if (_header.Size > kMaxHeaderChunkSize)
    HeadersError = true;
  else
  {
    CByteBuffer payload(_header.Size);
    RINOK(InStream_SeekSet(stream, _header.Pos))
    RINOK(ReadStream_FALSE(stream, payload, payload.Size()))
    const bool isCompressed = (_header.Id & NChunkId::kCompressedFlag) != 0;
    if (!DecodeHeader(payload, isCompressed, forcedVersion))
      HeadersError = true;
  }

  if (!HeadersError && Version == kVersion_11xx)
  {
    RINOK(ReadLzhufSizes(stream, callback))
  }

  RINOK(ReadUserData(stream))

  if (callback)
  {
    const UInt64 numFiles = Items.Size();
    RINOK(callback->SetCompleted(&numFiles, &fileSize))
  }
  return S_OK;
}


static int CompareIndicesByName(const unsigned *a, const unsigned *b, void *param)
{
  const CObjectVector<AString> &names = *(const CObjectVector<AString> *)param;
  const int res = strcmp(names[*a], names[*b]);
  if (res != 0)
    return res;
  return MyCompare(*a, *b);
}

void RemoveDuplicateItems(CObjectVector<CItem> &items)
{
  CObjectVector<AString> names;
  CUIntVector order;
  FOR_VECTOR (i, items)
  {
    AString &name = names.AddNew();
    name = items[i].Name;
    name.MakeLower_Ascii();
    order.Add(i);
  }
  order.Sort(CompareIndicesByName, &names);

  CBoolVector keep;
  keep.ClearAndSetSize(items.Size());
  FOR_VECTOR (i, keep)
    keep[i] = true;

  for (unsigned i = 0; i < order.Size();)
  {
    unsigned end = i + 1;
    while (end < order.Size() && names[order[end]] == names[order[i]])
      end++;
    if (end - i > 1)
    {
      items[order[i]] = items[order[end - 1]];
      for (unsigned k = i + 1; k < end; k++)
        keep[order[k]] = false;
    }
    i = end;
  }

  CObjectVector<CItem> result;
  FOR_VECTOR (i, items)
    if (keep[i])
      result.Add(items[i]);
  items = result;
}

} // namespace NXdb
} // namespace NArchive
