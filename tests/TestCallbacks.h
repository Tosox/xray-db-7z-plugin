// In-memory implementations of the 7-Zip callbacks, for driving CHandler in tests.

#ifndef XDB_TESTS_TEST_CALLBACKS_H
#define XDB_TESTS_TEST_CALLBACKS_H

#include <string.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Common/MyCom.h"
#include "Windows/PropVariant.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/Common/StreamObjects.h"

#include "XdbHandler.h"

#include "TestData.h"

namespace NXdbTest {

inline CMyComPtr<IInArchive> CreateHandler()
{
  return CMyComPtr<IInArchive>(new NArchive::NXdb::CHandler);
}

typedef std::vector<std::pair<std::wstring, NWindows::NCOM::CPropVariant> > CProps;

inline HRESULT SetProps(IInArchive *handler, const CProps &props)
{
  CMyComPtr<ISetProperties> setProperties;
  handler->QueryInterface(IID_ISetProperties, (void **)&setProperties);
  std::vector<const wchar_t *> names;
  std::vector<PROPVARIANT> values;
  for (const auto &prop : props)
  {
    names.push_back(prop.first.c_str());
    values.push_back(prop.second);
  }
  return setProperties->SetProperties(names.data(), values.data(), (UInt32)props.size());
}

inline UString GetStringProp(IInArchive *handler, UInt32 index, PROPID propID)
{
  NWindows::NCOM::CPropVariant prop;
  handler->GetProperty(index, propID, &prop);
  return prop.vt == VT_BSTR ? UString(prop.bstrVal) : UString();
}

inline UString GetArcStringProp(IInArchive *handler, PROPID propID)
{
  NWindows::NCOM::CPropVariant prop;
  handler->GetArchiveProperty(propID, &prop);
  return prop.vt == VT_BSTR ? UString(prop.bstrVal) : UString();
}

inline UInt64 GetUInt64Prop(IInArchive *handler, UInt32 index, PROPID propID)
{
  NWindows::NCOM::CPropVariant prop;
  handler->GetProperty(index, propID, &prop);
  return prop.vt == VT_UI8 ? prop.uhVal.QuadPart : (prop.vt == VT_UI4 ? prop.ulVal : (UInt64)(Int64)-1);
}

struct CExtractResults
{
  UInt64 Total = 0;
  UInt64 LastCompleted = 0;
  std::map<UInt32, CBytes> Data;    // extracted bytes by item index
  std::map<UInt32, Int32> Results;  // operation result by item index
};

class CExtractCallback Z7_final:
  public IArchiveExtractCallback,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_1(IArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(IProgress)

  UInt32 _index = 0;
  CDynBufSeqOutStream *_outSpec = nullptr;
  CMyComPtr<ISequentialOutStream> _out;

public:
  CExtractResults Results;
};

inline Z7_COM7F_IMF(CExtractCallback::SetTotal(UInt64 total))
{
  Results.Total = total;
  return S_OK;
}

inline Z7_COM7F_IMF(CExtractCallback::SetCompleted(const UInt64 *completed))
{
  if (completed)
    Results.LastCompleted = *completed;
  return S_OK;
}

inline Z7_COM7F_IMF(CExtractCallback::GetStream(UInt32 index, ISequentialOutStream **outStream, Int32 askMode))
{
  *outStream = nullptr;
  _index = index;
  _out.Release();
  _outSpec = nullptr;
  if (askMode != NArchive::NExtract::NAskMode::kExtract)
    return S_OK;
  _outSpec = new CDynBufSeqOutStream;
  _out = _outSpec;
  *outStream = _out;
  (*outStream)->AddRef();
  return S_OK;
}

inline Z7_COM7F_IMF(CExtractCallback::PrepareOperation(Int32 /* askExtractMode */))
{
  return S_OK;
}

inline Z7_COM7F_IMF(CExtractCallback::SetOperationResult(Int32 opRes))
{
  Results.Results[_index] = opRes;
  if (_outSpec)
    Results.Data[_index] = CBytes(_outSpec->GetBuffer(), _outSpec->GetBuffer() + _outSpec->GetSize());
  return S_OK;
}


// A seekable output stream in memory.
class CMemOutStream Z7_final:
  public IOutStream,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_1(IOutStream)
  Z7_IFACE_COM7_IMP(ISequentialOutStream)

  size_t _pos = 0;

public:
  CBytes Bytes;
};

inline Z7_COM7F_IMF(CMemOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize))
{
  if (_pos + size > Bytes.size())
    Bytes.resize(_pos + size);
  memcpy(Bytes.data() + _pos, data, size);
  _pos += size;
  if (processedSize)
    *processedSize = size;
  return S_OK;
}

