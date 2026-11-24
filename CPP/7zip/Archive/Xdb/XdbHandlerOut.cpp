// XdbHandlerOut.cpp
// Writing side of the handler: properties and updating.

#include "StdAfx.h"

#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"

#include "7zip/Common/MethodProps.h"

#include "XdbHandler.h"

namespace NArchive {
namespace NXdb {

// A userdata file larger than this is certainly a mistake.
static const UInt64 kMaxUserDataFileSize = (UInt64)1 << 24;

void COptions::Init()
{
  VersionDefined = false;
  Version = kVersion_Xdb;
  CompressMode = kCompressMode_Auto;
  KeepDuplicates = false;
  GenerateUserData = true;
  UserDataDefined = false;
  UserData.Free();
}

// Accepts 7-Zip's on/off/+/- and, for command lines written for v0.1.0, true/false.
static HRESULT ParseBool(const PROPVARIANT &value, bool &result)
{
  if (value.vt == VT_BSTR)
  {
    const UString s = value.bstrVal;
    if (s.IsEqualTo_Ascii_NoCase("true"))
    {
      result = true;
      return S_OK;
    }
    if (s.IsEqualTo_Ascii_NoCase("false"))
    {
      result = false;
      return S_OK;
    }
  }
  return PROPVARIANT_to_bool(value, result);
}

static HRESULT ParseVersion(const PROPVARIANT &value, COptions &options)
{
  UString name;
  if (value.vt == VT_BSTR)
    name = value.bstrVal;
  else if (value.vt == VT_UI4)
    name.Add_UInt32(value.ulVal); // 7-Zip turns values like "2215" into numbers
  else
    return E_INVALIDARG;

  if (name.IsEqualTo_Ascii_NoCase("auto"))
  {
    options.VersionDefined = false;
    return S_OK;
  }
  if (!FindVersion(name, options.Version))
    return E_INVALIDARG;
  options.VersionDefined = true;
  return S_OK;
}

static HRESULT ParseCompressMode(const PROPVARIANT &value, ECompressMode &mode)
{
  if (value.vt == VT_BSTR && UString(value.bstrVal).IsEqualTo_Ascii_NoCase("auto"))
  {
    mode = kCompressMode_Auto;
    return S_OK;
  }
  bool compress;
  RINOK(ParseBool(value, compress))
  mode = compress ? kCompressMode_Always : kCompressMode_Never;
  return S_OK;
}

static void AppendPropValue(UString &s, const PROPVARIANT &value)
{
  if (value.vt == VT_BSTR)
    s += value.bstrVal;
  else if (value.vt == VT_UI4)
    s.Add_UInt32(value.ulVal);
}

/*
  Stock 7-Zip splits the "Parameters" text of the Add dialog at every space,
  even inside quotes, so  xdb_ud="C:\My Mods\ud.ltx"  arrives as the two
  properties  xdb_ud="C:\My  and  Mods\ud.ltx" . This puts such a value back
  together and removes the quotes. Returns the index of the last property used.
*/
static UInt32 ReadQuotedValue(const wchar_t * const *names, const PROPVARIANT *values, UInt32 numProps,
    UInt32 index, UString &result)
{
  result.Empty();
  AppendPropValue(result, values[index]);
  if (result.IsEmpty() || (result[0] != L'"' && result[0] != L'\''))
    return index;

  const wchar_t quote = result[0];
  while ((result.Len() < 2 || result.Back() != quote) && index + 1 < numProps)
  {
    index++;
    result.Add_Space();
    result += names[index];
    if (values[index].vt != VT_EMPTY)
    {
      result += L'=';
      AppendPropValue(result, values[index]);
    }
  }
  if (result.Len() >= 2 && result.Back() == quote)
  {
    result.DeleteBack();
    result.Delete(0);
  }
  return index;
}

static HRESULT ReadUserDataFile(const UString &path, CByteBuffer &data)
{
  NWindows::NFile::NIO::CInFile file;
  UInt64 size;
  if (!file.Open(us2fs(path)) || !file.GetLength(size))
    return E_INVALIDARG;
  if (size > kMaxUserDataFileSize)
    return E_INVALIDARG;
  data.Alloc((size_t)size);
  size_t processed;
  if (!file.ReadFull(data, data.Size(), processed) || processed != data.Size())
    return E_FAIL;
  return S_OK;
}

// The compression level: "x" with a value, or "x9" style.
static bool IsLevelProperty(const UString &name)
{
  if (name[0] != L'x')
    return false;
  for (unsigned i = 1; i < name.Len(); i++)
    if (name[i] < L'0' || name[i] > L'9')
      return false;
  return true;
}

// Properties that the 7-Zip GUI and command line send for every format.
static bool IsIgnoredProperty(const UString &name)
{
  return IsLevelProperty(name)
      || name.IsPrefixedBy_Ascii_NoCase("mt")
      || name.IsPrefixedBy_Ascii_NoCase("memuse")
      || name.IsPrefixedBy_Ascii_NoCase("tp")
      || name == L"m" || name == L"tm" || name == L"tc" || name == L"ta";
}

Z7_COM7F_IMF(CHandler::SetProperties(const wchar_t * const *names, const PROPVARIANT *values, UInt32 numProps))
{
  COM_TRY_BEGIN
  _options.Init();

  for (UInt32 i = 0; i < numProps; i++)
  {
    UString name = names[i];
    name.MakeLower_Ascii();
    if (name.IsEmpty())
      return E_INVALIDARG;
    const PROPVARIANT &value = values[i];

    if (name == L"dbver")
    {
      RINOK(ParseVersion(value, _options))
    }
    else if (name == L"compress")
    {
      RINOK(ParseCompressMode(value, _options.CompressMode))
    }
    else if (name == L"keepdups")
    {
      RINOK(ParseBool(value, _options.KeepDuplicates))
    }
    else if (name == L"gen_xdb_ud")
    {
      RINOK(ParseBool(value, _options.GenerateUserData))
    }
    else if (name == L"xdb_ud")
    {
      UString path;
      i = ReadQuotedValue(names, values, numProps, i, path);
      RINOK(ReadUserDataFile(path, _options.UserData))
      _options.UserDataDefined = true;
    }
    else if (!IsIgnoredProperty(name))
      return E_INVALIDARG;
  }
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32 *type))
{
  *type = (UInt32)(Int32)NFileTimeType::kNotDefined;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream * /* outStream */, UInt32 /* numItems */,
    IArchiveUpdateCallback * /* callback */))
{
  return E_NOTIMPL;
}

} // namespace NXdb
} // namespace NArchive
