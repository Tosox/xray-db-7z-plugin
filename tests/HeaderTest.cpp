#include <gtest/gtest.h>

#include "XdbHeader.h"

#include "TestData.h"

using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

// Builds header bytes by hand, independently of WriteHeader().
class CBytesBuilder
{
public:
  CBytes Bytes;

  CBytesBuilder &U16(UInt32 v)
  {
    Bytes.push_back((Byte)v);
    Bytes.push_back((Byte)(v >> 8));
    return *this;
  }

  CBytesBuilder &U32(UInt32 v)
  {
    for (int i = 0; i < 4; i++)
      Bytes.push_back((Byte)(v >> (8 * i)));
    return *this;
  }

  CBytesBuilder &Str(const char *s)
  {
    while (*s)
      Bytes.push_back((Byte)*s++);
    return *this;
  }

  CBytesBuilder &CStr(const char *s)
  {
    Str(s);
    Bytes.push_back(0);
    return *this;
  }
};

bool Parse(EVersion version, const CBytes &bytes, CObjectVector<CItem> &items)
{
  return ParseHeader(version, bytes.data(), bytes.size(), items);
}

CItem MakeFile(const char *name, UInt32 offset, UInt32 size, UInt32 packSize, UInt32 crc, EMethod method)
{
  CItem item;
  item.Name = name;
  item.Offset = offset;
  item.Size = size;
  item.PackSize = packSize;
  item.Crc = crc;
  item.Method = method;
  return item;
}

CItem MakeDir(const char *name)
{
  CItem item;
  item.Name = name;
  item.IsDir = true;
  return item;
}

// A small archive's worth of items, valid for a data chunk of [8, 8 + 600).
CObjectVector<CItem> MakeSampleItems(EVersion version)
{
  const CVersionInfo &info = GetVersionInfo(version);
  const EMethod compressed = info.FileMethod;
  CObjectVector<CItem> items;
  items.Add(MakeDir("config"));
  items.Add(MakeDir("config\\weapons"));
  items.Add(MakeFile("config\\system.ltx", 8, 300, 120, 0x11111111, compressed));
  items.Add(MakeFile("config\\weapons\\w_ak74.ltx", 128, 200, 200, 0x22222222, kMethod_Store));
  items.Add(MakeFile("config\\empty.ltx", 328, 0, 0, 0, kMethod_Store));
  items.Add(MakeFile("scripts\\bind_stalker.script", 328, 900, 280, 0x33333333, compressed));
  for (unsigned i = 0; i < items.Size(); i++)
  {
    CItem &item = items[i];
    item.CrcDefined = info.HasCrc && !item.IsDir;
    if (!item.CrcDefined)
      item.Crc = 0;
    // 11xx stores no real size for compressed files; it comes from the data.
    if (info.Layout == kLayout_11xx && item.Method == kMethod_Lzhuf)
      item.Size = 0;
  }
  return items;
}

void ExpectSameItems(const CObjectVector<CItem> &actual, const CObjectVector<CItem> &expected)
{
  ASSERT_EQ(actual.Size(), expected.Size());
  for (unsigned i = 0; i < actual.Size(); i++)
  {
    const CItem &a = actual[i];
    const CItem &e = expected[i];
    EXPECT_STREQ(a.Name, e.Name) << i;
    EXPECT_EQ(a.IsDir, e.IsDir) << i;
    EXPECT_EQ(a.Offset, e.Offset) << i;
    EXPECT_EQ(a.Size, e.Size) << i;
    EXPECT_EQ(a.PackSize, e.PackSize) << i;
    EXPECT_EQ(a.CrcDefined, e.CrcDefined) << i;
    EXPECT_EQ(a.Crc, e.Crc) << i;
    EXPECT_EQ(a.Method, e.Method) << i;
  }
}

const EVersion kAllVersions[] =
{
  kVersion_11xx, kVersion_2215, kVersion_2945, kVersion_2947ru, kVersion_2947ww, kVersion_Xdb
};


TEST(Header, Parses2947Layout)
{
  const CBytes bytes = CBytesBuilder()
      .U16(7 + 16).U32(0).U32(0).U32(0).Str("config\\").U32(0)
      .U16(17 + 16).U32(300).U32(120).U32(0xDEADBEEF).Str("config\\system.ltx").U32(8)
      .Bytes;
  CObjectVector<CItem> items;
  ASSERT_TRUE(Parse(kVersion_Xdb, bytes, items));
  ASSERT_EQ(items.Size(), 2u);

  EXPECT_STREQ(items[0].Name, "config");
  EXPECT_TRUE(items[0].IsDir);
  EXPECT_FALSE(items[0].CrcDefined);

  EXPECT_STREQ(items[1].Name, "config\\system.ltx");
  EXPECT_FALSE(items[1].IsDir);
  EXPECT_EQ(items[1].Offset, 8u);
  EXPECT_EQ(items[1].Size, 300u);
  EXPECT_EQ(items[1].PackSize, 120u);
  EXPECT_EQ(items[1].Crc, 0xDEADBEEFu);
  EXPECT_TRUE(items[1].CrcDefined);
  EXPECT_EQ(items[1].Method, kMethod_Lzo);
}

