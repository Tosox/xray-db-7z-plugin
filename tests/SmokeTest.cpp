// Checks that the build itself is sound: the CRC table is initialized and the
// plugin DLL exports what 7-Zip expects.

#include <windows.h>

#include <gtest/gtest.h>

#include "7zCrc.h"

namespace {

TEST(Smoke, CrcTableIsInitialized)
{
  // Standard CRC-32 check value.
  const char kInput[] = "123456789";
  EXPECT_EQ(CrcCalc(kInput, sizeof(kInput) - 1), 0xCBF43926u);
}

class PluginDll : public testing::Test
{
protected:
  void SetUp() override
  {
    _module = LoadLibraryA(XDB_PLUGIN_PATH);
    ASSERT_NE(_module, nullptr) << "cannot load " << XDB_PLUGIN_PATH;
  }

  void TearDown() override
  {
    if (_module)
      FreeLibrary(_module);
  }

  FARPROC Get(const char *name) const { return GetProcAddress(_module, name); }

  HMODULE _module = nullptr;
};

TEST_F(PluginDll, RegistersOneFormat)
{
  typedef HRESULT (WINAPI *Func_GetNumberOfFormats)(UInt32 *numFormats);
  const auto getNumberOfFormats = reinterpret_cast<Func_GetNumberOfFormats>(Get("GetNumberOfFormats"));
  ASSERT_NE(getNumberOfFormats, nullptr);

  UInt32 numFormats = 0;
  ASSERT_EQ(getNumberOfFormats(&numFormats), S_OK);
  EXPECT_EQ(numFormats, 1u);
}

TEST_F(PluginDll, ExportsArchiveEntryPoints)
{
  for (const char *name : { "CreateObject", "GetHandlerProperty2", "GetIsArc" })
    EXPECT_NE(Get(name), nullptr) << name;
}

} // namespace
