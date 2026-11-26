// XdbHeader.cpp

#include "StdAfx.h"

#include <string.h>

#include "CpuArch.h"

#include "XdbHeader.h"

namespace NArchive {
namespace NXdb {

// In the 2947 layout, a record starts with its size: the name length plus 16.
static const unsigned kRecordSizeBias_2947 = 16;

namespace {

class CRecordReader
{
  const Byte *_cur;
  const Byte *_end;

  size_t Remaining() const { return (size_t)(_end - _cur); }

public:
  CRecordReader(const Byte *data, size_t size): _cur(data), _end(data + size) {}

  bool IsEnd() const { return _cur == _end; }

  bool ReadUInt16(UInt32 &value)
  {
    if (Remaining() < 2)
      return false;
    value = GetUi16(_cur);
    _cur += 2;
    return true;
  }

  bool ReadUInt32(UInt32 &value)
  {
    if (Remaining() < 4)
      return false;
    value = GetUi32(_cur);
    _cur += 4;
    return true;
  }

  bool ReadString(size_t length, AString &s)
  {
    if (Remaining() < length)
      return false;
    s.SetFrom_Chars_SizeT((const char *)_cur, length);
    _cur += length;
    return true;
  }

  bool ReadZeroTerminatedString(AString &s)
  {
    const Byte *zero = (const Byte *)memchr(_cur, 0, Remaining());
    if (!zero)
      return false;
    s.SetFrom_Chars_SizeT((const char *)_cur, (size_t)(zero - _cur));
    _cur = zero + 1;
    return true;
  }
};

class CRecordWriter
{
  Byte *_cur;

public:
  CRecordWriter(Byte *data): _cur(data) {}

  void WriteUInt16(UInt32 value)
  {
    SetUi16(_cur, (UInt16)value)
    _cur += 2;
  }

  void WriteUInt32(UInt32 value)
  {
    SetUi32(_cur, value)
    _cur += 4;
  }

  void WriteString(const AString &s)
  {
    memcpy(_cur, s.Ptr(), s.Len());
    _cur += s.Len();
  }

