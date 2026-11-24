// Deterministic sample data for tests.

#ifndef XDB_TESTS_TEST_DATA_H
#define XDB_TESTS_TEST_DATA_H

#include <random>
#include <string>
#include <vector>

#include "Common/MyBuffer.h"
#include "Common/MyTypes.h"

namespace NXdbTest {

typedef std::vector<Byte> CBytes;

// Incompressible bytes.
inline CBytes MakeRandomBytes(size_t size, unsigned seed = 1)
{
  std::mt19937 random(seed);
  CBytes bytes(size);
  for (Byte &b : bytes)
    b = (Byte)random();
  return bytes;
}

// Something that looks like a config file: compresses well, but not trivially.
inline CBytes MakeTextBytes(size_t size, unsigned seed = 1)
{
  static const char *const kWords[] =
  {
    "[section]", "inv_weight", "=", "0.35", "cost", "1200", "\r\n", "; comment",
    "visual", "dynamics\\weapons\\wpn_ak74.ogf", "slot", "2", "  ", "true", "false"
  };
  std::mt19937 random(seed);
  CBytes bytes;
  bytes.reserve(size);
  while (bytes.size() < size)
  {
    const char *word = kWords[random() % (sizeof(kWords) / sizeof(kWords[0]))];
    for (; *word != 0 && bytes.size() < size; word++)
      bytes.push_back((Byte)*word);
    if (bytes.size() < size)
      bytes.push_back(' ');
  }
  return bytes;
}

inline CBytes ToBytes(const CByteBuffer &buffer)
{
  const Byte *data = buffer;
  return CBytes(data, data + buffer.Size());
}

} // namespace NXdbTest

#endif
