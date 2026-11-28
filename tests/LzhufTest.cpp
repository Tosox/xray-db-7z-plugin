#include <stdlib.h>

#include <new>

#include <gtest/gtest.h>

#include "xr_lzhuf.h"

#include "XdbLzhuf.h"

#include "TestData.h"

using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

// The reference coder keeps its state between calls, and its encoder compares
// ring bytes it never wrote. Each call therefore gets a fresh, zeroed instance.
xray_re::_lzhuf *NewReferenceCoder()
{
  void *memory = calloc(1, sizeof(xray_re::_lzhuf));
  return new (memory) xray_re::_lzhuf;
}

void DeleteReferenceCoder(xray_re::_lzhuf *coder)
{
  coder->~_lzhuf();
  free(coder);
}

CBytes ReferenceEncode(const CBytes &data)
{
  xray_re::_lzhuf *coder = NewReferenceCoder();
  uint8_t *stream = nullptr;
  size_t streamSize = 0;
  coder->Encode(stream, streamSize, data.data(), data.size());
  DeleteReferenceCoder(coder);
  CBytes result(stream, stream + streamSize);
  free(stream);
  return result;
}

CBytes ReferenceDecode(const CBytes &stream)
{
  xray_re::_lzhuf *coder = NewReferenceCoder();
  uint8_t *data = nullptr;
  size_t size = 0;
  coder->Decode(data, size, stream.data(), stream.size());
  DeleteReferenceCoder(coder);
  CBytes result(data, data + size);
  free(data);
  return result;
}

CBytes Encode(const CBytes &data)
{
  CByteBuffer stream;
  NLzhuf::Encode(data.data(), data.size(), stream);
  return ToBytes(stream);
}

bool Decode(const CBytes &stream, CBytes &data, size_t maxUnpackSize = 1 << 30)
{
  CByteBuffer buffer;
  const bool ok = NLzhuf::Decode(stream.data(), stream.size(), buffer, maxUnpackSize);
  data = ToBytes(buffer);
  return ok;
}

struct CSample
{
  const char *Name;
  CBytes Data;
};

std::vector<CSample> MakeSamples()
{
  std::vector<CSample> samples;
  for (size_t size : { 0, 1, 2, 3, 7, 8, 59, 60, 61, 100, 4095, 4096, 4097, 100000 })
  {
    samples.push_back({ "zeros", CBytes(size, 0) });
    samples.push_back({ "text", MakeTextBytes(size, (unsigned)size) });
    samples.push_back({ "random", MakeRandomBytes(size, (unsigned)size) });
  }
  // Large enough for the Huffman tree to be rebuilt many times.
  samples.push_back({ "text 1 MiB", MakeTextBytes(1 << 20) });
  return samples;
}

TEST(Lzhuf, RoundTrip)
{
  for (const CSample &sample : MakeSamples())
  {
    CBytes decoded;
    ASSERT_TRUE(Decode(Encode(sample.Data), decoded)) << sample.Name << ", " << sample.Data.size();
    EXPECT_EQ(decoded, sample.Data) << sample.Name << ", " << sample.Data.size();
  }
}

TEST(Lzhuf, EncoderMatchesReference)
{
  for (const CSample &sample : MakeSamples())
  {
    // The reference encoder overflows its buffer for inputs shorter than 8 bytes.
    if (sample.Data.size() < 8)
      continue;
    EXPECT_EQ(Encode(sample.Data), ReferenceEncode(sample.Data)) << sample.Name << ", " << sample.Data.size();
  }
}

TEST(Lzhuf, ReferenceDecodesOurStreams)
{
  for (const CSample &sample : MakeSamples())
  {
    if (sample.Data.empty())
      continue;
    EXPECT_EQ(ReferenceDecode(Encode(sample.Data)), sample.Data) << sample.Name << ", " << sample.Data.size();
  }
}

TEST(Lzhuf, DecodesReferenceStreams)
{
  for (const CSample &sample : MakeSamples())
  {
    if (sample.Data.size() < 8)
      continue;
    CBytes decoded;
    ASSERT_TRUE(Decode(ReferenceEncode(sample.Data), decoded)) << sample.Name << ", " << sample.Data.size();
    EXPECT_EQ(decoded, sample.Data) << sample.Name << ", " << sample.Data.size();
  }
}

TEST(Lzhuf, EmptyInputIsJustTheSizePrefix)
{
  EXPECT_EQ(Encode(CBytes()), CBytes(4, 0));
}

TEST(Lzhuf, RejectsStreamShorterThanSizePrefix)
{
  CBytes decoded;
  EXPECT_FALSE(Decode(CBytes{ 1, 0, 0 }, decoded));
}

TEST(Lzhuf, RejectsTruncatedStream)
{
  CBytes stream = Encode(MakeTextBytes(10000));
  stream.resize(stream.size() / 2);
  CBytes decoded;
  EXPECT_FALSE(Decode(stream, decoded));
}

TEST(Lzhuf, RejectsImplausibleUnpackSize)
{
  // Claims 1 GiB from 16 bytes of input.
  CBytes stream(16, 0);
  stream[3] = 0x40;
  CBytes decoded;
  EXPECT_FALSE(Decode(stream, decoded));
}

TEST(Lzhuf, RespectsMaxUnpackSize)
{
  const CBytes stream = Encode(MakeTextBytes(1000));
  CBytes decoded;
  EXPECT_FALSE(Decode(stream, decoded, 999));
  EXPECT_TRUE(Decode(stream, decoded, 1000));
}

TEST(Lzhuf, SurvivesGarbage)
{
  for (unsigned seed = 0; seed < 2000; seed++)
  {
    CBytes stream = MakeRandomBytes(64 + seed % 512, seed);
    stream[2] = stream[3] = 0; // keep the stored size small
    CBytes decoded;
    Decode(stream, decoded); // must not crash or hang; the result doesn't matter
  }
}

} // namespace