TEST(Header, Parses2945Layout)
{
  const CBytes bytes = CBytesBuilder().CStr("a.ltx").U32(0xCAFE).U32(8).U32(10).U32(10).Bytes;
  CObjectVector<CItem> items;
  ASSERT_TRUE(Parse(kVersion_2945, bytes, items));
  ASSERT_EQ(items.Size(), 1u);
  EXPECT_EQ(items[0].Crc, 0xCAFEu);
  EXPECT_EQ(items[0].Offset, 8u);
  EXPECT_EQ(items[0].Method, kMethod_Store);
}

TEST(Header, Parses2215Layout)
{
  const CBytes bytes = CBytesBuilder().CStr("a.ltx").U32(8).U32(100).U32(40).Bytes;
  CObjectVector<CItem> items;
  ASSERT_TRUE(Parse(kVersion_2215, bytes, items));
  ASSERT_EQ(items.Size(), 1u);
  EXPECT_EQ(items[0].Size, 100u);
  EXPECT_EQ(items[0].PackSize, 40u);
  EXPECT_FALSE(items[0].CrcDefined);
  EXPECT_EQ(items[0].Method, kMethod_Lzo);
}

TEST(Header, Parses11xxLayout)
{
  const CBytes bytes = CBytesBuilder()
      .CStr("stored.ltx").U32(1).U32(8).U32(100)
      .CStr("packed.ltx").U32(0).U32(108).U32(40)
      .Bytes;
  CObjectVector<CItem> items;
  ASSERT_TRUE(Parse(kVersion_11xx, bytes, items));
  ASSERT_EQ(items.Size(), 2u);
  EXPECT_EQ(items[0].Method, kMethod_Store);
  EXPECT_EQ(items[0].Size, 100u);
  EXPECT_EQ(items[1].Method, kMethod_Lzhuf);
  EXPECT_EQ(items[1].PackSize, 40u);
  EXPECT_EQ(items[1].Size, 0u); // read from the LZHUF stream later
}

TEST(Header, EmptyHeaderHasNoItems)
{
  for (EVersion version : kAllVersions)
  {
    CObjectVector<CItem> items;
    EXPECT_TRUE(ParseHeader(version, nullptr, 0, items));
    EXPECT_EQ(items.Size(), 0u);
  }
}

TEST(Header, RoundTripAllVersions)
{
  for (EVersion version : kAllVersions)
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    const CObjectVector<CItem> items = MakeSampleItems(version);
    CByteBuffer header;
    ASSERT_TRUE(WriteHeader(version, items, header));
    CObjectVector<CItem> parsed;
    ASSERT_TRUE(ParseHeader(version, header, header.Size(), parsed));
    ExpectSameItems(parsed, items);
  }
}

TEST(Header, DirectoriesAreWrittenWithTrailingBackslash)
{
  CObjectVector<CItem> items;
  items.Add(MakeDir("textures"));
  CByteBuffer header;
  ASSERT_TRUE(WriteHeader(kVersion_Xdb, items, header));
  const CBytes expected = CBytesBuilder().U16(9 + 16).U32(0).U32(0).U32(0).Str("textures\\").U32(0).Bytes;
  EXPECT_EQ(ToBytes(header), expected);
}

// Layouts that look alike must reject each other once a real file is present,
// either while parsing or when the offsets are checked against the data chunk.
TEST(Header, LayoutsDoNotParseAsEachOther)
{
  for (EVersion written : kAllVersions)
  {
    CByteBuffer header;
    ASSERT_TRUE(WriteHeader(written, MakeSampleItems(written), header));
    for (EVersion parsedAs : kAllVersions)
    {
      if (GetVersionInfo(parsedAs).Layout == GetVersionInfo(written).Layout)
        continue;
      CObjectVector<CItem> items;
      const bool accepted = ParseHeader(parsedAs, header, header.Size(), items)
          && AreItemsInRange(items, 8, 8 + 600);
      EXPECT_FALSE(accepted) << GetVersionInfo(written).Name << " parsed as " << GetVersionInfo(parsedAs).Name;
    }
  }
}

