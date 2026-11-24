#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "TestArchive.h"
#include "TestCallbacks.h"

using namespace NArchive;
using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

const EVersion kAllVersions[] =
{
  kVersion_11xx, kVersion_2215, kVersion_2945, kVersion_2947ru, kVersion_2947ww, kVersion_Xdb
};

struct COpenedArchive
{
  CBytes Bytes;
  CMyComPtr<IInArchive> Handler;

  HRESULT Open(const CBytes &bytes, const CProps &props = {})
  {
    Bytes = bytes;
    Handler = CreateHandler();
    if (!props.empty())
    {
      const HRESULT res = SetProps(Handler, props);
      if (res != S_OK)
        return res;
    }
    return Handler->Open(MakeInStream(Bytes), nullptr, nullptr);
  }

  UInt32 NumItems()
  {
    UInt32 numItems = 0;
    Handler->GetNumberOfItems(&numItems);
    return numItems;
  }

  CExtractResults ExtractAll(bool testMode = false)
  {
    CExtractCallback *spec = new CExtractCallback;
    CMyComPtr<IArchiveExtractCallback> callback = spec;
    EXPECT_EQ(Handler->Extract(nullptr, (UInt32)(Int32)-1, testMode ? 1 : 0, callback), S_OK);
    return spec->Results;
  }
};

// Position of a file's data in an archive built by BuildArchive().
size_t FindData(const CBytes &archive, const CBytes &data)
{
  const auto it = std::search(archive.begin(), archive.end(), data.begin(), data.end());
  return (size_t)(it - archive.begin());
}

TEST(Handler, ListsItemsAndProperties)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  COpenedArchive arc;
  ASSERT_EQ(arc.Open(BuildArchive(kVersion_Xdb, files)), S_OK);
  ASSERT_EQ(arc.NumItems(), files.size());

  EXPECT_EQ(GetStringProp(arc.Handler, 1, kpidPath), UString(L"config\\system.ltx"));
  EXPECT_EQ(GetUInt64Prop(arc.Handler, 1, kpidSize), files[1].Data.size());
  EXPECT_EQ(GetStringProp(arc.Handler, 1, kpidMethod), UString(L"LZO"));
  EXPECT_EQ(GetStringProp(arc.Handler, 2, kpidMethod), UString(L"Store"));

  NWindows::NCOM::CPropVariant isDir;
  arc.Handler->GetProperty(0, kpidIsDir, &isDir);
  EXPECT_EQ(isDir.vt, VT_BOOL);
  EXPECT_NE(isDir.boolVal, VARIANT_FALSE);

  EXPECT_EQ(GetArcStringProp(arc.Handler, kpidUserDefined + 1), UString(L"xdb"));
}

TEST(Handler, ExtractsEveryVersion)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  for (EVersion version : kAllVersions)
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    COpenedArchive arc;
    ASSERT_EQ(arc.Open(BuildArchive(version, files)), S_OK);
    CExtractResults result = arc.ExtractAll();
    for (UInt32 i = 0; i < files.size(); i++)
    {
      EXPECT_EQ(result.Results[i], NExtract::NOperationResult::kOK) << i;
      if (!files[i].IsDir)
        EXPECT_EQ(result.Data[i], files[i].Data) << i;
    }
    UInt64 totalSize = 0;
    for (const CTestFile &file : files)
      totalSize += file.Data.size();
    EXPECT_EQ(result.Total, totalSize);
    EXPECT_EQ(result.LastCompleted, totalSize);
  }
}

TEST(Handler, TestModeChecksWithoutOutput)
{
  COpenedArchive arc;
  ASSERT_EQ(arc.Open(BuildArchive(kVersion_2947ww, MakeSampleFiles())), S_OK);
  CExtractResults result = arc.ExtractAll(true);
  EXPECT_TRUE(result.Data.empty());
  for (const auto &opRes : result.Results)
    EXPECT_EQ(opRes.second, NExtract::NOperationResult::kOK);
}

TEST(Handler, ReportsCrcError)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  CBytes bytes = BuildArchive(kVersion_Xdb, files);
  bytes[FindData(bytes, files[2].Data) + 10] ^= 1; // the stored random file

  COpenedArchive arc;
  ASSERT_EQ(arc.Open(bytes), S_OK);
  CExtractResults result = arc.ExtractAll();
  EXPECT_EQ(result.Results[1], NExtract::NOperationResult::kOK);
  EXPECT_EQ(result.Results[2], NExtract::NOperationResult::kCRCError);
}