  void WriteZeroTerminatedString(const AString &s)
  {
    WriteString(s);
    *_cur++ = 0;
  }
};

} // namespace


static bool ReadRecord(ERecordLayout layout, CRecordReader &reader, CItem &item)
{
  switch (layout)
  {
    case kLayout_11xx:
    {
      UInt32 stored;
      if (!reader.ReadZeroTerminatedString(item.Name)
          || !reader.ReadUInt32(stored)
          || !reader.ReadUInt32(item.Offset)
          || !reader.ReadUInt32(item.PackSize))
        return false;
      if (stored > 1)
        return false;
      // The size of a compressed file is the first field of its LZHUF stream;
      // CInArchive fills it in.
      item.Size = stored ? item.PackSize : 0;
      item.Method = stored ? kMethod_Store : kMethod_Lzhuf;
      return true;
    }

    case kLayout_2215:
      return reader.ReadZeroTerminatedString(item.Name)
          && reader.ReadUInt32(item.Offset)
          && reader.ReadUInt32(item.Size)
          && reader.ReadUInt32(item.PackSize);

    case kLayout_2945:
      return reader.ReadZeroTerminatedString(item.Name)
          && reader.ReadUInt32(item.Crc)
          && reader.ReadUInt32(item.Offset)
          && reader.ReadUInt32(item.Size)
          && reader.ReadUInt32(item.PackSize);

    case kLayout_2947:
    {
      UInt32 recordSize;
      return reader.ReadUInt16(recordSize)
          && recordSize >= kRecordSizeBias_2947
          && reader.ReadUInt32(item.Size)
          && reader.ReadUInt32(item.PackSize)
          && reader.ReadUInt32(item.Crc)
          && reader.ReadString(recordSize - kRecordSizeBias_2947, item.Name)
          && reader.ReadUInt32(item.Offset);
    }
  }
  return false;
}

static bool HasControlChars(const AString &s)
{
  for (unsigned i = 0; i < s.Len(); i++)
    if ((Byte)s[i] < 0x20)
      return true;
  return false;
}

// Applies the rules shared by all layouts and derives the item's kind.
static bool FinishItem(const CVersionInfo &info, CItem &item)
{
  while (!item.Name.IsEmpty() && item.Name.Back() == '\\')
    item.Name.DeleteBack();
  if (item.Name.IsEmpty() || HasControlChars(item.Name))
    return false;

  if (item.Offset == 0)
  {
    if (item.Size != 0 || item.PackSize != 0)
      return false;
    item.IsDir = true;
    item.Method = kMethod_Store;
    return true;
  }

  if (info.Layout != kLayout_11xx)
    item.Method = (item.Size != item.PackSize) ? info.FileMethod : kMethod_Store;
  item.CrcDefined = info.HasCrc;
  return true;
}

bool ParseHeader(EVersion version, const Byte *data, size_t size, CObjectVector<CItem> &items)
{
  const CVersionInfo &info = GetVersionInfo(version);
  items.Clear();
  CRecordReader reader(data, size);
  while (!reader.IsEnd())
  {
    CItem &item = items.AddNew();
    if (!ReadRecord(info.Layout, reader, item) || !FinishItem(info, item))
    {
      items.Clear();
      return false;
    }
  }
  return true;
}


static AString GetStoredName(const CItem &item)
{
  AString name = item.Name;
  if (item.IsDir)
    name.Add_Char('\\');
  return name;
}

static size_t GetRecordSize(ERecordLayout layout, const AString &name)
{
  switch (layout)
  {
    case kLayout_11xx:
    case kLayout_2215: return name.Len() + 1 + 3 * 4;
    case kLayout_2945: return name.Len() + 1 + 4 * 4;
    case kLayout_2947: break;
  }
  return 2 + 3 * 4 + name.Len() + 4;
}

// 11xx has no directory records: the reference unpacker has no notion of
// them and crashes on one, so they are left out.
static bool IsWritten(ERecordLayout layout, const CItem &item)
{
  return !(item.IsDir && layout == kLayout_11xx);
}

bool WriteHeader(EVersion version, const CObjectVector<CItem> &items, CByteBuffer &header)
{
  const ERecordLayout layout = GetVersionInfo(version).Layout;

  size_t totalSize = 0;
  FOR_VECTOR (i, items)
  {
    if (!IsWritten(layout, items[i]))
      continue;
    const AString name = GetStoredName(items[i]);
    if (layout == kLayout_2947 && name.Len() > 0xFFFF - kRecordSizeBias_2947)
      return false;
    totalSize += GetRecordSize(layout, name);
  }

  header.Alloc(totalSize);
  CRecordWriter writer(header);

  FOR_VECTOR (i, items)
  {
    const CItem &item = items[i];
    if (!IsWritten(layout, item))
      continue;
    const AString name = GetStoredName(item);
    // Directories are written with offset 0 and all other fields 0.
    const UInt32 offset = item.IsDir ? 0 : item.Offset;
    const UInt32 size = item.IsDir ? 0 : item.Size;
    const UInt32 packSize = item.IsDir ? 0 : item.PackSize;
    const UInt32 crc = item.IsDir ? 0 : item.Crc;

    switch (layout)
    {
      case kLayout_11xx:
        writer.WriteZeroTerminatedString(name);
        writer.WriteUInt32(item.Method == kMethod_Lzhuf ? 0 : 1); // "stored"
        writer.WriteUInt32(offset);
        writer.WriteUInt32(packSize);
        break;

      case kLayout_2215:
        writer.WriteZeroTerminatedString(name);
        writer.WriteUInt32(offset);
        writer.WriteUInt32(size);
        writer.WriteUInt32(packSize);
        break;

      case kLayout_2945:
        writer.WriteZeroTerminatedString(name);
        writer.WriteUInt32(crc);
        writer.WriteUInt32(offset);
        writer.WriteUInt32(size);
        writer.WriteUInt32(packSize);
        break;

      case kLayout_2947:
        writer.WriteUInt16(name.Len() + kRecordSizeBias_2947);
        writer.WriteUInt32(size);
        writer.WriteUInt32(packSize);
        writer.WriteUInt32(crc);
        writer.WriteString(name);
        writer.WriteUInt32(offset);
        break;
    }
  }
  return true;
}

bool AreItemsInRange(const CObjectVector<CItem> &items, UInt64 dataBegin, UInt64 dataEnd)
{
  FOR_VECTOR (i, items)
  {
    const CItem &item = items[i];
    if (item.IsDir)
      continue;
    if (item.Offset < dataBegin || (UInt64)item.Offset + item.PackSize > dataEnd)
      return false;
  }
  return true;
}

} // namespace NXdb
} // namespace NArchive
