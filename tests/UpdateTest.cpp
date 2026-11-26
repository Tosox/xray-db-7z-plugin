#include <algorithm>

#include <gtest/gtest.h>

#include "CpuArch.h"

#include "XdbIn.h"

#include "TestArchive.h"
#include "TestCallbacks.h"

using namespace NArchive;
using namespace NArchive::NXdb;
using namespace NXdbTest;
using NWindows::NCOM::CPropVariant;

namespace {

const EVersion kAllVersions[] =
{
  kVersion_11xx, kVersion_2215, kVersion_2945, kVersion_2947ru, kVersion_2947ww, kVersion_Xdb
};

std::vector<CUpdateEntry> MakeNewEntries()
{
  return {
    NewDir(L"Config"),
    NewFile(L"Config\\System.LTX", MakeTextBytes(8000, 1)),
    NewFile(L"config/random.bin", MakeRandomBytes(3000, 2)),
    NewFile(L"config\\empty.ltx", CBytes()),
    NewFile(L"scripts\\bind_stalker.script", MakeTextBytes(30000, 3)),
  };
}

// Opens (bytes) with the plugin's reader, independently of CHandler.
CInArchive ReadArchive(const CBytes &bytes)
{
  CInArchive arc;
  EXPECT_EQ(arc.Open(MakeInStream(bytes), nullptr), S_OK);
  EXPECT_FALSE(arc.HeadersError);
  return arc;
}

struct COpened
{
  CBytes Bytes;
  CMyComPtr<IInArchive> Handler;

  explicit COpened(const CBytes &bytes):
      Bytes(bytes),
      Handler(CreateHandler())
  {
    EXPECT_EQ(Handler->Open(MakeInStream(Bytes), nullptr, nullptr), S_OK);
  }

  CExtractResults ExtractAll()
  {
    CExtractCallback *spec = new CExtractCallback;
    CMyComPtr<IArchiveExtractCallback> callback = spec;
    EXPECT_EQ(Handler->Extract(nullptr, (UInt32)(Int32)-1, 0, callback), S_OK);
    return spec->Results;
  }

  UInt32 NumItems()
  {
    UInt32 n = 0;
    Handler->GetNumberOfItems(&n);
    return n;
  }
};

void ExpectAllExtractOk(COpened &arc)
{
  const CExtractResults result = arc.ExtractAll();
  for (const auto &opRes : result.Results)
    EXPECT_EQ(opRes.second, NExtract::NOperationResult::kOK) << "item " << opRes.first;
}


TEST(Update, CreatesEveryVersion)
{
  const std::vector<CUpdateEntry> entries = MakeNewEntries();
  for (EVersion version : kAllVersions)
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    CBytes bytes;
    const std::wstring name(GetVersionInfo(version).Name, GetVersionInfo(version).Name
        + strlen(GetVersionInfo(version).Name));
    ASSERT_EQ(RunUpdate(CreateHandler(), entries, bytes, { { L"dbver", CPropVariant(name.c_str()) } }), S_OK);

    // 11xx keeps no directories; the others get a record for "scripts", which
    // was added by its file's path alone.
    std::vector<CUpdateEntry> kept;
    for (const CUpdateEntry &e : entries)
      if (!(e.IsDir && version == kVersion_11xx))
        kept.push_back(e);
    if (version != kVersion_11xx)
      kept.insert(kept.end() - 1, NewDir(L"scripts"));

    const CInArchive arc = ReadArchive(bytes);
    EXPECT_EQ(arc.Version, version);
    ASSERT_EQ(arc.Items.Size(), kept.size());
    const unsigned first = (version == kVersion_11xx) ? 0 : 1;
    EXPECT_STREQ(arc.Items[first].Name, "config\\system.ltx");
    EXPECT_STREQ(arc.Items[first + 1].Name, "config\\random.bin");
    EXPECT_NE(arc.Items[first].Method, kMethod_Store);    // text compresses
    EXPECT_EQ(arc.Items[first + 1].Method, kMethod_Store); // random data doesn't
    if (version != kVersion_11xx)
    {
      EXPECT_TRUE(arc.Items[4].IsDir);
      EXPECT_STREQ(arc.Items[4].Name, "scripts");
    }

    COpened opened(bytes);
    CExtractResults result = opened.ExtractAll();
    for (UInt32 i = 0; i < kept.size(); i++)
    {
      EXPECT_EQ(result.Results[i], NExtract::NOperationResult::kOK) << i;
      if (!kept[i].IsDir)
        EXPECT_EQ(result.Data[i], kept[i].Data) << i;
    }
  }
}