TEST(Handler, ReportsDataErrorInCompressedFile)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  CBytes bytes = BuildArchive(kVersion_2215, files); // no CRCs: only decoding can notice
  const CBytes packed = PackFile(kMethod_Lzo, files[1].Data);
  const size_t pos = FindData(bytes, packed);
  for (size_t i = 0; i < packed.size(); i += 7)
    bytes[pos + i] ^= 0x5A;

  COpenedArchive arc;
  ASSERT_EQ(arc.Open(bytes), S_OK);
  CExtractResults result = arc.ExtractAll();
  EXPECT_EQ(result.Results[1], NExtract::NOperationResult::kDataError);
  EXPECT_EQ(result.Results[4], NExtract::NOperationResult::kOK);
}

TEST(Handler, ShowsUserDataAsFile)
{
  const CBytes userData = MakeTextBytes(300);
  CArchiveOptions options;
  options.UserData = &userData;
  const std::vector<CTestFile> files = MakeSampleFiles();
  COpenedArchive arc;
  ASSERT_EQ(arc.Open(BuildArchive(kVersion_Xdb, files, options)), S_OK);

  const UInt32 index = (UInt32)files.size();
  ASSERT_EQ(arc.NumItems(), index + 1);
  EXPECT_EQ(GetStringProp(arc.Handler, index, kpidPath), UString(L"[userdata].ltx"));
  EXPECT_EQ(GetUInt64Prop(arc.Handler, index, kpidSize), userData.size());

  CExtractResults result = arc.ExtractAll();
  EXPECT_EQ(result.Results[index], NExtract::NOperationResult::kOK);
  EXPECT_EQ(result.Data[index], userData);
  EXPECT_FALSE(GetArcStringProp(arc.Handler, kpidUserDefined).IsEmpty());
}

TEST(Handler, ReportsHeadersError)
{
  CBytes bytes;
  AppendChunk(bytes, NChunkId::kData, MakeTextBytes(100));
  AppendChunk(bytes, NChunkId::kHeader | NChunkId::kCompressedFlag, MakeRandomBytes(300));
  COpenedArchive arc;
  ASSERT_EQ(arc.Open(bytes), S_OK);
  NWindows::NCOM::CPropVariant flags;
  arc.Handler->GetArchiveProperty(kpidErrorFlags, &flags);
  ASSERT_EQ(flags.vt, VT_UI4);
  EXPECT_NE(flags.ulVal & kpv_ErrorFlags_HeadersError, 0u);
}

TEST(Handler, RejectsOtherFiles)
{
  COpenedArchive arc;
  EXPECT_EQ(arc.Open(MakeRandomBytes(5000)), S_FALSE);
}

TEST(Handler, RemovesDuplicatesUnlessKeepdups)
{
  std::vector<CTestFile> files = MakeSampleFiles();
  files.push_back(TestFile("CONFIG\\SYSTEM.LTX", MakeTextBytes(10, 9), false));
  const CBytes bytes = BuildArchive(kVersion_Xdb, files);

  COpenedArchive arc;
  ASSERT_EQ(arc.Open(bytes), S_OK);
  EXPECT_EQ(arc.NumItems(), files.size() - 1);
  EXPECT_EQ(GetUInt64Prop(arc.Handler, 1, kpidSize), 10u); // the later copy wins

  ASSERT_EQ(arc.Open(bytes, { { L"keepdups", NWindows::NCOM::CPropVariant(L"on") } }), S_OK);
  EXPECT_EQ(arc.NumItems(), files.size());
}

// The command line also passes -m properties when it opens the archive to
// update. "dbver" names the version to write, so it must not affect reading.
TEST(Handler, DbverDoesNotAffectReading)
{
  const CBytes bytes = BuildArchive(kVersion_2215, MakeSampleFiles());
  COpenedArchive arc;
  ASSERT_EQ(arc.Open(bytes, { { L"dbver", NWindows::NCOM::CPropVariant(L"xdb") } }), S_OK);
  EXPECT_EQ(GetArcStringProp(arc.Handler, kpidUserDefined + 1), UString(L"2215"));
  EXPECT_EQ(arc.NumItems(), MakeSampleFiles().size());
}


