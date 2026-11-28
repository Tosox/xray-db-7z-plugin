// Opt-in tests against real game archives, which are too large and not
// redistributable to keep in the repository. Set XDB_TEST_ARCHIVES to a
// semicolon-separated list of archive paths, e.g.
//   XDB_TEST_ARCHIVES=C:\Games\SoC\gamedata.dbd;C:\Games\CoP\patches\xpatch_02.db

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Common/MyCom.h"
#include "Common/StringConvert.h"
#include "7zip/Common/FileStreams.h"

#include "XdbIn.h"

#include "TestArchive.h"
#include "TestCallbacks.h"

using namespace NArchive;
using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

std::vector<std::string> GetArchivePaths()
{
  std::vector<std::string> paths;
  // Read as UTF-16 and kept as UTF-8: getenv() would give the ANSI code page and lose
  // characters such as the Cyrillic in some game folders.
  const wchar_t *list = _wgetenv(L"XDB_TEST_ARCHIVES");
  if (!list)
    return paths;
  std::string s = (const char *)UnicodeStringToMultiByte(UString(list), CP_UTF8);
  size_t start = 0;
  while (start <= s.size())
  {
    const size_t end = s.find(';', start);
    const std::string path = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!path.empty())
      paths.push_back(path);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return paths;
}

CMyComPtr<IInStream> OpenFile(const std::string &path)
{
  CInFileStream *spec = new CInFileStream;
  CMyComPtr<IInStream> stream = spec;
  if (!spec->Open(us2fs(MultiByteToUnicodeString(path.c_str(), CP_UTF8))))
    return nullptr;
  return stream;
}

class RealArchives : public testing::Test
{
protected:
  void SetUp() override
  {
    Paths = GetArchivePaths();
    if (Paths.empty())
      GTEST_SKIP() << "XDB_TEST_ARCHIVES is not set";
  }

  std::vector<std::string> Paths;
};

TEST_F(RealArchives, OpenWithoutErrors)
{
  for (const std::string &path : Paths)
  {
    SCOPED_TRACE(path);
    CMyComPtr<IInStream> stream = OpenFile(path);
    ASSERT_TRUE(stream) << "cannot open the file";
    CInArchive arc;
    ASSERT_EQ(arc.Open(stream, nullptr), S_OK);
    EXPECT_FALSE(arc.HeadersError);
    EXPECT_GT(arc.Items.Size(), 0u);
    unsigned numDirs = 0;
    FOR_VECTOR (i, arc.Items)
      if (arc.Items[i].IsDir)
        numDirs++;
    printf("  %-8s %6u items (%u dirs)  %s\n", GetVersionInfo(arc.Version).Name, arc.Items.Size(), numDirs, path.c_str());
  }
}

TEST_F(RealArchives, EveryFilePassesItsCrc)
{
  for (const std::string &path : Paths)
  {
    SCOPED_TRACE(path);
    CMyComPtr<IInArchive> handler = CreateHandler();
    ASSERT_EQ(handler->Open(OpenFile(path), nullptr, nullptr), S_OK);

    CExtractCallback *spec = new CExtractCallback;
    CMyComPtr<IArchiveExtractCallback> callback = spec;
    ASSERT_EQ(handler->Extract(nullptr, (UInt32)(Int32)-1, 1, callback), S_OK);

    UInt32 numErrors = 0;
    for (const auto &result : spec->Results.Results)
      if (result.second != NExtract::NOperationResult::kOK)
        numErrors++;
    EXPECT_EQ(numErrors, 0u);
    printf("  %6u files, %llu bytes checked  %s\n", (unsigned)spec->Results.Results.size(),
        (unsigned long long)spec->Results.Total, path.c_str());
  }
}

// Rewriting an archive without changes keeps every name, size and CRC, and
// adds only the folder records it lacked.
TEST_F(RealArchives, RewriteKeepsEverything)
{
  for (const std::string &path : Paths)
  {
    SCOPED_TRACE(path);
    CMyComPtr<IInStream> stream = OpenFile(path);
    CInArchive original;
    ASSERT_EQ(original.Open(stream, nullptr), S_OK);

    // Some archives contain duplicate names; keep them all, on open and on update.
    const CProps keepDuplicates = { { L"keepdups", NWindows::NCOM::CPropVariant(L"on") } };
    CMyComPtr<IInArchive> handler = CreateHandler();
    ASSERT_EQ(SetProps(handler, keepDuplicates), S_OK);
    ASSERT_EQ(handler->Open(OpenFile(path), nullptr, nullptr), S_OK);
    UInt32 numItems = 0;
    handler->GetNumberOfItems(&numItems);
    std::vector<CUpdateEntry> entries;
    for (UInt32 i = 0; i < numItems; i++)
      entries.push_back(Keep((Int32)i));
    CBytes bytes;
    ASSERT_EQ(RunUpdate(handler, entries, bytes, keepDuplicates), S_OK);

    CInArchive rewritten;
    ASSERT_EQ(rewritten.Open(MakeInStream(bytes), nullptr), S_OK);
    EXPECT_EQ(rewritten.Version, original.Version);
    EXPECT_EQ(rewritten.UserDataDefined, original.UserDataDefined);
    unsigned j = 0;
    for (unsigned i = 0; i < original.Items.Size(); i++, j++)
    {
      const CItem &b = original.Items[i];
      while (j < rewritten.Items.Size() && rewritten.Items[j].IsDir && rewritten.Items[j].Name != b.Name)
        j++;
      ASSERT_LT(j, rewritten.Items.Size()) << b.Name.Ptr();
      const CItem &a = rewritten.Items[j];
      EXPECT_STREQ(a.Name, b.Name);
      EXPECT_EQ(a.IsDir, b.IsDir);
      EXPECT_EQ(a.Size, b.Size);
      EXPECT_EQ(a.PackSize, b.PackSize);
      EXPECT_EQ(a.Crc, b.Crc);
    }
  }
}

} // namespace
