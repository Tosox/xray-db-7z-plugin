// XdbOut.cpp

#include "StdAfx.h"

#include "CpuArch.h"

#include "7zip/Common/LimitedStreams.h"
#include "7zip/Common/StreamUtils.h"

#include "XdbFormat.h"
#include "XdbOut.h"

namespace NArchive {
namespace NXdb {

HRESULT COutArchive::Create(IOutStream *stream)
{
  _stream = stream;
  _pos = 0;
  _dataChunkPos = 0;
  return _stream->Seek(0, STREAM_SEEK_CUR, &_base);
}

HRESULT COutArchive::WriteChunkHeader(UInt32 id, UInt32 size)
{
  Byte buf[kChunkHeaderSize];
  SetUi32(buf, id)
  SetUi32(buf + 4, size)
  return WriteData(buf, sizeof(buf));
}

HRESULT COutArchive::WriteChunk(UInt32 id, const Byte *data, size_t size)
{
  if (size > GetRemainingSize() || GetRemainingSize() - size < kChunkHeaderSize)
    return k_hres_TooLarge;
  RINOK(WriteChunkHeader(id, (UInt32)size))
  return WriteData(data, size);
}

HRESULT COutArchive::BeginDataChunk()
{
  _dataChunkPos = _pos;
  return WriteChunkHeader(NChunkId::kData, 0); // the size is patched in EndDataChunk()
}

HRESULT COutArchive::WriteData(const void *data, size_t size)
{
  if (size > GetRemainingSize())
    return k_hres_TooLarge;
  RINOK(WriteStream(_stream, data, size))
  _pos += size;
  return S_OK;
}

HRESULT COutArchive::CopyData(ISequentialInStream *inStream, UInt64 size, ICompressCoder *copyCoder,
    ICompressProgressInfo *progress, UInt64 &copied)
{
  copied = 0;
  if (size > GetRemainingSize())
    return k_hres_TooLarge;
  CMyComPtr2_Create<ISequentialInStream, CLimitedSequentialInStream> limitedStream;
  limitedStream->SetStream(inStream);
  limitedStream->Init(size);
  const HRESULT res = copyCoder->Code(limitedStream, _stream, NULL, NULL, progress);
  copied = size - limitedStream->GetRem();
  _pos += copied;
  RINOK(res)
  return copied == size ? S_OK : S_FALSE;
}

HRESULT COutArchive::EndDataChunk()
{
  const UInt64 dataSize = _pos - _dataChunkPos - kChunkHeaderSize;
  Byte buf[4];
  SetUi32(buf, (UInt32)dataSize)
  RINOK(_stream->Seek((Int64)(_base + _dataChunkPos + 4), STREAM_SEEK_SET, NULL))
  RINOK(WriteStream(_stream, buf, sizeof(buf)))
  return _stream->Seek((Int64)(_base + _pos), STREAM_SEEK_SET, NULL);
}

} // namespace NXdb
} // namespace NArchive
