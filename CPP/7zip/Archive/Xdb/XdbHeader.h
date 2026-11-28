// XdbHeader.h
// The file table stored in the HEADER chunk, after descrambling and decompression.

#ifndef ZIP7_INC_XDB_HEADER_H
#define ZIP7_INC_XDB_HEADER_H

#include "Common/MyBuffer.h"
#include "Common/MyVector.h"

#include "XdbFormat.h"

namespace NArchive {
namespace NXdb {

/*
  Parses a file table. Fails unless the data is exactly a sequence of
  well-formed records: names are non-empty and free of control characters,
  only directories have offset 0 (and then all sizes are 0), and the 11xx
  "stored" field is 0 or 1. These rules are what make layouts that look
  alike reject each other. File offsets are checked by AreItemsInRange().
*/
bool ParseHeader(EVersion version, const Byte *data, size_t size, CObjectVector<CItem> &items);

// Returns false if a name is too long for the 2947 layout.
bool WriteHeader(EVersion version, const CObjectVector<CItem> &items, CByteBuffer &header);

// True if the data of every file lies within [dataBegin, dataEnd).
bool AreItemsInRange(const CObjectVector<CItem> &items, UInt64 dataBegin, UInt64 dataEnd);

} // namespace NXdb
} // namespace NArchive

#endif
