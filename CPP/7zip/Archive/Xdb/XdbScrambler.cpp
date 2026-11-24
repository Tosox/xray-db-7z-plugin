// XdbScrambler.cpp

#include "StdAfx.h"

#include "XdbScrambler.h"

namespace NArchive {
namespace NXdb {

struct CScramblerKeyParams
{
  UInt32 StreamSeed;
  UInt32 TableSeed;
  unsigned TableShuffleRounds; // times 256 swaps
};

static const CScramblerKeyParams kScramblerKeys[] =
{
  { 0x0131A9D3, 0x01329436, 8 }, // kScramblerKey_RU
  { 0x016EB2EB, 0x005BBC4B, 4 }  // kScramblerKey_WW
};

static inline UInt32 NextSeed(UInt32 seed)
{
  return 1 + seed * 0x08088405;
}

static inline Byte SeedToKeyByte(UInt32 seed)
{
  return (Byte)(seed >> 24);
}

CScrambler::CScrambler(EScramblerKey key)
{
  const CScramblerKeyParams &params = kScramblerKeys[key];
  _seed = params.StreamSeed;

  for (unsigned i = 0; i < 256; i++)
    _encryptTable[i] = (Byte)i;

  UInt32 seed = params.TableSeed;
  for (unsigned n = params.TableShuffleRounds * 256; n != 0; n--)
  {
    seed = NextSeed(seed);
    const Byte a = SeedToKeyByte(seed);
    Byte b;
    do
    {
      seed = NextSeed(seed);
      b = SeedToKeyByte(seed);
    }
    while (a == b);

    const Byte temp = _encryptTable[a];
    _encryptTable[a] = _encryptTable[b];
    _encryptTable[b] = temp;
  }

  for (unsigned i = 0; i < 256; i++)
    _decryptTable[_encryptTable[i]] = (Byte)i;
}

void CScrambler::Encrypt(Byte *data, size_t size) const
{
  UInt32 seed = _seed;
  for (size_t i = 0; i < size; i++)
  {
    seed = NextSeed(seed);
    data[i] = (Byte)(_encryptTable[data[i]] ^ SeedToKeyByte(seed));
  }
}

void CScrambler::Decrypt(Byte *data, size_t size) const
{
  UInt32 seed = _seed;
  for (size_t i = 0; i < size; i++)
  {
    seed = NextSeed(seed);
    data[i] = _decryptTable[(Byte)(data[i] ^ SeedToKeyByte(seed))];
  }
}

} // namespace NXdb
} // namespace NArchive
