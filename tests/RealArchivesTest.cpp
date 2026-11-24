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

using namespace NArchive::NXdb;

namespace {

std::vector<std::string> GetArchivePaths()
{
  std::vector<std::string> paths;
  const char *list = getenv("XDB_TEST_ARCHIVES");
  if (!list)
    return paths;
  std::string s = list;
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
    ASSERT_EQ(arc.Open(stream, nullptr, nullptr), S_OK);
    EXPECT_FALSE(arc.HeadersError);
    EXPECT_GT(arc.Items.Size(), 0u);
    unsigned numDirs = 0;
    FOR_VECTOR (i, arc.Items)
      if (arc.Items[i].IsDir)
        numDirs++;
    printf("  %-8s %6u items (%u dirs)  %s\n", GetVersionInfo(arc.Version).Name, arc.Items.Size(), numDirs, path.c_str());
  }
}

} // namespace
