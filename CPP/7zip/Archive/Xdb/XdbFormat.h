// XdbFormat.h
// On-disk constants and items of S.T.A.L.K.E.R. DB archives (docs/db-format.md).

#ifndef ZIP7_INC_XDB_FORMAT_H
#define ZIP7_INC_XDB_FORMAT_H

#include "Common/MyString.h"
#include "Common/MyTypes.h"
#include "Common/MyVector.h"

#include "XdbScrambler.h"

namespace NArchive {
namespace NXdb {

namespace NChunkId
{
  const UInt32 kData = 0;
  const UInt32 kHeader = 1;
  const UInt32 kUserData = 0x29A;

  // Set in the id of a chunk whose payload is LZHUF-compressed.
  const UInt32 kCompressedFlag = 0x80000000;
}

const unsigned kChunkHeaderSize = 8; // UInt32 id, UInt32 size

// Names are stored in the Windows Cyrillic code page.
const unsigned kNameCodePage = 1251;

// Update errors that 7-Zip shows with a message naming the problem.
const HRESULT k_hres_TooLarge = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);         // past a 4 GiB field
const HRESULT k_hres_BadName = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);            // empty name or part
const HRESULT k_hres_NotInCodePage = HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
const HRESULT k_hres_NameInUse = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);

enum EVersion
{
  kVersion_11xx,
  kVersion_2215,
  kVersion_2945,
  kVersion_2947ru,
  kVersion_2947ww,
  kVersion_Xdb
};

const unsigned kNumVersions = 6;

enum ERecordLayout
{
  kLayout_11xx,
  kLayout_2215,
  kLayout_2945,
  kLayout_2947
};

enum EMethod
{
  kMethod_Store,
  kMethod_Lzo,
  kMethod_Lzhuf
};

struct CVersionInfo
{
  const char *Name;          // as shown in 7-Zip and accepted by the "dbver" property
  ERecordLayout Layout;
  bool IsScrambled;
  EScramblerKey ScramblerKey; // only if IsScrambled
  EMethod FileMethod;         // how compressed files are stored
  bool HasCrc;
  bool HasUserData;
};

const CVersionInfo &GetVersionInfo(EVersion version);

// Accepts the names from CVersionInfo, case-insensitively.
bool FindVersion(const UString &name, EVersion &version);

const char *GetMethodName(EMethod method);


struct CItem
{
  AString Name;    // as stored: CP1251, '\' separators, no trailing separator
  UInt32 Offset;   // absolute position in the archive; 0 for directories
  UInt32 Size;
  UInt32 PackSize;
  UInt32 Crc;
  bool CrcDefined;
  bool IsDir;
  EMethod Method;

  CItem():
      Offset(0),
      Size(0),
      PackSize(0),
      Crc(0),
      CrcDefined(false),
      IsDir(false),
      Method(kMethod_Store)
      {}

  UString GetPath() const;
};

/*
  Converts a path from an update callback to the stored form: CP1251,
  ASCII-lowercase, '\' separators, no leading or trailing separators.
  Fails with k_hres_BadName for an empty path or an empty part ("a\\b"), and
  with k_hres_NotInCodePage for characters that CP1251 lacks.
*/
HRESULT PathToItemName(const UString &path, AString &name);

/*
  Finds names that occur more than once (ASCII case-insensitively). Of each
  group, the last entry is kept, at the position of the first. (sources[i])
  is the index of the entry that belongs at position i, or -1 to drop it.
*/
void FindDuplicateNames(const AStringVector &names, CIntVector &sources);

// Applies FindDuplicateNames() to anything with an AString Name.
template <class T>
void RemoveDuplicateNames(CObjectVector<T> &items)
{
  AStringVector names;
  FOR_VECTOR (i, items)
    names.Add(items[i].Name);
  CIntVector sources;
  FindDuplicateNames(names, sources);
  CObjectVector<T> result;
  FOR_VECTOR (i, items)
    if (sources[i] >= 0)
      result.Add(items[(unsigned)sources[i]]);
  items = result;
}

} // namespace NXdb
} // namespace NArchive

#endif
