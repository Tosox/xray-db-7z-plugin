// XdbUpdate.h

#ifndef ZIP7_INC_XDB_UPDATE_H
#define ZIP7_INC_XDB_UPDATE_H

#include "Common/MyBuffer.h"
#include "Common/MyVector.h"

#include "7zip/Archive/IArchive.h"

#include "XdbFormat.h"

namespace NArchive {
namespace NXdb {

struct CUpdateItem
{
  int IndexInArc;         // item of the archive being updated, or -1
  UInt32 IndexInClient;   // index for IArchiveUpdateCallback
  bool NewData;           // read the contents from the callback, not the archive
  bool IsDir;
  AString Name;           // stored form, see PathToItemName()
  UInt64 Size;            // of new data, if known (for progress and buffering)

  CUpdateItem():
      IndexInArc(-1),
      IndexInClient(0),
      NewData(false),
      IsDir(false),
      Size(0)
      {}
};

struct CUpdateOptions
{
  EVersion Version;
  bool Compress;
  bool KeepDuplicates;
  bool WriteUserData;   // xdb only
  CByteBuffer UserData;

  CUpdateOptions():
      Version(kVersion_Xdb),
      Compress(true),
      KeepDuplicates(false),
      WriteUserData(false)
      {}
};

/*
  Reads a whole stream into (buf); its first (size) bytes are the contents.
  (sizeHint) is the expected size, if known. Fails above 4 GiB.
*/
HRESULT ReadStreamToBuffer(ISequentialInStream *stream, UInt64 sizeHint, CByteBuffer &buf, size_t &size);

/*
  Writes a new archive from (updateItems): copies unchanged files from
  (inStream) when their stored form fits the target version, re-encodes them
  otherwise, and compresses new files one at a time.
  Returns E_NOTIMPL if (outStream) can't seek.
*/
HRESULT UpdateArchive(
    IInStream *inStream,
    const CObjectVector<CItem> &inputItems,
    const CObjectVector<CUpdateItem> &updateItems,
    const CUpdateOptions &options,
    ISequentialOutStream *outStream,
    IArchiveUpdateCallback *callback);

} // namespace NXdb
} // namespace NArchive

#endif
