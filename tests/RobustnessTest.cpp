// Opens and extracts thousands of randomly damaged archives. Nothing is
// checked except that the handler neither crashes nor hangs: the reader and
// the decoders must cope with any input.

#include <random>

#include <gtest/gtest.h>

#include "TestArchive.h"
#include "TestCallbacks.h"

using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

const unsigned kMutationsPerVersion = 2000;

void Damage(CBytes &bytes, std::mt19937 &random)
{
  switch (random() % 4)
  {
    case 0: // flip a few bytes anywhere
    case 1:
      for (unsigned n = 1 + random() % 8; n != 0; n--)
        bytes[random() % bytes.size()] ^= (Byte)(1 + random() % 255);
      break;
    case 2: // damage the header chunk at the end, where the file table is
      for (unsigned n = 1 + random() % 4; n != 0; n--)
        bytes[bytes.size() - 1 - random() % std::min<size_t>(bytes.size(), 64)] ^= (Byte)(1 + random() % 255);
      break;
    case 3: // truncate
      bytes.resize(random() % bytes.size());
      break;
  }
}

TEST(Robustness, DamagedArchivesNeverCrash)
{
  const EVersion versions[] =
  {
    kVersion_11xx, kVersion_2215, kVersion_2945, kVersion_2947ru, kVersion_2947ww, kVersion_Xdb
  };
  std::mt19937 random(12345);
  for (EVersion version : versions)
  {
    SCOPED_TRACE(GetVersionInfo(version).Name);
    const CBytes original = BuildArchive(version, MakeSampleFiles());
    for (unsigned i = 0; i < kMutationsPerVersion; i++)
    {
      CBytes bytes = original;
      Damage(bytes, random);

      CMyComPtr<IInArchive> handler = CreateHandler();
      if (handler->Open(MakeInStream(bytes), nullptr, nullptr) != S_OK)
        continue;
      CExtractCallback *spec = new CExtractCallback;
      CMyComPtr<IArchiveExtractCallback> callback = spec;
      EXPECT_EQ(handler->Extract(nullptr, (UInt32)(Int32)-1, 1, callback), S_OK);
    }
  }
}

} // namespace