HRESULT SetOne(const wchar_t *name, const NWindows::NCOM::CPropVariant &value)
{
  return SetProps(CreateHandler(), { { name, value } });
}

TEST(Properties, AcceptsKnownValues)
{
  using NWindows::NCOM::CPropVariant;
  EXPECT_EQ(SetOne(L"dbver", CPropVariant(L"auto")), S_OK);
  EXPECT_EQ(SetOne(L"dbver", CPropVariant(L"2947WW")), S_OK);
  EXPECT_EQ(SetOne(L"dbver", CPropVariant((UInt32)2945)), S_OK);
  EXPECT_EQ(SetOne(L"compress", CPropVariant(L"auto")), S_OK);
  EXPECT_EQ(SetOne(L"compress", CPropVariant(L"off")), S_OK);
  EXPECT_EQ(SetOne(L"compress", CPropVariant(L"true")), S_OK);
  EXPECT_EQ(SetOne(L"gen_xdb_ud", CPropVariant(L"-")), S_OK);
  EXPECT_EQ(SetOne(L"keepdups", CPropVariant()), S_OK); // "keepdups" alone means on
}

TEST(Properties, IgnoresGenericProperties)
{
  using NWindows::NCOM::CPropVariant;
  for (const wchar_t *name : { L"x", L"x9", L"mt", L"mt4", L"memuse", L"m", L"tm", L"tc", L"ta", L"tp1" })
    EXPECT_EQ(SetOne(name, CPropVariant((UInt32)5)), S_OK) << name;
}

TEST(Properties, RejectsUnknownOrInvalid)
{
  using NWindows::NCOM::CPropVariant;
  EXPECT_EQ(SetOne(L"dbver", CPropVariant(L"2946")), E_INVALIDARG);
  EXPECT_EQ(SetOne(L"compress", CPropVariant(L"sometimes")), E_INVALIDARG);
  EXPECT_EQ(SetOne(L"xfoo", CPropVariant(L"1")), E_INVALIDARG);
  EXPECT_EQ(SetOne(L"level", CPropVariant(L"1")), E_INVALIDARG);
  EXPECT_EQ(SetOne(L"xdb_ud", CPropVariant(L"C:\\does\\not\\exist.ltx")), E_INVALIDARG);
}

class UserDataFile : public testing::Test
{
protected:
  void SetUp() override
  {
    Dir = std::filesystem::temp_directory_path() / "xdb tests" / "user data";
    std::filesystem::create_directories(Dir);
    Path = Dir / "ud.ltx";
    std::ofstream(Path, std::ios::binary) << "[header]\ncreator = test\n";
  }

  void TearDown() override { std::filesystem::remove_all(Dir.parent_path()); }

  std::filesystem::path Dir;
  std::filesystem::path Path;
};

TEST_F(UserDataFile, AcceptsPathWithSpaces)
{
  EXPECT_EQ(SetOne(L"xdb_ud", NWindows::NCOM::CPropVariant(Path.wstring().c_str())), S_OK);
}

TEST_F(UserDataFile, JoinsQuotedPathSplitByStockSevenZip)
{
  // xdb_ud="<path>" as stock 7-Zip delivers it from the GUI: split at each space.
  const std::wstring quoted = L"\"" + Path.wstring() + L"\"";
  CProps props;
  size_t start = 0;
  bool first = true;
  while (start < quoted.size())
  {
    size_t end = quoted.find(L' ', start);
    if (end == std::wstring::npos)
      end = quoted.size();
    const std::wstring token = quoted.substr(start, end - start);
    if (first)
      props.push_back({ L"xdb_ud", NWindows::NCOM::CPropVariant(token.c_str()) });
    else
      props.push_back({ token, NWindows::NCOM::CPropVariant() });
    first = false;
    start = end + 1;
  }
  ASSERT_GT(props.size(), 1u);
  props.push_back({ L"compress", NWindows::NCOM::CPropVariant(L"on") });
  EXPECT_EQ(SetProps(CreateHandler(), props), S_OK);
}

} // namespace