TEST(Update, NewArchivesDefaultToXdbWithGeneratedUserData)
{
  CBytes bytes;
  ASSERT_EQ(RunUpdate(CreateHandler(), MakeNewEntries(), bytes), S_OK);
  const CInArchive arc = ReadArchive(bytes);
  EXPECT_EQ(arc.Version, kVersion_Xdb);
  ASSERT_TRUE(arc.UserDataDefined);
  const std::string text((const char *)(const Byte *)arc.UserData, arc.UserData.Size());
  EXPECT_NE(text.find("[header]"), std::string::npos);

  ASSERT_EQ(RunUpdate(CreateHandler(), MakeNewEntries(), bytes, { { L"gen_xdb_ud", CPropVariant(L"off") } }), S_OK);
  EXPECT_FALSE(ReadArchive(bytes).UserDataDefined);
}

TEST(Update, CompressOffStoresEverything)
{
  CBytes bytes;
  ASSERT_EQ(RunUpdate(CreateHandler(), MakeNewEntries(), bytes, { { L"compress", CPropVariant(L"off") } }), S_OK);
  const CInArchive arc = ReadArchive(bytes);
  FOR_VECTOR (i, arc.Items)
    EXPECT_EQ(arc.Items[i].Method, kMethod_Store) << i;
  COpened opened(bytes);
  ExpectAllExtractOk(opened);
}

TEST(Update, WritesDirectoriesAndChunksInReferenceOrder)
{
  CBytes bytes;
  ASSERT_EQ(RunUpdate(CreateHandler(), MakeNewEntries(), bytes), S_OK);
  // USERDATA, DATA, HEADER (compressed)
  EXPECT_EQ(GetUi32(&bytes[0]), NChunkId::kUserData);
  const size_t dataPos = kChunkHeaderSize + GetUi32(&bytes[4]);
  EXPECT_EQ(GetUi32(&bytes[dataPos]), NChunkId::kData);
  const size_t headerPos = dataPos + kChunkHeaderSize + GetUi32(&bytes[dataPos + 4]);
  EXPECT_EQ(GetUi32(&bytes[headerPos]), NChunkId::kHeader | NChunkId::kCompressedFlag);
  EXPECT_EQ(headerPos + kChunkHeaderSize + GetUi32(&bytes[headerPos + 4]), bytes.size());

  const CInArchive arc = ReadArchive(bytes);
  EXPECT_TRUE(arc.Items[0].IsDir);
}

TEST(Update, AddsMissingDirectories)
{
  // "a" has its own record, later in the list; only "a\b" is missing.
  CBytes bytes;
  ASSERT_EQ(RunUpdate(CreateHandler(), { NewFile(L"A\\b\\c.ltx", MakeTextBytes(100)), NewDir(L"a") }, bytes), S_OK);
  const CInArchive arc = ReadArchive(bytes);
  ASSERT_EQ(arc.Items.Size(), 3u);
  EXPECT_TRUE(arc.Items[0].IsDir);
  EXPECT_STREQ(arc.Items[0].Name, "a\\b");
  EXPECT_STREQ(arc.Items[1].Name, "a\\b\\c.ltx");
  EXPECT_TRUE(arc.Items[2].IsDir);
  EXPECT_STREQ(arc.Items[2].Name, "a");
}

TEST(Update, CopiesUnchangedFilesByteForByte)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  COpened arc(BuildArchive(kVersion_Xdb, files));

  std::vector<CUpdateEntry> entries;
  for (Int32 i = 0; i < (Int32)files.size(); i++)
    entries.push_back(Keep(i));
  entries.push_back(NewFile(L"new.ltx", MakeTextBytes(500, 7)));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);

  // The LZO stream of a kept file appears unchanged in the new archive.
  const CBytes packed = PackFile(kMethod_Lzo, files[5].Data);
  EXPECT_NE(std::search(bytes.begin(), bytes.end(), packed.begin(), packed.end()), bytes.end());

  COpened updated(bytes);
  EXPECT_EQ(updated.NumItems(), files.size() + 1);
  ExpectAllExtractOk(updated);
}

