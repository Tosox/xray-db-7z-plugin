// XdbUpdate.cpp

#include "StdAfx.h"

#include "7zCrc.h"

#include "7zip/Archive/Common/InStreamWithCRC.h"
#include "7zip/Common/ProgressUtils.h"
#include "7zip/Common/StreamUtils.h"
#include "7zip/Compress/CopyCoder.h"

#include "XdbCodec.h"
#include "XdbHeader.h"
#include "XdbLzhuf.h"
#include "XdbOut.h"
#include "XdbScrambler.h"
#include "XdbUpdate.h"

namespace NArchive {
namespace NXdb {

// Compressing a file needs all of it in memory; larger files are stored.
static const UInt64 kMaxCompressSize = (UInt64)1 << 28;

static const UInt64 kMaxFileSize = (UInt64)0xFFFFFFFF;

HRESULT ReadStreamToBuffer(ISequentialInStream *stream, UInt64 sizeHint, CByteBuffer &buf, size_t &size)
{
  // One byte more than expected, so the end is found without growing the buffer.
  const size_t kMinCapacity = 1 << 16;
  buf.Alloc(sizeHint < kMinCapacity ? kMinCapacity : (size_t)sizeHint + 1);
  size = 0;
  for (;;)
  {
    if (size == buf.Size())
    {
      if (size >= kMaxFileSize)
        return k_hres_TooLarge;
      buf.ChangeSize_KeepData(size * 2, size);
    }
    const size_t chunk = MyMin(buf.Size() - size, (size_t)1 << 30);
    UInt32 processed;
    RINOK(stream->Read((Byte *)buf + size, (UInt32)chunk, &processed))
    if (processed == 0)
      return size <= kMaxFileSize ? S_OK : k_hres_TooLarge;
    size += processed;
  }
}

namespace {

// A lowercase directory name, kept in a sorted CObjectVector.
struct CDirName
{
  AString Name;
  int Compare(const CDirName &other) const { return strcmp(Name, other.Name); }
};

/*
  Inserts a directory record before the first item under each folder that has
  none, as the original packer writes one for every folder. The engine does not
  need them; they make the archive the same however its files were added
  (a file added by its path alone, or an archive converted from 11xx).
*/
void AddMissingDirs(CObjectVector<CItem> &items)
{
  CObjectVector<CDirName> known;
  FOR_VECTOR (i, items)
  {
    if (!items[i].IsDir)
      continue;
    CDirName dir;
    dir.Name = items[i].Name;
    dir.Name.MakeLower_Ascii();
    known.AddToUniqueSorted(dir);
  }

  CObjectVector<CItem> result;
  FOR_VECTOR (i, items)
  {
    const CItem &item = items[i];
    for (int sep = item.Name.Find('\\'); sep > 0; sep = item.Name.Find('\\', (unsigned)sep + 1))
    {
      CDirName dir;
      dir.Name.SetFrom(item.Name, (unsigned)sep);
      dir.Name.MakeLower_Ascii();
      const unsigned numKnown = known.Size();
      known.AddToUniqueSorted(dir);
      if (known.Size() == numKnown)
        continue;
      CItem &newDir = result.AddNew();
      newDir.Name.SetFrom(item.Name, (unsigned)sep);
      newDir.IsDir = true;
    }
    result.Add(item);
  }
  items = result;
}

// A name in CheckNames(): of an item, or of a folder above one.
struct CNameUse
{
  AString Name;
  unsigned Index; // of the item
  bool IsFile;
  bool IsParent;

