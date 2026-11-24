// In-memory implementations of the 7-Zip callbacks, for driving CHandler in tests.

#ifndef XDB_TESTS_TEST_CALLBACKS_H
#define XDB_TESTS_TEST_CALLBACKS_H

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

} // namespace NXdbTest

#endif
