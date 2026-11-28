// The DLL entry points that aren't in ArchiveExports.cpp. They replace 7-Zip's DllExports2.cpp,
// which also exports codecs and hashers: the plugin only offers the xdb format.

#include "StdAfx.h"

#include "Common/MyWindows.h"
#include "Common/MyInitGuid.h"

#include "Windows/PropVariant.h"

#include "7zip/ICoder.h"   // NModulePropID, NModuleInterfaceType
#include "7zip/Archive/IArchive.h"

// The base of the formats' class IDs, which ArchiveExports.cpp completes with the format ID.
Z7_DEFINE_GUID(CLSID_CArchiveHandler,
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    0x10, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00);

STDAPI CreateArchiver(const GUID *clsid, const GUID *iid, void **outObject);

STDAPI CreateObject(const GUID *clsid, const GUID *iid, void **outObject);
STDAPI CreateObject(const GUID *clsid, const GUID *iid, void **outObject)
{
  return CreateArchiver(clsid, iid, outObject);
}

STDAPI SetLargePageMode();
STDAPI SetLargePageMode()
{
  return S_OK;
}

extern bool g_CaseSensitive;

STDAPI SetCaseSensitive(Int32 caseSensitive);
STDAPI SetCaseSensitive(Int32 caseSensitive)
{
  g_CaseSensitive = (caseSensitive != 0);
  return S_OK;
}

// 7-Zip loads the DLL only with this interface type; the version is shown by "7z i".
STDAPI GetModuleProp(PROPID propID, PROPVARIANT *value);
STDAPI GetModuleProp(PROPID propID, PROPVARIANT *value)
{
  ::VariantClear((VARIANTARG *)value);
  switch (propID)
  {
    case NModulePropID::kInterfaceType:
      NWindows::NCOM::PropVarEm_Set_UInt32(value, NModuleInterfaceType::k_IUnknown_VirtDestructor_ThisModule);
      break;
    case NModulePropID::kVersion:
      NWindows::NCOM::PropVarEm_Set_UInt32(value, ((UInt32)XDB_VERSION_MAJOR << 16) | XDB_VERSION_MINOR);
      break;
  }
  return S_OK;
}