  int Compare(const CNameUse &other) const
  {
    const int res = strcmp(Name, other.Name);
    return res != 0 ? res : MyCompare(Index, other.Index);
  }
};

/*
  A file can't share its name with another file or with a folder: the engine
  would find only one of them. 7-Zip lets a rename go onto a used name, so this
  refuses the update rather than drop a file. A folder listed twice (one renamed
  onto another) is merged into the first.
*/
HRESULT CheckNames(CObjectVector<CUpdateItem> &items)
{
  CObjectVector<CNameUse> uses;
  FOR_VECTOR (i, items)
  {
    AString name = items[i].Name;
    name.MakeLower_Ascii();
    for (int sep = name.Find('\\'); sep > 0; sep = name.Find('\\', (unsigned)sep + 1))
    {
      CNameUse &parent = uses.AddNew();
      parent.Name.SetFrom(name, (unsigned)sep);
      parent.Index = i;
      parent.IsFile = false;
      parent.IsParent = true;
    }
    CNameUse &use = uses.AddNew();
    use.Name = name;
    use.Index = i;
    use.IsFile = !items[i].IsDir;
    use.IsParent = false;
  }
  uses.Sort();

  CBoolVector drop;
  drop.ClearAndSetSize(items.Size());
  FOR_VECTOR (i, drop)
    drop[i] = false;
  for (unsigned start = 0; start < uses.Size();)
  {
    unsigned end = start + 1;
    const bool firstIsFile = uses[start].IsFile;
    bool hasDir = !uses[start].IsFile && !uses[start].IsParent;
    for (; end < uses.Size() && uses[end].Name == uses[start].Name; end++)
    {
      const CNameUse &use = uses[end];
      if (use.IsFile || firstIsFile)
        return k_hres_NameInUse;
      if (!use.IsParent)
      {
        if (hasDir)
          drop[use.Index] = true;
        hasDir = true;
      }
    }
    start = end;
  }

  CObjectVector<CUpdateItem> result;
  FOR_VECTOR (i, items)
    if (!drop[i])
      result.Add(items[i]);
  items = result;
  return S_OK;
}

class CUpdater
{
  IInStream *_inStream;
  const CObjectVector<CItem> &_inputItems;
  const CUpdateOptions &_options;
  const CVersionInfo &_info;
  IArchiveUpdateCallback *_callback;

  CMyComPtr<IArchiveUpdateCallbackFile> _opCallback;
  COutArchive _out;
  CItemPacker _packer;
  CMyComPtr2_Create<ICompressCoder, NCompress::CCopyCoder> _copyCoder;
  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> _progress;

  HRESULT WriteFileData(const Byte *data, size_t size, CItem &item);
  HRESULT CopyStoredData(ISequentialInStream *stream, UInt64 maxSize, CItem &item);
  HRESULT CopyItem(UInt32 indexInArc, CItem &item);
  HRESULT AddItem(const CUpdateItem &ui, CItem &item, bool &skipped);
  HRESULT WriteHeader(const CObjectVector<CItem> &items);

public:
  CUpdater(IInStream *inStream, const CObjectVector<CItem> &inputItems, const CUpdateOptions &options,
      IArchiveUpdateCallback *callback):
      _inStream(inStream),
      _inputItems(inputItems),
      _options(options),
      _info(GetVersionInfo(options.Version)),
      _callback(callback)
      {}