inline Z7_COM7F_IMF(CMemOutStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition))
{
  Int64 base = 0;
  if (seekOrigin == STREAM_SEEK_CUR)
    base = (Int64)_pos;
  else if (seekOrigin == STREAM_SEEK_END)
    base = (Int64)Bytes.size();
  if (base + offset < 0)
    return E_INVALIDARG;
  _pos = (size_t)(base + offset);
  if (newPosition)
    *newPosition = _pos;
  return S_OK;
}

inline Z7_COM7F_IMF(CMemOutStream::SetSize(UInt64 newSize))
{
  Bytes.resize((size_t)newSize);
  return S_OK;
}

// What an update callback reports for one item of the new archive.
struct CUpdateEntry
{
  Int32 IndexInArc = -1;
  bool NewData = false;
  bool NewProps = false;
  std::wstring Path;
  bool IsDir = false;
  CBytes Data;
};

inline CUpdateEntry Keep(Int32 indexInArc)
{
  CUpdateEntry e;
  e.IndexInArc = indexInArc;
  return e;
}

inline CUpdateEntry NewFile(const std::wstring &path, const CBytes &data)
{
  CUpdateEntry e;
  e.NewData = e.NewProps = true;
  e.Path = path;
  e.Data = data;
  return e;
}

inline CUpdateEntry NewDir(const std::wstring &path)
{
  CUpdateEntry e;
  e.NewData = e.NewProps = true;
  e.Path = path;
  e.IsDir = true;
  return e;
}

inline CUpdateEntry Rename(Int32 indexInArc, const std::wstring &path)
{
  CUpdateEntry e;
  e.IndexInArc = indexInArc;
  e.NewProps = true;
  e.Path = path;
  return e;
}

inline CUpdateEntry Replace(Int32 indexInArc, const std::wstring &path, const CBytes &data)
{
  CUpdateEntry e = NewFile(path, data);
  e.IndexInArc = indexInArc;
  return e;
}

class CUpdateCallback Z7_final:
  public IArchiveUpdateCallback,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_1(IArchiveUpdateCallback)
  Z7_IFACE_COM7_IMP(IProgress)

public:
  std::vector<CUpdateEntry> Entries;
  unsigned NumResults = 0;
};

inline Z7_COM7F_IMF(CUpdateCallback::SetTotal(UInt64 /* total */))
{
  return S_OK;
}

inline Z7_COM7F_IMF(CUpdateCallback::SetCompleted(const UInt64 * /* completed */))
{
  return S_OK;
}

inline Z7_COM7F_IMF(CUpdateCallback::GetUpdateItemInfo(UInt32 index, Int32 *newData, Int32 *newProps,
    UInt32 *indexInArchive))
{
  const CUpdateEntry &e = Entries[index];
  *newData = e.NewData ? 1 : 0;
  *newProps = e.NewProps ? 1 : 0;
  *indexInArchive = (UInt32)e.IndexInArc;
  return S_OK;
}

inline Z7_COM7F_IMF(CUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  const CUpdateEntry &e = Entries[index];
  NWindows::NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPath:  prop = e.Path.c_str(); break;
    case kpidIsDir: prop = e.IsDir; break;
    case kpidSize:  if (!e.IsDir) prop = (UInt64)e.Data.size(); break;
  }
  prop.Detach(value);
  return S_OK;
}

inline Z7_COM7F_IMF(CUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream))
{
  const CUpdateEntry &e = Entries[index];
  CBufInStream *spec = new CBufInStream;
  CMyComPtr<ISequentialInStream> stream = spec;
  spec->Init(e.Data.data(), e.Data.size());
  *inStream = stream.Detach();
  return S_OK;
}

inline Z7_COM7F_IMF(CUpdateCallback::SetOperationResult(Int32 /* operationResult */))
{
  NumResults++;
  return S_OK;
}

// Runs IOutArchive::UpdateItems on (handler) and returns the new archive.
inline HRESULT RunUpdate(IInArchive *handler, const std::vector<CUpdateEntry> &entries, CBytes &archive,
    const CProps &props = {})
{
  if (!props.empty())
  {
    const HRESULT res = SetProps(handler, props);
    if (res != S_OK)
      return res;
  }
  CMyComPtr<IOutArchive> outArchive;
  handler->QueryInterface(IID_IOutArchive, (void **)&outArchive);
  CUpdateCallback *callbackSpec = new CUpdateCallback;
  CMyComPtr<IArchiveUpdateCallback> callback = callbackSpec;
  callbackSpec->Entries = entries;
  CMemOutStream *outSpec = new CMemOutStream;
  CMyComPtr<ISequentialOutStream> outStream = outSpec;
  const HRESULT res = outArchive->UpdateItems(outStream, (UInt32)entries.size(), callback);
  archive = outSpec->Bytes;
  return res;
}

} // namespace NXdbTest

#endif
