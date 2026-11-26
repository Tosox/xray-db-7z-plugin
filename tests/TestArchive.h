// Builds DB archives for tests directly from docs/db-format.md, independently
// of the plugin's own writer.

#ifndef XDB_TESTS_TEST_ARCHIVE_H
#define XDB_TESTS_TEST_ARCHIVE_H

#include <string>
#include <vector>

#include "Common/MyCom.h"
#include "7zip/Common/StreamObjects.h"
#include "7zCrc.h"

#include "lzokay.hpp"

#include "XdbFormat.h"
#include "XdbHeader.h"
#include "XdbLzhuf.h"
#include "XdbScrambler.h"

#include "TestData.h"

namespace NXdbTest {

using namespace NArchive::NXdb;

struct CTestFile
{
  std::string Name; // stored form: '\' separators
  CBytes Data;
  bool IsDir = false;
  bool Compress = false;
};

inline CTestFile TestDir(const std::string &name)
{
  CTestFile file;
  file.Name = name;
  file.IsDir = true;
  return file;
}

inline CTestFile TestFile(const std::string &name, const CBytes &data, bool compress)
{
  CTestFile file;
  file.Name = name;
  file.Data = data;
  file.Compress = compress;
  return file;
}

inline std::vector<CTestFile> MakeSampleFiles()
{
  return {
    TestDir("config"),
    TestFile("config\\system.ltx", MakeTextBytes(5000, 1), true),
    TestFile("config\\random.bin", MakeRandomBytes(3000, 2), false),
    TestFile("config\\empty.ltx", CBytes(), false),
    TestDir("scripts"),
    TestFile("scripts\\bind_stalker.script", MakeTextBytes(20000, 3), true),
  };
}

// What an archive of (version) holds after writing (files): 11xx keeps no directories.
inline std::vector<CTestFile> ExpectedFiles(EVersion version, const std::vector<CTestFile> &files)
{
  std::vector<CTestFile> result;
  for (const CTestFile &file : files)
    if (!(file.IsDir && version == kVersion_11xx))
      result.push_back(file);
  return result;
}

struct CArchiveOptions
{
  bool CompressHeader = true;
  bool HeaderFirst = false;
  const CBytes *UserData = nullptr;
};

inline void AppendChunk(CBytes &archive, UInt32 id, const CBytes &payload)
{
  for (int i = 0; i < 4; i++)
    archive.push_back((Byte)(id >> (8 * i)));
  for (int i = 0; i < 4; i++)
    archive.push_back((Byte)(payload.size() >> (8 * i)));
  archive.insert(archive.end(), payload.begin(), payload.end());
}

inline CBytes PackFile(EMethod method, const CBytes &data)
{
  if (method == kMethod_Lzhuf)
  {
    CByteBuffer stream;
    NLzhuf::Encode(data.data(), data.size(), stream);
    return ToBytes(stream);
  }
  CBytes packed(lzokay::compress_worst_size(data.size()));
  size_t packedSize = 0;
  lzokay::compress(data.data(), data.size(), packed.data(), packed.size(), packedSize);
  packed.resize(packedSize);
  return packed;
}

class CArchiveBuilder
{
  EVersion _version;
  CArchiveOptions _options;
  CObjectVector<CItem> _items; // offsets relative to the data chunk payload
  CBytes _data;

public:
  CArchiveBuilder(EVersion version, const std::vector<CTestFile> &files, const CArchiveOptions &options):
      _version(version),
      _options(options)
  {
    const CVersionInfo &info = GetVersionInfo(version);
    for (const CTestFile &file : files)
    {
      CItem &item = _items.AddNew();
      item.Name = file.Name.c_str();
      item.IsDir = file.IsDir;
      if (file.IsDir)
        continue;
      CBytes packed = file.Data;
      if (file.Compress && !file.Data.empty())
      {
        packed = PackFile(info.FileMethod, file.Data);
        item.Method = info.FileMethod;
      }
      item.Offset = (UInt32)_data.size();
      item.Size = (UInt32)file.Data.size();
      item.PackSize = (UInt32)packed.size();
      item.Crc = info.HasCrc ? CrcCalc(file.Data.data(), file.Data.size()) : 0;
      _data.insert(_data.end(), packed.begin(), packed.end());
    }
  }

  CBytes EncodeHeader(size_t dataStart) const
  {
    CObjectVector<CItem> items = _items;
    for (unsigned i = 0; i < items.Size(); i++)
      if (!items[i].IsDir)
        items[i].Offset += (UInt32)dataStart;
    CByteBuffer table;
    WriteHeader(_version, items, table);
    if (!_options.CompressHeader)
      return ToBytes(table);
    CByteBuffer stream;
    NLzhuf::Encode(table, table.Size(), stream);
    CBytes header = ToBytes(stream);
    const CVersionInfo &info = GetVersionInfo(_version);
    if (info.IsScrambled)
      CScrambler(info.ScramblerKey).Encrypt(header.data(), header.size());
    return header;
  }

  CBytes Build() const
  {
    CBytes archive;
    if (_options.UserData)
      AppendChunk(archive, NChunkId::kUserData, *_options.UserData);

    const UInt32 headerId = NChunkId::kHeader | (_options.CompressHeader ? NChunkId::kCompressedFlag : 0);
    if (!_options.HeaderFirst)
    {
      const CBytes header = EncodeHeader(archive.size() + kChunkHeaderSize);
      AppendChunk(archive, NChunkId::kData, _data);
      AppendChunk(archive, headerId, header);
      return archive;
    }

    // The data offsets depend on the size of the header in front of them,
    // and the compressed size depends on the offsets: iterate until stable.
    CBytes header;
    size_t headerSize = 0;
    for (;;)
    {
      header = EncodeHeader(archive.size() + kChunkHeaderSize + headerSize + kChunkHeaderSize);
      if (header.size() == headerSize)
        break;
      headerSize = header.size();
    }
    AppendChunk(archive, headerId, header);
    AppendChunk(archive, NChunkId::kData, _data);
    return archive;
  }
};

inline CBytes BuildArchive(EVersion version, const std::vector<CTestFile> &files, const CArchiveOptions &options = {})
{
  return CArchiveBuilder(version, files, options).Build();
}

// An IInStream over a byte vector that must outlive it.
inline CMyComPtr<IInStream> MakeInStream(const CBytes &bytes)
{
  CBufInStream *spec = new CBufInStream;
  CMyComPtr<IInStream> stream = spec;
  spec->Init(bytes.data(), bytes.size());
  return stream;
}

} // namespace NXdbTest

#endif
