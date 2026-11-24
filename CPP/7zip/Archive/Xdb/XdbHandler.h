// XdbHandler.h

#ifndef ZIP7_INC_XDB_HANDLER_H
#define ZIP7_INC_XDB_HANDLER_H

#include "Common/MyBuffer.h"
#include "Common/MyCom.h"

#include "7zip/Archive/IArchive.h"
#include "7zip/Compress/CopyCoder.h"

#include "XdbIn.h"

namespace NArchive {
namespace NXdb {

enum ECompressMode
{
  kCompressMode_Auto,   // like the archive being updated; on for new archives
  kCompressMode_Always,
  kCompressMode_Never
};

// Settings from ISetProperties: see README.md and GetFormatOptions() in XdbRegister.cpp.
struct COptions
{
  bool VersionDefined;       // "dbver"; otherwise the opened archive's version, or xdb
  EVersion Version;
  ECompressMode CompressMode; // "compress"
  bool KeepDuplicates;       // "keepdups"
  bool GenerateUserData;     // "gen_xdb_ud"
  bool UserDataDefined;      // "xdb_ud": contents of the given file
  CByteBuffer UserData;

  COptions() { Init(); }
  void Init();
};

// The userdata block of an xdb archive is shown as a file with this name.
extern const char * const kUserDataItemName;

Z7_CLASS_IMP_CHandler_IInArchive_2(
    ISetProperties,
    IOutArchive
)
  CMyComPtr<IInStream> _stream;
  CInArchive _arc;
  bool _isOpen;
  bool _showUserDataItem;
  COptions _options;
  CMyComPtr2_Create<ICompressCoder, NCompress::CCopyCoder> _copyCoder;

  bool IsUserDataItem(UInt32 index) const { return _showUserDataItem && index == _arc.Items.Size(); }
  HRESULT ExtractItem(const CItem &item, ISequentialOutStream *outStream, ICompressProgressInfo *progress,
      Int32 &opRes);

public:
  CHandler(): _isOpen(false), _showUserDataItem(false) {}
};

} // namespace NXdb
} // namespace NArchive

#endif
