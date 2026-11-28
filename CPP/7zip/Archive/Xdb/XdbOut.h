// XdbOut.h

#ifndef ZIP7_INC_XDB_OUT_H
#define ZIP7_INC_XDB_OUT_H

#include "Common/MyCom.h"

#include "7zip/IStream.h"
#include "7zip/ICoder.h"

namespace NArchive {
namespace NXdb {

/*
  Writes the chunks of an archive. The size of the data chunk is only known
  once all files are written, so the stream must be seekable to patch it.
  Positions are relative to where the archive starts in the stream, which is
  what file offsets in the header refer to.
*/
class COutArchive
{
  CMyComPtr<IOutStream> _stream;
  UInt64 _base;
  UInt64 _pos;
  UInt64 _dataChunkPos;

  HRESULT WriteChunkHeader(UInt32 id, UInt32 size);

public:
  HRESULT Create(IOutStream *stream);

  UInt64 GetPos() const { return _pos; }

  // How many more bytes the archive can take: offsets and sizes are 32-bit.
  UInt64 GetRemainingSize() const { return (UInt64)0xFFFFFFFF - _pos; }

  HRESULT WriteChunk(UInt32 id, const Byte *data, size_t size);

  HRESULT BeginDataChunk();
  HRESULT WriteData(const void *data, size_t size);
  // Copies (size) bytes; returns S_FALSE if the input ends early.
  HRESULT CopyData(ISequentialInStream *inStream, UInt64 size, ICompressCoder *copyCoder,
      ICompressProgressInfo *progress, UInt64 &copied);
  HRESULT EndDataChunk();
};

} // namespace NXdb
} // namespace NArchive

#endif
