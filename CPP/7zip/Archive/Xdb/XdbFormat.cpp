// XdbFormat.cpp

#include "StdAfx.h"

#include "Common/StringConvert.h"

#include "Archive/Common/ItemNameUtils.h"

#include "XdbFormat.h"

namespace NArchive {
namespace NXdb {

static const CVersionInfo kVersions[kNumVersions] =
{
  // Name      Layout        Scrambled  Key                FileMethod      Crc    UserData
  { "11xx",    kLayout_11xx, false,     kScramblerKey_RU, kMethod_Lzhuf, false, false },
  { "2215",    kLayout_2215, false,     kScramblerKey_RU, kMethod_Lzo,   false, false },
  { "2945",    kLayout_2945, false,     kScramblerKey_RU, kMethod_Lzo,   true,  false },
  { "2947ru",  kLayout_2947, true,      kScramblerKey_RU, kMethod_Lzo,   true,  false },
  { "2947ww",  kLayout_2947, true,      kScramblerKey_WW, kMethod_Lzo,   true,  false },
  { "xdb",     kLayout_2947, false,     kScramblerKey_RU, kMethod_Lzo,   true,  true  }
};

const CVersionInfo &GetVersionInfo(EVersion version)
{
  return kVersions[version];
}

bool FindVersion(const UString &name, EVersion &version)
{
  for (unsigned i = 0; i < kNumVersions; i++)
  {
    if (name.IsEqualTo_Ascii_NoCase(kVersions[i].Name))
    {
      version = (EVersion)i;
      return true;
    }
  }
  return false;
}

const char *GetMethodName(EMethod method)
{
  switch (method)
  {
    case kMethod_Lzo:   return "LZO";
    case kMethod_Lzhuf: return "LZHUF";
    case kMethod_Store: break;
  }
  return "Store";
}

UString CItem::GetPath() const
{
  UString path = MultiByteToUnicodeString(Name, kNameCodePage);
  path.Replace(L'\\', L'/');
  return NItemName::GetOsPath(path);
}

static bool IsSeparator(wchar_t c)
{
  return c == L'\\' || c == L'/';
}

bool PathToItemName(const UString &path, AString &name)
{
  UString s = path;
  s.Replace(L'/', L'\\');
  while (!s.IsEmpty() && IsSeparator(s.Back()))
    s.DeleteBack();
  unsigned start = 0;
  while (start < s.Len() && IsSeparator(s[start]))
    start++;
  s.DeleteFrontal(start);
  if (s.IsEmpty())
    return false;

  bool defaultCharUsed = false;
  name = UnicodeStringToMultiByte(s, kNameCodePage, '_', defaultCharUsed);
  if (defaultCharUsed)
    return false;
  // The engine looks names up in lowercase; only ASCII is folded, as in the original tools.
  name.MakeLower_Ascii();
  return true;
}

} // namespace NXdb
} // namespace NArchive
