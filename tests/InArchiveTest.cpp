#include <gtest/gtest.h>

#include "XdbIn.h"

#include "TestArchive.h"

using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

const EVersion kAllVersions[] =
{
  kVersion_11xx, kVersion_2215, kVersion_2945, kVersion_2947ru, kVersion_2947ww, kVersion_Xdb
};

HRESULT Open(CInArchive &arc, const CBytes &bytes)
{
  return arc.Open(MakeInStream(bytes), nullptr);
}

TEST(InArchive, DetectsEveryVersion)
{
  const std::vector<CTestFile> files = MakeSampleFiles();
  for (EVersion version : kAllVersions)
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    const CBytes bytes = BuildArchive(version, files);
    CInArchive arc;
    ASSERT_EQ(Open(arc, bytes), S_OK);
    EXPECT_FALSE(arc.HeadersError);
    EXPECT_EQ(arc.Version, version);
    EXPECT_EQ(arc.PhySize, bytes.size());
    ASSERT_EQ(arc.Items.Size(), files.size());
    for (unsigned i = 0; i < files.size(); i++)
    {
      EXPECT_STREQ(arc.Items[i].Name, files[i].Name.c_str());
      EXPECT_EQ(arc.Items[i].IsDir, files[i].IsDir);
      // 11xx sizes of compressed files come from their LZHUF streams.
      EXPECT_EQ(arc.Items[i].Size, files[i].Data.size());
    }
  }
}

TEST(InArchive, DetectsVersionWithHeaderChunkFirst)
{
  for (EVersion version : kAllVersions)
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    CArchiveOptions options;
    options.HeaderFirst = true;
    CInArchive arc;
    ASSERT_EQ(Open(arc, BuildArchive(version, MakeSampleFiles(), options)), S_OK);
    EXPECT_FALSE(arc.HeadersError);
    EXPECT_EQ(arc.Version, version);
    EXPECT_EQ(arc.Items.Size(), MakeSampleFiles().size());
  }
}

TEST(InArchive, DetectsUncompressedHeaders)
{
  for (EVersion version : { kVersion_11xx, kVersion_2215, kVersion_2945, kVersion_Xdb })
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    CArchiveOptions options;
    options.CompressHeader = false;
    CInArchive arc;
    ASSERT_EQ(Open(arc, BuildArchive(version, MakeSampleFiles(), options)), S_OK);
    EXPECT_FALSE(arc.HeadersError);
    EXPECT_EQ(arc.Version, version);
  }
}

TEST(InArchive, ReadsUserData)
{
  const CBytes userData = MakeTextBytes(100);
  CArchiveOptions options;
  options.UserData = &userData;
  CInArchive arc;
  ASSERT_EQ(Open(arc, BuildArchive(kVersion_Xdb, MakeSampleFiles(), options)), S_OK);
  ASSERT_TRUE(arc.UserDataDefined);
  EXPECT_EQ(ToBytes(arc.UserData), userData);
}

TEST(InArchive, EmptyArchiveIsXdb)
{
  CInArchive arc;
  ASSERT_EQ(Open(arc, BuildArchive(kVersion_2215, {})), S_OK);
  EXPECT_FALSE(arc.HeadersError);
  EXPECT_EQ(arc.Version, kVersion_Xdb);
  EXPECT_EQ(arc.Items.Size(), 0u);
}

TEST(InArchive, RejectsWhatIsNotAnArchive)
{
  CInArchive arc;
  EXPECT_EQ(Open(arc, CBytes()), S_FALSE);
  EXPECT_EQ(Open(arc, MakeRandomBytes(1000)), S_FALSE);
  EXPECT_EQ(Open(arc, CBytes(1 << 16, 0)), S_FALSE); // a chain of empty data chunks

  CBytes onlyData;
  AppendChunk(onlyData, NChunkId::kData, MakeTextBytes(100));
  EXPECT_EQ(Open(arc, onlyData), S_FALSE);

  CBytes truncated = BuildArchive(kVersion_Xdb, MakeSampleFiles());
  truncated.pop_back();
  EXPECT_EQ(Open(arc, truncated), S_FALSE);

  // Empty data and header chunks, as zero bytes inside other files look.
  CBytes empty;
  AppendChunk(empty, NChunkId::kData, CBytes());
  AppendChunk(empty, NChunkId::kHeader, CBytes());
  EXPECT_EQ(Open(arc, empty), S_FALSE);
}

TEST(InArchive, ReportsCorruptHeader)
{
  CBytes bytes;
  AppendChunk(bytes, NChunkId::kData, MakeTextBytes(100));
  AppendChunk(bytes, NChunkId::kHeader | NChunkId::kCompressedFlag, MakeRandomBytes(300));
  CInArchive arc;
  ASSERT_EQ(Open(arc, bytes), S_OK);
  EXPECT_TRUE(arc.HeadersError);
  EXPECT_EQ(arc.Items.Size(), 0u);
}

TEST(InArchive, RejectsOffsetsOutsideTheDataChunk)
{
  // A valid xdb header whose data chunk was cut short.
  CBytes bytes;
  AppendChunk(bytes, NChunkId::kData, MakeTextBytes(10));
  CObjectVector<CItem> items;
  CItem &item = items.AddNew();
  item.Name = "a.ltx";
  item.Offset = 8;
  item.Size = item.PackSize = 100;
  CByteBuffer table;
  WriteHeader(kVersion_Xdb, items, table);
  AppendChunk(bytes, NChunkId::kHeader, ToBytes(table));

  CInArchive arc;
  ASSERT_EQ(Open(arc, bytes), S_OK);
  EXPECT_TRUE(arc.HeadersError);
}

TEST(InArchive, RemoveDuplicateNamesKeepsLastAtFirstPosition)
{
  CObjectVector<CItem> items;
  const char *names[] = { "a", "B", "c", "b", "A" };
  for (unsigned i = 0; i < 5; i++)
  {
    CItem &item = items.AddNew();
    item.Name = names[i];
    item.Size = i;
  }
  RemoveDuplicateNames(items);
  ASSERT_EQ(items.Size(), 3u);
  EXPECT_STREQ(items[0].Name, "A");
  EXPECT_EQ(items[0].Size, 4u);
  EXPECT_STREQ(items[1].Name, "b");
  EXPECT_EQ(items[1].Size, 3u);
  EXPECT_STREQ(items[2].Name, "c");
}

} // namespace
