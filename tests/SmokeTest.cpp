// Checks that the build itself is sound: the CRC table is initialized and the
// plugin DLL exports what 7-Zip expects.

#include <windows.h>

#include <string>

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

// Read by the Tosox/7zip fork; the name must be undecorated, also on Win32.
TEST_F(PluginDll, ExportsFormatOptions)
{
  typedef HRESULT (WINAPI *Func_GetFormatOptions)(UInt32 formatIndex, PROPVARIANT *value);
  const auto getFormatOptions = reinterpret_cast<Func_GetFormatOptions>(Get("GetFormatOptions"));
  ASSERT_NE(getFormatOptions, nullptr);

  PROPVARIANT value;
  value.vt = VT_EMPTY;
  ASSERT_EQ(getFormatOptions(0, &value), S_OK);
  ASSERT_EQ(value.vt, VT_BSTR);
  const std::wstring schema = value.bstrVal;
  SysFreeString(value.bstrVal);
  EXPECT_EQ(schema.rfind(L"version=1\n", 0), 0u);
  for (const wchar_t *option : { L"[dbver]", L"[compress]", L"[xdb_ud]", L"[gen_xdb_ud]" })
    EXPECT_NE(schema.find(option), std::wstring::npos) << option;
}

} // namespace