  HRESULT Update(const CObjectVector<CUpdateItem> &updateItems, IOutStream *outStream);
};

} // namespace

// Writes a file that is in memory, compressed if that helps and is wanted.
HRESULT CUpdater::WriteFileData(const Byte *data, size_t size, CItem &item)
{
  if (size > kMaxFileSize)
    return k_hres_TooLarge;
  item.Offset = (UInt32)_out.GetPos();
  item.Size = (UInt32)size;
  item.Crc = CrcCalc(data, size);
  item.CrcDefined = _info.HasCrc;

  CByteBuffer packed;
  if (_options.Compress && size <= kMaxCompressSize && _packer.Pack(_info.FileMethod, data, size, packed))
  {
    item.Method = _info.FileMethod;
    item.PackSize = (UInt32)packed.Size();
    return _out.WriteData(packed, packed.Size());
  }
  item.Method = kMethod_Store;
  item.PackSize = item.Size;
  return _out.WriteData(data, size);
}

// Copies a stored file from (stream), computing its CRC on the way.
HRESULT CUpdater::CopyStoredData(ISequentialInStream *stream, UInt64 maxSize, CItem &item)
{
  CMyComPtr2_Create<ISequentialInStream, CSequentialInStreamWithCRC> crcStream;
  crcStream->SetStream(stream);
  crcStream->Init();
  item.Offset = (UInt32)_out.GetPos();
  UInt64 copied;
  const HRESULT res = _out.CopyData(crcStream, maxSize, _copyCoder.Interface(), _progress, copied);
  if (res != S_OK && res != S_FALSE)
    return res;
  if (copied > kMaxFileSize)
    return k_hres_TooLarge;
  item.Size = item.PackSize = (UInt32)copied;
  item.Crc = crcStream->GetCRC();
  item.CrcDefined = _info.HasCrc;
  item.Method = kMethod_Store;
  return res;
}

HRESULT CUpdater::CopyItem(UInt32 indexInArc, CItem &item)
{
  const CItem &src = _inputItems[indexInArc];
  RINOK(InStream_SeekSet(_inStream, src.Offset))

  if (src.Method == kMethod_Store)
  {
    const HRESULT res = CopyStoredData(_inStream, src.PackSize, item);
    if (res != S_OK)
      return res == S_FALSE ? E_FAIL : res; // S_FALSE: the source archive is truncated
    // Keep the stored CRC rather than the one just computed, as for compressed
    // files: if the data was damaged, the damage stays detectable.
    if (src.CrcDefined)
      item.Crc = src.Crc;
    return S_OK;
  }

  // A compressed file can be copied as it is if the target version uses the
  // same codec and the CRC is known or not needed.
  if (src.Method == _info.FileMethod && (src.CrcDefined || !_info.HasCrc))
  {
    item.Offset = (UInt32)_out.GetPos();
    UInt64 copied;
    const HRESULT res = _out.CopyData(_inStream, src.PackSize, _copyCoder.Interface(), _progress, copied);
    if (res != S_OK)
      return res == S_FALSE ? E_FAIL : res;
    item.Size = src.Size;
    item.PackSize = src.PackSize;
    item.Crc = src.Crc;
    item.CrcDefined = _info.HasCrc;
    item.Method = src.Method;
    return S_OK;
  }

  // Otherwise it has to be decoded and written again.
  CByteBuffer packed(src.PackSize);
  RINOK(ReadStream_FAIL(_inStream, packed, packed.Size()))
  CByteBuffer data;
  if (!UnpackItemData(src.Method, packed, packed.Size(), src.Size, data))
    return E_FAIL;
  return WriteFileData(data, data.Size(), item);
}

HRESULT CUpdater::AddItem(const CUpdateItem &ui, CItem &item, bool &skipped)
{
  skipped = false;
  CMyComPtr<ISequentialInStream> fileStream;
  const HRESULT res = _callback->GetStream(ui.IndexInClient, &fileStream);
  if (res == S_FALSE)
  {
    // The file couldn't be opened; the callback has already reported it.
    skipped = true;
    return S_OK;
  }
  RINOK(res)

  if (!fileStream)
    RINOK(WriteFileData(NULL, 0, item))
  else if (!_options.Compress || ui.Size > kMaxCompressSize)
  {
    // Streamed without buffering. S_FALSE means the file ended, which is
    // expected; S_OK means it would not fit into the archive.
    const HRESULT copyRes = CopyStoredData(fileStream, _out.GetRemainingSize(), item);
    if (copyRes == S_OK)
      return k_hres_TooLarge;
    if (copyRes != S_FALSE)
      return copyRes;
  }
  else
  {
    CByteBuffer data;
    size_t size;
    RINOK(ReadStreamToBuffer(fileStream, ui.Size, data, size))
    RINOK(WriteFileData(data, size, item))
  }
  return _callback->SetOperationResult(NUpdate::NOperationResult::kOK);
}

HRESULT CUpdater::WriteHeader(const CObjectVector<CItem> &items)
{
  CByteBuffer table;
  if (!NXdb::WriteHeader(_options.Version, items, table))
    return E_INVALIDARG;
  CByteBuffer header;
  NLzhuf::Encode(table, table.Size(), header);
  if (_info.IsScrambled)
    CScrambler(_info.ScramblerKey).Encrypt(header, header.Size());
  return _out.WriteChunk(NChunkId::kHeader | NChunkId::kCompressedFlag, header, header.Size());
}

HRESULT CUpdater::Update(const CObjectVector<CUpdateItem> &updateItems, IOutStream *outStream)
{
  Z7_DECL_CMyComPtr_QI_FROM(IArchiveUpdateCallbackFile, opCallback, _callback)
  _opCallback = opCallback;

  UInt64 totalSize = 0;
  FOR_VECTOR (i, updateItems)
  {
    const CUpdateItem &ui = updateItems[i];
    if (ui.NewData)
      totalSize += ui.Size;
    else if (ui.DataInBuffer)
      totalSize += ui.Data.Size();
    else if (ui.IndexInArc >= 0)
      totalSize += _inputItems[(unsigned)ui.IndexInArc].PackSize;
  }
  RINOK(_callback->SetTotal(totalSize))
  _progress->Init(_callback, true);

  RINOK(_out.Create(outStream))
  if (_options.WriteUserData)
    RINOK(_out.WriteChunk(NChunkId::kUserData, _options.UserData, _options.UserData.Size()))
  RINOK(_out.BeginDataChunk())

  CObjectVector<CItem> items;
  UInt64 completed = 0;
  FOR_VECTOR (i, updateItems)
  {
    _progress->InSize = _progress->OutSize = completed;
    RINOK(_progress->SetCur())

    const CUpdateItem &ui = updateItems[i];
    CItem item;
    item.Name = ui.Name;
    item.IsDir = ui.IsDir;

    if (!ui.IsDir)
    {
      if (ui.NewData)
      {
        bool skipped;
        RINOK(AddItem(ui, item, skipped))
        if (skipped)
          continue;
        completed += ui.Size;
      }
      else if (ui.DataInBuffer)
      {
        RINOK(WriteFileData(ui.Data, ui.Data.Size(), item))
        completed += ui.Data.Size();
      }
      else
      {
        if (_opCallback)
          RINOK(_opCallback->ReportOperation(NEventIndexType::kInArcIndex, (UInt32)ui.IndexInArc,
              NUpdateNotifyOp::kReplicate))
        RINOK(CopyItem((UInt32)ui.IndexInArc, item))
        completed += _inputItems[(unsigned)ui.IndexInArc].PackSize;
      }
    }
    items.Add(item);
  }

  RINOK(_out.EndDataChunk())
  AddMissingDirs(items);
  RINOK(WriteHeader(items))
  _progress->InSize = _progress->OutSize = completed;
  return _progress->SetCur();
}

HRESULT UpdateArchive(
    IInStream *inStream,
    const CObjectVector<CItem> &inputItems,
    const CObjectVector<CUpdateItem> &updateItems,
    const CUpdateOptions &options,
    ISequentialOutStream *outStream,
    IArchiveUpdateCallback *callback)
{
  // The data chunk's size is written once all files are in.
  Z7_DECL_CMyComPtr_QI_FROM(IOutStream, seekableStream, outStream)
  if (!seekableStream)
    return E_NOTIMPL;

  CObjectVector<CUpdateItem> items = updateItems;
  if (!options.KeepDuplicates)
    RINOK(CheckNames(items))

  CUpdater updater(inStream, inputItems, options, callback);
  return updater.Update(items, seekableStream);
}

} // namespace NXdb
} // namespace NArchive
