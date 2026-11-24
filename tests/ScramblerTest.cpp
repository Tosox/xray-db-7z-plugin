#include <gtest/gtest.h>

#include "xr_scrambler.h"

#include "XdbScrambler.h"

#include "TestData.h"

using namespace NArchive::NXdb;
using namespace NXdbTest;

namespace {

struct CKeyPair
{
  EScramblerKey Key;
  xray_re::xr_scrambler::cipher_config Reference;
};

const CKeyPair kKeys[] =
{
  { kScramblerKey_RU, xray_re::xr_scrambler::CC_RU },
  { kScramblerKey_WW, xray_re::xr_scrambler::CC_WW }
};

TEST(Scrambler, EncryptMatchesReference)
{
  const CBytes plain = MakeRandomBytes(10000);
  for (const CKeyPair &key : kKeys)
  {
    CBytes expected(plain.size());
    xray_re::xr_scrambler(key.Reference).encrypt(expected.data(), plain.data(), plain.size());

    CBytes actual = plain;
    CScrambler(key.Key).Encrypt(actual.data(), actual.size());
    EXPECT_EQ(actual, expected) << "key " << key.Key;
  }
}

TEST(Scrambler, DecryptMatchesReference)
{
  const CBytes encrypted = MakeRandomBytes(10000, 2);
  for (const CKeyPair &key : kKeys)
  {
    CBytes expected(encrypted.size());
    xray_re::xr_scrambler(key.Reference).decrypt(expected.data(), encrypted.data(), encrypted.size());

    CBytes actual = encrypted;
    CScrambler(key.Key).Decrypt(actual.data(), actual.size());
    EXPECT_EQ(actual, expected) << "key " << key.Key;
  }
}

TEST(Scrambler, DecryptReversesEncrypt)
{
  const CBytes plain = MakeTextBytes(5000);
  for (const CKeyPair &key : kKeys)
  {
    const CScrambler scrambler(key.Key);
    CBytes data = plain;
    scrambler.Encrypt(data.data(), data.size());
    EXPECT_NE(data, plain);
    scrambler.Decrypt(data.data(), data.size());
    EXPECT_EQ(data, plain);
  }
}

TEST(Scrambler, KeysDiffer)
{
  CBytes ru = MakeTextBytes(100);
  CBytes ww = ru;
  CScrambler(kScramblerKey_RU).Encrypt(ru.data(), ru.size());
  CScrambler(kScramblerKey_WW).Encrypt(ww.data(), ww.size());
  EXPECT_NE(ru, ww);
}

} // namespace
