// XdbScrambler.h

#ifndef ZIP7_INC_XDB_SCRAMBLER_H
#define ZIP7_INC_XDB_SCRAMBLER_H

#include "Common/MyTypes.h"

namespace NArchive {
namespace NXdb {

enum EScramblerKey
{
  kScramblerKey_RU,
  kScramblerKey_WW
};

/*
  Byte substitution cipher with an LCG key stream. 2947ru and 2947ww archives
  apply it to the (LZHUF-compressed) header chunk. See docs/db-format.md.
*/
class CScrambler
{
  UInt32 _seed;
  Byte _encryptTable[256];
  Byte _decryptTable[256];

public:
  explicit CScrambler(EScramblerKey key);

  // Both work in place.
  void Encrypt(Byte *data, size_t size) const;
  void Decrypt(Byte *data, size_t size) const;
};

} // namespace NXdb
} // namespace NArchive

#endif