// Updating something else must not make existing damage undetectable.
TEST(Update, KeepsTheCrcOfDamagedFiles)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  CBytes damaged = BuildArchive(kVersion_Xdb, files);
  const auto pos = std::search(damaged.begin(), damaged.end(), files[2].Data.begin(), files[2].Data.end());
  ASSERT_NE(pos, damaged.end());
  pos[100] ^= 0x10; // one flipped bit in the stored random.bin

  COpened arc(damaged);
  ASSERT_EQ(arc.ExtractAll().Results[2], NExtract::NOperationResult::kCRCError);
  const UInt32 storedCrc = ReadArchive(damaged).Items[2].Crc;

  std::vector<CUpdateEntry> entries;
  for (Int32 i = 0; i < (Int32)files.size(); i++)
    entries.push_back(Keep(i));
  entries.push_back(NewFile(L"new.ltx", MakeTextBytes(500, 7)));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);

  EXPECT_EQ(ReadArchive(bytes).Items[2].Crc, storedCrc);
  COpened updated(bytes);
  CExtractResults result = updated.ExtractAll();
  EXPECT_EQ(result.Results[2], NExtract::NOperationResult::kCRCError);
  EXPECT_EQ(result.Results[1], NExtract::NOperationResult::kOK);
}

TEST(Update, DeletesRenamesAndReplaces)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  COpened arc(BuildArchive(kVersion_2945, files));
  const CBytes replacement = MakeTextBytes(1234, 9);

  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, {
      Keep(0),
      Rename(1, L"config\\renamed.ltx"),
      // 2 is deleted
      Replace(3, L"config\\empty.ltx", replacement),
      Keep(4),
      Keep(5),
    }, bytes), S_OK);

  const CInArchive result = ReadArchive(bytes);
  EXPECT_EQ(result.Version, kVersion_2945);
  ASSERT_EQ(result.Items.Size(), 5u);
  EXPECT_STREQ(result.Items[1].Name, "config\\renamed.ltx");
  EXPECT_STREQ(result.Items[2].Name, "config\\empty.ltx");
  EXPECT_EQ(result.Items[2].Size, replacement.size());

  COpened updated(bytes);
  CExtractResults extracted = updated.ExtractAll();
  EXPECT_EQ(extracted.Data[1], files[1].Data);
  EXPECT_EQ(extracted.Data[2], replacement);
}

TEST(Update, ConvertsBetweenVersions)
{
  // 11xx (LZHUF, no CRC) and 2215 (LZO, no CRC) to xdb (LZO, CRC) and back.
  const std::vector<CTestFile> files = MakeSampleFiles();
  const EVersion conversions[][2] =
  {
    { kVersion_11xx, kVersion_Xdb },
    { kVersion_2215, kVersion_Xdb },
    { kVersion_Xdb, kVersion_11xx },
    { kVersion_2947ru, kVersion_2945 },
  };
  for (const auto &conversion : conversions)
  {
    const CVersionInfo &to = GetVersionInfo(conversion[1]);
    SCOPED_TRACE(std::string(GetVersionInfo(conversion[0]).Name) + " -> " + to.Name);
    COpened arc(BuildArchive(conversion[0], files));
    std::vector<CUpdateEntry> entries;
    for (Int32 i = 0; i < (Int32)arc.NumItems(); i++)
      entries.push_back(Keep(i));
    // Directories dropped by 11xx come back as generated records.
    const std::vector<CTestFile> expected = ExpectedFiles(conversion[1], files);
    const std::wstring name(to.Name, to.Name + strlen(to.Name));
    CBytes bytes;
    ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes, { { L"dbver", CPropVariant(name.c_str()) } }), S_OK);

    const CInArchive result = ReadArchive(bytes);
    EXPECT_EQ(result.Version, conversion[1]);
    FOR_VECTOR (i, result.Items)
    {
      const CItem &item = result.Items[i];
      if (item.IsDir)
        continue;
      EXPECT_TRUE(item.Method == kMethod_Store || item.Method == to.FileMethod) << i;
      EXPECT_EQ(item.CrcDefined, to.HasCrc) << i;
    }

    COpened converted(bytes);
    // Converting to xdb generates userdata, shown as [userdata].ltx after the files.
    EXPECT_EQ(converted.NumItems(), expected.size() + (conversion[1] == kVersion_Xdb ? 1 : 0));
    CExtractResults extracted = converted.ExtractAll();
    for (UInt32 i = 0; i < expected.size(); i++)
    {
      EXPECT_EQ(extracted.Results[i], NExtract::NOperationResult::kOK) << i;
      if (!expected[i].IsDir)
        EXPECT_EQ(extracted.Data[i], expected[i].Data) << i;
    }
  }
}

