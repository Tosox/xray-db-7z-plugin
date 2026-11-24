// XdbIn.h

#ifndef ZIP7_INC_XDB_IN_H
#define ZIP7_INC_XDB_IN_H

#include "Common/MyBuffer.h"
#include "Common/MyVector.h"

#include "7zip/Archive/IArchive.h"

#include "XdbFormat.h"

namespace NArchive {
namespace NXdb {

// Reads the chunk structure and file table of an archive.
class CInArchive
{
  HRESULT ReadChunks(IInStream *stream, UInt64 fileSize);
  bool DecodeHeader(const CByteBuffer &payload, bool isCompressed, const EVersion *forcedVersion);
  HRESULT ReadLzhufSizes(IInStream *stream, IArchiveOpenCallback *callback);
  HRESULT ReadUserData(IInStream *stream);

  struct CChunk
  {
    UInt64 Pos;  // of the payload
    UInt32 Size;
    UInt32 Id;   // including NChunkId::kCompressedFlag
    bool Found;

    CChunk(): Pos(0), Size(0), Id(0), Found(false) {}
  };

  CChunk _data;
  CChunk _header;
  CChunk _userData;

public:
  CObjectVector<CItem> Items;
  EVersion Version;
  CByteBuffer UserData;
  bool UserDataDefined;
  UInt64 PhySize;

  // The chunks are intact, but no version could decode the file table.
  bool HeadersError;

  CInArchive() { Clear(); }
  void Clear();

  /*
    Returns S_FALSE if the stream is not a DB archive. A damaged file table
    still opens (S_OK) with HeadersError set and no items.
    With (forcedVersion), only that version is tried.
  */
  HRESULT Open(IInStream *stream, const EVersion *forcedVersion, IArchiveOpenCallback *callback);
};

// Of several items with the same name (ASCII case-insensitively), keeps the
// last one, at the position of the first.
void RemoveDuplicateItems(CObjectVector<CItem> &items);

} // namespace NXdb
} // namespace NArchive

#endif
