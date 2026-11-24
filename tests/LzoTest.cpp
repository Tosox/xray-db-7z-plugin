// lzokay replaces miniLZO in the plugin. These tests check that the two
// produce and accept the same LZO1X streams.

#include <gtest/gtest.h>

#include "lzokay.hpp"

extern "C" {
#include "minilzo.h"
}

#include "TestData.h"

using namespace NXdbTest;

namespace {

CBytes LzokayCompress(const CBytes &data)
{
  CBytes stream(lzokay::compress_worst_size(data.size()));
  size_t streamSize = 0;
  EXPECT_EQ(lzokay::compress(data.data(), data.size(), stream.data(), stream.size(), streamSize),
      lzokay::EResult::Success);
  stream.resize(streamSize);
  return stream;
}

bool LzokayDecompress(const CBytes &stream, size_t size, CBytes &data)
{
  data.assign(size, 0);
  size_t outSize = 0;
  return lzokay::decompress(stream.data(), stream.size(), data.data(), data.size(), outSize)
      == lzokay::EResult::Success
      && outSize == size;
}

CBytes MiniLzoCompress(const CBytes &data)
{
  EXPECT_EQ(lzo_init(), LZO_E_OK);
  CBytes stream(data.size() + data.size() / 16 + 64 + 3);
  std::vector<Byte> workMemory(LZO1X_1_MEM_COMPRESS);
  lzo_uint streamSize = 0;
  EXPECT_EQ(lzo1x_1_compress(data.data(), (lzo_uint)data.size(), stream.data(), &streamSize, workMemory.data()),
      LZO_E_OK);
  stream.resize(streamSize);
  return stream;
}

bool MiniLzoDecompress(const CBytes &stream, size_t size, CBytes &data)
{
  EXPECT_EQ(lzo_init(), LZO_E_OK);
  data.assign(size, 0);
  lzo_uint outSize = (lzo_uint)size;
  return lzo1x_decompress_safe(stream.data(), (lzo_uint)stream.size(), data.data(), &outSize, nullptr) == LZO_E_OK
      && outSize == size;
}

std::vector<CBytes> MakeSamples()
{
  std::vector<CBytes> samples;
  for (size_t size : { 1, 16, 1000, 70000, 1 << 20 })
  {
    samples.push_back(MakeTextBytes(size, (unsigned)size));
    samples.push_back(MakeRandomBytes(size, (unsigned)size));
    samples.push_back(CBytes(size, 'a'));
  }
  return samples;
}

TEST(Lzo, LzokayRoundTrip)
{
  for (const CBytes &sample : MakeSamples())
  {
    CBytes decoded;
    ASSERT_TRUE(LzokayDecompress(LzokayCompress(sample), sample.size(), decoded)) << sample.size();
    EXPECT_EQ(decoded, sample);
  }
}

TEST(Lzo, MiniLzoDecodesLzokayStreams)
{
  for (const CBytes &sample : MakeSamples())
  {
    CBytes decoded;
    ASSERT_TRUE(MiniLzoDecompress(LzokayCompress(sample), sample.size(), decoded)) << sample.size();
    EXPECT_EQ(decoded, sample);
  }
}

TEST(Lzo, LzokayDecodesMiniLzoStreams)
{
  for (const CBytes &sample : MakeSamples())
  {
    CBytes decoded;
    ASSERT_TRUE(LzokayDecompress(MiniLzoCompress(sample), sample.size(), decoded)) << sample.size();
    EXPECT_EQ(decoded, sample);
  }
}

TEST(Lzo, LzokayRejectsWrongSize)
{
  const CBytes sample = MakeTextBytes(1000);
  CBytes decoded;
  EXPECT_FALSE(LzokayDecompress(LzokayCompress(sample), sample.size() - 1, decoded));
}

} // namespace
