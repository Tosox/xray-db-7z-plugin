// XdbHandler.cpp
// Reading side of the handler: listing, properties and extraction.

#include "StdAfx.h"

#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"

#include "7zip/Archive/Common/OutStreamWithCRC.h"
#include "7zip/Common/LimitedStreams.h"
#include "7zip/Common/ProgressUtils.h"
#include "7zip/Common/StreamUtils.h"

#include "7zCrc.h"

#include "XdbCodec.h"
#include "XdbHandler.h"

namespace NArchive {
namespace NXdb {

const char * const kUserDataItemName = "[userdata].ltx";

enum
{
  kpidUserData = kpidUserDefined,
  kpidDbFormat
};

static const Byte kProps[] =
{
  kpidPath,
  kpidIsDir,
  kpidSize,
  kpidPackSize,
  kpidCRC,
  kpidMethod
};

static const CStatProp kArcProps[] =
{
  { "Userdata", kpidUserData, VT_BSTR },
  { "DB-Format", kpidDbFormat, VT_BSTR }
};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps_WITH_NAME

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  NWindows::NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPhySize:
      if (_isOpen)
        prop = _arc.PhySize;
      break;
    case kpidErrorFlags:
      if (_isOpen && _arc.HeadersError)
        prop = (UInt32)kpv_ErrorFlags_HeadersError;
      break;
    case kpidDbFormat:
      if (_isOpen && !_arc.HeadersError)
        prop = GetVersionInfo(_arc.Version).Name;
      break;
    case kpidUserData:
      if (_isOpen && _arc.UserDataDefined)
      {
        AString text;
        text.SetFrom_Chars_SizeT((const char *)(const Byte *)_arc.UserData, _arc.UserData.Size());
        prop = MultiByteToUnicodeString(text, kNameCodePage);
      }
      break;
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = _arc.Items.Size() + (_showUserDataItem ? 1 : 0);
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  NWindows::NCOM::CPropVariant prop;

  if (IsUserDataItem(index))
  {
    const CByteBuffer &userData = _arc.UserData;
    switch (propID)
    {
      case kpidPath:     prop = kUserDataItemName; break;
      case kpidIsDir:    prop = false; break;
      case kpidSize:
      case kpidPackSize: prop = (UInt64)userData.Size(); break;
      case kpidCRC:      prop = CrcCalc(userData, userData.Size()); break;
      case kpidMethod:   prop = GetMethodName(kMethod_Store); break;
    }
    prop.Detach(value);
    return S_OK;
  }

  const CItem &item = _arc.Items[index];
  switch (propID)
  {
    case kpidPath:
      prop = item.GetPath();
      break;
    case kpidIsDir:
      prop = item.IsDir;
      break;
    case kpidSize:
      if (!item.IsDir)
        prop = (UInt64)item.Size;
      break;
    case kpidPackSize:
      if (!item.IsDir)
        prop = (UInt64)item.PackSize;
      break;
    case kpidCRC:
      if (item.CrcDefined)
        prop = item.Crc;
      break;
    case kpidMethod:
      if (!item.IsDir)
        prop = GetMethodName(item.Method);
      break;
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}

static bool HasItemNamed(const CObjectVector<CItem> &items, const char *name)
{
  FOR_VECTOR (i, items)
    if (items[i].Name.IsEqualTo_Ascii_NoCase(name))
      return true;
  return false;
}

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 * /* maxCheckStartPosition */,
    IArchiveOpenCallback *callback))
{
  COM_TRY_BEGIN
  Close();
  RINOK(_arc.Open(stream, callback))
  if (!_options.KeepDuplicates)
    RemoveDuplicateNames(_arc.Items);
  _showUserDataItem = _arc.Version == kVersion_Xdb
      && _arc.UserDataDefined
      && !HasItemNamed(_arc.Items, kUserDataItemName);
  _stream = stream;
  _isOpen = true;
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Close())
{
  _arc.Clear();
  _stream.Release();
  _isOpen = false;
  _showUserDataItem = false;
  return S_OK;
}

