// XdbLzhuf.h

#ifndef ZIP7_INC_XDB_LZHUF_H
#define ZIP7_INC_XDB_LZHUF_H

#include "Common/MyBuffer.h"
#include "Common/MyTypes.h"

namespace NArchive {
namespace NXdb {

/*
  LZHUF (LZSS with an adaptive Huffman coder, Yoshizaki 1988), used by the
  X-Ray engine for archive headers and for file data in 11xx archives.
  A stream is the uncompressed size (UInt32, little-endian) followed by the
  bit stream. See docs/db-format.md.
*/
namespace NLzhuf {

const unsigned kSizePrefixSize = 4;

// Decoding never expands a stream by more than this factor: the cheapest code
// (a 60-byte match) still takes at least 10 bits. Larger stored sizes can only
// come from corrupt data.
const unsigned kMaxExpansion = 64;

void Encode(const Byte *data, size_t size, CByteBuffer &stream);

// Reads the uncompressed size from the start of a stream.
bool GetUnpackSize(const Byte *stream, size_t streamSize, UInt32 &unpackSize);

/*
  Decodes a complete stream into (dest), sized to the stored uncompressed size.
  Returns false if the stream is corrupt or if the stored size is larger than
  (maxUnpackSize) or than kMaxExpansion times the stream size.
*/
bool Decode(const Byte *stream, size_t streamSize, CByteBuffer &dest, size_t maxUnpackSize);

} // namespace NLzhuf

} // namespace NXdb
} // namespace NArchive

#endif