TEST(Header, Compressed11xxIsNotMistakenFor2215)
{
  // As 2215, the 11xx "stored = 0" field becomes offset 0, i.e. a directory
  // with non-zero sizes.
  const CBytes bytes = CBytesBuilder().CStr("a.ltx").U32(0).U32(8).U32(40).Bytes;
  CObjectVector<CItem> items;
  EXPECT_TRUE(Parse(kVersion_11xx, bytes, items));
  EXPECT_FALSE(Parse(kVersion_2215, bytes, items));
}

TEST(Header, RejectsMalformedRecords)
{
  CObjectVector<CItem> items;
  // truncated
  CBytes bytes = CBytesBuilder().CStr("a.ltx").U32(8).U32(10).U32(10).Bytes;
  bytes.pop_back();
  EXPECT_FALSE(Parse(kVersion_2215, bytes, items));
  // trailing garbage
  bytes = CBytesBuilder().CStr("a.ltx").U32(8).U32(10).U32(10).Str("x").Bytes;
  EXPECT_FALSE(Parse(kVersion_2215, bytes, items));
  // empty name
  EXPECT_FALSE(Parse(kVersion_2215, CBytesBuilder().CStr("").U32(8).U32(10).U32(10).Bytes, items));
  // control character in name
  EXPECT_FALSE(Parse(kVersion_2215, CBytesBuilder().CStr("a\x01.ltx").U32(8).U32(10).U32(10).Bytes, items));
  // "directory" with a size
  EXPECT_FALSE(Parse(kVersion_2215, CBytesBuilder().CStr("dir\\").U32(0).U32(10).U32(10).Bytes, items));
  // 11xx "stored" field other than 0 or 1
  EXPECT_FALSE(Parse(kVersion_11xx, CBytesBuilder().CStr("a.ltx").U32(2).U32(8).U32(10).Bytes, items));
  // 2947 record size smaller than its fixed part
  EXPECT_FALSE(Parse(kVersion_Xdb, CBytesBuilder().U16(15).U32(0).U32(0).U32(0).U32(8).Bytes, items));
}

TEST(Header, AreItemsInRange)
{
  CObjectVector<CItem> items;
  items.Add(MakeDir("dir"));
  items.Add(MakeFile("a", 8, 10, 10, 0, kMethod_Store));
  EXPECT_TRUE(AreItemsInRange(items, 8, 18));
  EXPECT_FALSE(AreItemsInRange(items, 8, 17));
  EXPECT_FALSE(AreItemsInRange(items, 9, 100));
}

TEST(Header, RejectsNameTooLongFor2947)
{
  CObjectVector<CItem> items;
  CItem item = MakeFile("", 8, 0, 0, 0, kMethod_Store);
  for (unsigned i = 0; i < 0x10000; i++)
    item.Name.Add_Char('a');
  items.Add(item);
  CByteBuffer header;
  EXPECT_FALSE(WriteHeader(kVersion_Xdb, items, header));
  EXPECT_TRUE(WriteHeader(kVersion_2945, items, header));
}


TEST(ItemName, ConvertsCyrillicAndLowercasesAscii)
{
  AString name;
  ASSERT_TRUE(PathToItemName(UString(L"Конфиг\\Weapons/Файл.LTX"), name));
  // CP1251: К=0xCA о=0xEE н=0xED ф=0xF4 и=0xE8 г=0xE3, Ф=0xD4 а=0xE0 й=0xE9 л=0xEB
  EXPECT_STREQ(name, "\xCA\xEE\xED\xF4\xE8\xE3\\weapons\\\xD4\xE0\xE9\xEB.ltx");

  CItem item;
  item.Name = name;
  EXPECT_EQ(item.GetPath(), UString(L"Конфиг\\weapons\\Файл.ltx"));
}

TEST(ItemName, StripsSeparatorsAndRejectsEmptyOrUnencodable)
{
  AString name;
  ASSERT_TRUE(PathToItemName(UString(L"\\gamedata\\"), name));
  EXPECT_STREQ(name, "gamedata");
  EXPECT_FALSE(PathToItemName(UString(L"\\"), name));
  EXPECT_FALSE(PathToItemName(UString(L"日本.ltx"), name));
}

TEST(Version, FindByName)
{
  EVersion version;
  ASSERT_TRUE(FindVersion(UString(L"XDB"), version));
  EXPECT_EQ(version, kVersion_Xdb);
  ASSERT_TRUE(FindVersion(UString(L"2947ww"), version));
  EXPECT_EQ(version, kVersion_2947ww);
  EXPECT_FALSE(FindVersion(UString(L"2946"), version));
}

} // namespace