TEST(Update, KeepsTheVersionOfTheUpdatedArchive)
{
  COpened arc(BuildArchive(kVersion_2947ww, MakeSampleFiles()));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, { Keep(1), NewFile(L"added.ltx", MakeTextBytes(100)) }, bytes), S_OK);
  EXPECT_EQ(ReadArchive(bytes).Version, kVersion_2947ww);
}

TEST(Update, CompressAutoFollowsTheArchive)
{
  std::vector<CTestFile> files = { TestFile("a.ltx", MakeTextBytes(1000, 1), false),
                                   TestFile("b.ltx", MakeTextBytes(1000, 2), false) };
  COpened stored(BuildArchive(kVersion_Xdb, files));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(stored.Handler, { Keep(0), Keep(1), NewFile(L"c.ltx", MakeTextBytes(5000, 3)) }, bytes), S_OK);
  EXPECT_EQ(ReadArchive(bytes).Items[2].Method, kMethod_Store);
}

TEST(Update, LeavesTheHandlerUnchanged)
{
  COpened arc(BuildArchive(kVersion_Xdb, MakeSampleFiles()));
  const UInt32 numItems = arc.NumItems();
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, { Keep(1) }, bytes), S_OK);
  EXPECT_EQ(arc.NumItems(), numItems);
  ExpectAllExtractOk(arc);
}

TEST(Update, LastOfDuplicateNamesWins)
{
  CBytes bytes;
  ASSERT_EQ(RunUpdate(CreateHandler(), {
      NewFile(L"A.ltx", MakeTextBytes(10, 1)),
      NewFile(L"a.ltx", MakeTextBytes(20, 2)),
    }, bytes), S_OK);
  const CInArchive arc = ReadArchive(bytes);
  ASSERT_EQ(arc.Items.Size(), 1u);
  EXPECT_EQ(arc.Items[0].Size, 20u);
}

TEST(Update, RejectsNamesOutsideCp1251)
{
  CBytes bytes;
  EXPECT_EQ(RunUpdate(CreateHandler(), { NewFile(L"日本.ltx", MakeTextBytes(10)) }, bytes), E_INVALIDARG);
}

TEST(Update, CreatesAnEmptyArchive)
{
  CBytes bytes;
  ASSERT_EQ(RunUpdate(CreateHandler(), {}, bytes), S_OK);
  EXPECT_EQ(ReadArchive(bytes).Items.Size(), 0u);
}

TEST(Update, RefusesDamagedArchive)
{
  CBytes damaged;
  AppendChunk(damaged, NChunkId::kData, MakeTextBytes(100));
  AppendChunk(damaged, NChunkId::kHeader | NChunkId::kCompressedFlag, MakeRandomBytes(300));
  COpened arc(damaged);
  CBytes bytes;
  EXPECT_EQ(RunUpdate(arc.Handler, {}, bytes), E_NOTIMPL);
}

TEST(Update, NeedsASeekableStream)
{
  CMyComPtr<IOutArchive> outArchive;
  CMyComPtr<IInArchive> handler = CreateHandler();
  handler->QueryInterface(IID_IOutArchive, (void **)&outArchive);
  CUpdateCallback *callbackSpec = new CUpdateCallback;
  CMyComPtr<IArchiveUpdateCallback> callback = callbackSpec;
  CDynBufSeqOutStream *outSpec = new CDynBufSeqOutStream;
  CMyComPtr<ISequentialOutStream> outStream = outSpec;
  EXPECT_EQ(outArchive->UpdateItems(outStream, 0, callback), E_NOTIMPL);
}


class UserDataUpdate : public testing::Test
{
protected:
  const CBytes UserData = MakeTextBytes(200, 42);
  std::vector<CTestFile> Files = MakeSampleFiles();
  // The [userdata].ltx item comes after the files.
  const Int32 UserDataIndex = (Int32)MakeSampleFiles().size();

  CBytes BuildWithUserData()
  {
    CArchiveOptions options;
    options.UserData = &UserData;
    return BuildArchive(kVersion_Xdb, Files, options);
  }

