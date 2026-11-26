// XdbHandlerOut.cpp
// Writing side of the handler: properties and updating.

#include "StdAfx.h"

#include "Common/ComTry.h"
#include "Common/Defs.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include "Windows/PropVariant.h"

#include "7zip/Common/MethodProps.h"
#include "7zip/Common/StreamUtils.h"

#include "XdbCodec.h"
#include "XdbHandler.h"
#include "XdbUpdate.h"

namespace NArchive {
namespace NXdb {

// A userdata file larger than this is certainly a mistake.
static const UInt64 kMaxUserDataFileSize = (UInt64)1 << 24;

// Written to new xdb archives when no userdata is given (gen_xdb_ud). The
// values are those the games use for archives that aren't multiplayer maps.
static const char kGeneratedUserData[] =
    "[header]\n"
    "auto_load = true\n"
    "entry_point = $fs_root$\\gamedata\\\n"
    "level_name = single\n"
    "level_ver = 1.0\n"
    "creator = \"Modder\"\n"
    "link = \"\"\n";

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
    return GetLastError_noZero_HRESULT();
  if (size > kMaxUserDataFileSize)
    return k_hres_TooLarge;
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

static HRESULT GetPathProp(IArchiveUpdateCallback *callback, UInt32 index, UString &path)
{
  NWindows::NCOM::CPropVariant prop;
  RINOK(callback->GetProperty(index, kpidPath, &prop))
  if (prop.vt != VT_BSTR)
    return E_INVALIDARG;
  path = prop.bstrVal;
  return S_OK;
}

static HRESULT GetIsDirProp(IArchiveUpdateCallback *callback, UInt32 index, bool &isDir)
{
  NWindows::NCOM::CPropVariant prop;
  RINOK(callback->GetProperty(index, kpidIsDir, &prop))
  if (prop.vt == VT_EMPTY)
    isDir = false;
  else if (prop.vt == VT_BOOL)
    isDir = (prop.boolVal != VARIANT_FALSE);
  else
    return E_INVALIDARG;
  return S_OK;
}

static HRESULT GetSizeProp(IArchiveUpdateCallback *callback, UInt32 index, UInt64 &size)
{
  NWindows::NCOM::CPropVariant prop;
  RINOK(callback->GetProperty(index, kpidSize, &prop))
  if (prop.vt == VT_EMPTY)
    size = 0;
  else if (prop.vt == VT_UI8)
    size = prop.uhVal.QuadPart;
  else
    return E_INVALIDARG;
  return S_OK;
}

static HRESULT ReadUserDataItem(IArchiveUpdateCallback *callback, UInt32 index, CByteBuffer &userData)
{
  CMyComPtr<ISequentialInStream> stream;
  RINOK(callback->GetStream(index, &stream))
  size_t size = 0;
  CByteBuffer buf;
  if (stream)
  {
    RINOK(ReadStreamToBuffer(stream, 0, buf, size))
    if (size > kMaxUserDataFileSize)
      return k_hres_TooLarge;
  }
  userData.CopyFrom(buf, size);
  return callback->SetOperationResult(NUpdate::NOperationResult::kOK);
}

// The contents of a file of the open archive, for a file renamed to the userdata name.
static HRESULT ReadArchivedFile(IInStream *stream, const CItem &item, CByteBuffer &data)
{
  if (item.Size > kMaxUserDataFileSize)
    return k_hres_TooLarge;
  CByteBuffer packed(item.PackSize);
  RINOK(InStream_SeekSet(stream, item.Offset))
  RINOK(ReadStream_FAIL(stream, packed, packed.Size()))
  return UnpackItemData(item.Method, packed, packed.Size(), item.Size, data) ? S_OK : S_FALSE;
}

// "compress=auto" follows the archive being updated.
static bool MostFilesAreCompressed(const CObjectVector<CItem> &items)
{
  unsigned numCompressed = 0;
  unsigned numStored = 0;
  FOR_VECTOR (i, items)
  {
    if (items[i].IsDir)
      continue;
    if (items[i].Method == kMethod_Store)
      numStored++;
    else
      numCompressed++;
  }
  return (numCompressed == 0 && numStored == 0) || numCompressed > numStored;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream *outStream, UInt32 numItems,
    IArchiveUpdateCallback *callback))
{
  COM_TRY_BEGIN
  // A damaged file table can't be rewritten without losing files.
  if (_isOpen && _arc.HeadersError)
    return E_NOTIMPL;

  CUpdateOptions options;
  options.Version = _options.VersionDefined ? _options.Version : (_isOpen ? _arc.Version : kVersion_Xdb);
  options.KeepDuplicates = _options.KeepDuplicates;
  switch (_options.CompressMode)
  {
    case kCompressMode_Always: options.Compress = true; break;
    case kCompressMode_Never:  options.Compress = false; break;
    case kCompressMode_Auto:   options.Compress = !_isOpen || MostFilesAreCompressed(_arc.Items); break;
  }
  const bool isXdb = (options.Version == kVersion_Xdb);

  CObjectVector<CUpdateItem> updateItems;
  bool userDataInList = false;
  bool userDataChanged = false;
  CByteBuffer userDataFromList;

  for (UInt32 i = 0; i < numItems; i++)
  {
    Int32 newData;
    Int32 newProps;
    UInt32 indexInArc;
    RINOK(callback->GetUpdateItemInfo(i, &newData, &newProps, &indexInArc))

    CUpdateItem ui;
    ui.IndexInClient = i;
    ui.NewData = IntToBool(newData);
    const bool inArc = (indexInArc != (UInt32)(Int32)-1);
    const bool fromUserDataItem = inArc && IsUserDataItem(indexInArc);
    if (inArc && !fromUserDataItem)
    {
      if (indexInArc >= _arc.Items.Size())
        return E_INVALIDARG;
      ui.IndexInArc = (int)indexInArc;
    }

    UString path;
    if (IntToBool(newProps) || !inArc)
    {
      RINOK(GetPathProp(callback, i, path))
      RINOK(GetIsDirProp(callback, i, ui.IsDir))
    }
    else if (fromUserDataItem)
      path = kUserDataItemName;
    else
    {
      const CItem &item = _arc.Items[indexInArc];
      ui.Name = item.Name;
      ui.IsDir = item.IsDir;
    }

    /*
      In an xdb archive, the userdata file is the userdata chunk: the shown
      userdata item, and any file added or renamed to its name. A real file
      that already had the name and is left alone stays a file. Added or
      renamed contents win over the unchanged userdata, as they replace it.
    */
    const bool renamed = inArc && !fromUserDataItem && IntToBool(newProps);
    if (isXdb && !ui.IsDir && (ui.NewData || renamed || fromUserDataItem) && path.IsEqualTo_Ascii_NoCase(kUserDataItemName))
    {
      if (ui.NewData)
      {
        RINOK(ReadUserDataItem(callback, i, userDataFromList))
        userDataChanged = true;
      }
      else if (renamed)
      {
        const HRESULT res = ReadArchivedFile(_stream, _arc.Items[indexInArc], userDataFromList);
        if (res != S_OK)
          return res == S_FALSE ? E_FAIL : res;
        userDataChanged = true;
      }
      else if (!userDataChanged)
        userDataFromList = _arc.UserData;
      userDataInList = true;
      continue;
    }

    if (fromUserDataItem)
    {
      if (ui.NewData)
      {
        // Other versions have no userdata.
        if (!isXdb)
          continue;
      }
      else
      {
        // Kept under its name for another version: dropped, as that has no
        // userdata. Renamed: a file with the userdata's contents.
        if (!IntToBool(newProps) || path.IsEqualTo_Ascii_NoCase(kUserDataItemName))
          continue;
        ui.DataInBuffer = true;
        ui.Data = _arc.UserData;
      }
    }

    if (ui.Name.IsEmpty())
    {
      // A file renamed to an empty name arrives as its folder's path.
      if (!ui.IsDir && (path.IsEmpty() || path.Back() == L'\\' || path.Back() == L'/'))
        return k_hres_BadName;
      RINOK(PathToItemName(path, ui.Name))
    }
    if (ui.NewData && !ui.IsDir)
    {
      RINOK(GetSizeProp(callback, i, ui.Size))
    }
    updateItems.Add(ui);
  }

  if (isXdb)
  {
    if (_options.UserDataDefined)
    {
      options.WriteUserData = true;
      options.UserData = _options.UserData;
    }
    else if (userDataInList)
    {
      options.WriteUserData = true;
      options.UserData = userDataFromList;
    }
    else if (_isOpen && _arc.Version == kVersion_Xdb)
    {
      // A shown userdata file that isn't in the list was deleted. Userdata
      // that couldn't be shown (a real file has its name) is kept.
      if (_arc.UserDataDefined && !_showUserDataItem)
      {
        options.WriteUserData = true;
        options.UserData = _arc.UserData;
      }
    }
    else if (_options.GenerateUserData)
    {
      options.WriteUserData = true;
      options.UserData.CopyFrom((const Byte *)kGeneratedUserData, sizeof(kGeneratedUserData) - 1);
    }
  }

  return UpdateArchive(_stream, _arc.Items, updateItems, options, outStream, callback);
  COM_TRY_END
}

} // namespace NXdb
} // namespace NArchive