HRESULT CHandler::ExtractItem(const CItem &item, ISequentialOutStream *outStream,
    ICompressProgressInfo *progress, Int32 &opRes)
{
  opRes = NExtract::NOperationResult::kOK;
  RINOK(InStream_SeekSet(_stream, item.Offset))

  if (item.Method == kMethod_Store)
  {
    CMyComPtr2_Create<ISequentialInStream, CLimitedSequentialInStream> inStream;
    inStream->SetStream(_stream);
    inStream->Init(item.PackSize);
    RINOK(_copyCoder.Interface()->Code(inStream, outStream, NULL, NULL, progress))
    if (_copyCoder->TotalSize != item.PackSize)
      opRes = NExtract::NOperationResult::kUnexpectedEnd;
    return S_OK;
  }

  // Compressed files are decoded in memory, one at a time.
  CByteBuffer packed(item.PackSize);
  const HRESULT res = ReadStream_FALSE(_stream, packed, packed.Size());
  if (res == S_FALSE)
  {
    opRes = NExtract::NOperationResult::kUnexpectedEnd;
    return S_OK;
  }
  RINOK(res)

  CByteBuffer data;
  if (!UnpackItemData(item.Method, packed, packed.Size(), item.Size, data))
  {
    opRes = NExtract::NOperationResult::kDataError;
    return S_OK;
  }
  RINOK(WriteStream(outStream, data, data.Size()))
  if (progress)
  {
    const UInt64 inSize = item.PackSize;
    const UInt64 outSize = item.Size;
    RINOK(progress->SetRatioInfo(&inSize, &outSize))
  }
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems, Int32 testMode,
    IArchiveExtractCallback *extractCallback))
{
  COM_TRY_BEGIN
  const bool allFilesMode = (numItems == (UInt32)(Int32)-1);
  if (allFilesMode)
    GetNumberOfItems(&numItems);
  if (numItems == 0)
    return S_OK;

  UInt64 totalSize = 0;
  for (UInt32 i = 0; i < numItems; i++)
  {
    const UInt32 index = allFilesMode ? i : indices[i];
    totalSize += IsUserDataItem(index) ? _arc.UserData.Size() : _arc.Items[index].Size;
  }
  RINOK(extractCallback->SetTotal(totalSize))

  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
  lps->Init(extractCallback, false);
  CMyComPtr2_Create<ISequentialOutStream, COutStreamWithCRC> outStream;

  UInt64 totalPackSize = 0;
  totalSize = 0;

  for (UInt32 i = 0;; i++)
  {
    lps->InSize = totalPackSize;
    lps->OutSize = totalSize;
    RINOK(lps->SetCur())
    if (i >= numItems)
      break;

    const UInt32 index = allFilesMode ? i : indices[i];
    const Int32 askMode = testMode ? NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
    CMyComPtr<ISequentialOutStream> realOutStream;
    RINOK(extractCallback->GetStream(index, &realOutStream, askMode))

    if (IsUserDataItem(index))
    {
      const CByteBuffer &userData = _arc.UserData;
      RINOK(extractCallback->PrepareOperation(askMode))
      if (realOutStream)
      {
        RINOK(WriteStream(realOutStream, userData, userData.Size()))
      }
      realOutStream.Release();
      RINOK(extractCallback->SetOperationResult(NExtract::NOperationResult::kOK))
      totalPackSize += userData.Size();
      totalSize += userData.Size();
      continue;
    }

    const CItem &item = _arc.Items[index];
    if (item.IsDir)
    {
      RINOK(extractCallback->PrepareOperation(askMode))
      RINOK(extractCallback->SetOperationResult(NExtract::NOperationResult::kOK))
      continue;
    }
    if (!testMode && !realOutStream)
      continue;
    RINOK(extractCallback->PrepareOperation(askMode))

    outStream->SetStream(realOutStream);
    outStream->Init();
    realOutStream.Release();

    Int32 opRes;
    RINOK(ExtractItem(item, outStream, lps, opRes))
    if (opRes == NExtract::NOperationResult::kOK)
    {
      if (outStream->GetSize() != item.Size)
        opRes = NExtract::NOperationResult::kDataError;
      else if (item.CrcDefined && outStream->GetCRC() != item.Crc)
        opRes = NExtract::NOperationResult::kCRCError;
    }
    outStream->ReleaseStream();
    RINOK(extractCallback->SetOperationResult(opRes))

    totalPackSize += item.PackSize;
    totalSize += item.Size;
  }
  return S_OK;
  COM_TRY_END
}

} // namespace NXdb
} // namespace NArchive