  std::vector<CUpdateEntry> KeepFiles()
  {
    std::vector<CUpdateEntry> entries;
    for (Int32 i = 0; i < (Int32)Files.size(); i++)
      entries.push_back(Keep(i));
    return entries;
  }
};

TEST_F(UserDataUpdate, KeepsUserDataItem)
{
  COpened arc(BuildWithUserData());
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries.push_back(Keep(UserDataIndex));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);
  const CInArchive result = ReadArchive(bytes);
  ASSERT_TRUE(result.UserDataDefined);
  EXPECT_EQ(ToBytes(result.UserData), UserData);
  EXPECT_EQ(result.Items.Size(), Files.size()); // not stored as a file
}

TEST_F(UserDataUpdate, EditsUserDataItem)
{
  COpened arc(BuildWithUserData());
  const CBytes edited = MakeTextBytes(50, 43);
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries.push_back(Replace(UserDataIndex, L"[userdata].ltx", edited));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);
  EXPECT_EQ(ToBytes(ReadArchive(bytes).UserData), edited);
}

TEST_F(UserDataUpdate, DeletedUserDataStaysDeleted)
{
  COpened arc(BuildWithUserData());
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, KeepFiles(), bytes), S_OK); // gen_xdb_ud is on by default
  EXPECT_FALSE(ReadArchive(bytes).UserDataDefined);
}

TEST_F(UserDataUpdate, AddingTheFileSetsUserData)
{
  COpened arc(BuildArchive(kVersion_Xdb, Files)); // no userdata yet
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries.push_back(NewFile(L"[USERDATA].ltx", UserData));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);
  const CInArchive result = ReadArchive(bytes);
  EXPECT_EQ(ToBytes(result.UserData), UserData);
  EXPECT_EQ(result.Items.Size(), Files.size());
}

TEST_F(UserDataUpdate, RenamingAFileToTheNameSetsUserData)
{
  COpened arc(BuildArchive(kVersion_Xdb, Files)); // no userdata yet
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries[1] = Rename(1, L"[userdata].ltx");       // config\system.ltx, compressed
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);
  const CInArchive result = ReadArchive(bytes);
  ASSERT_TRUE(result.UserDataDefined);
  EXPECT_EQ(ToBytes(result.UserData), Files[1].Data);
  EXPECT_EQ(result.Items.Size(), Files.size() - 1);
}

TEST_F(UserDataUpdate, RenamingOntoTheUserDataReplacesIt)
{
  COpened arc(BuildWithUserData());
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries[1] = Rename(1, L"[userdata].ltx");
  entries.push_back(Keep(UserDataIndex));          // the old one comes later, and still loses
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);
  const CInArchive result = ReadArchive(bytes);
  EXPECT_EQ(ToBytes(result.UserData), Files[1].Data);
  EXPECT_EQ(result.Items.Size(), Files.size() - 1);
}

TEST_F(UserDataUpdate, RenamingTheUserDataKeepsItAsAFile)
{
  COpened arc(BuildWithUserData());
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries.push_back(Rename(UserDataIndex, L"notes.ltx"));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes), S_OK);
  const CInArchive result = ReadArchive(bytes);
  EXPECT_FALSE(result.UserDataDefined);
  ASSERT_EQ(result.Items.Size(), Files.size() + 1);
  EXPECT_STREQ(result.Items[(unsigned)Files.size()].Name, "notes.ltx");
  COpened renamed(bytes);
  EXPECT_EQ(renamed.ExtractAll().Data[(UInt32)Files.size()], UserData);
}

TEST_F(UserDataUpdate, ConvertingToOtherVersionsDropsUserData)
{
  COpened arc(BuildWithUserData());
  std::vector<CUpdateEntry> entries = KeepFiles();
  entries.push_back(Keep(UserDataIndex));
  CBytes bytes;
  ASSERT_EQ(RunUpdate(arc.Handler, entries, bytes, { { L"dbver", CPropVariant(L"2947ww") } }), S_OK);
  const CInArchive result = ReadArchive(bytes);
  EXPECT_EQ(result.Version, kVersion_2947ww);
  EXPECT_FALSE(result.UserDataDefined);
  EXPECT_EQ(result.Items.Size(), Files.size());
}

} // namespace
