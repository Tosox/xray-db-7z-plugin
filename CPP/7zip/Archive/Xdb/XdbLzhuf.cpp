// XdbLzhuf.cpp
//
// Port of LZHUF.C (c) 1989 by Haruyasu Yoshizaki, Haruhiko Okumura and Kenji
// Rikitake ("Permission granted for non-commercial use"), as used by the X-Ray
// engine and xray_re-tools. The algorithm and its tables are unchanged, so the
// encoder produces the same bytes as the original; the decoder adds the bounds
// checks needed for untrusted archives.

#include "StdAfx.h"

#include <string.h>

#include "CpuArch.h"

#include "XdbLzhuf.h"

namespace NArchive {
namespace NXdb {
namespace NLzhuf {

static const unsigned kRingSize = 4096;  // window of previously seen bytes (N)
static const unsigned kRingMask = kRingSize - 1;
static const unsigned kMaxMatch = 60;    // longest match (F)
static const unsigned kThreshold = 2;    // shorter matches are sent as literals
static const unsigned kMaxFreq = 0x4000; // the tree is rebuilt when the root reaches this

// Symbols are the 256 literals followed by the match lengths (N_CHAR).
static const unsigned kNumSymbols = 256 - kThreshold + kMaxMatch;
static const unsigned kNumNodes = kNumSymbols * 2 - 1; // T
static const unsigned kRoot = kNumNodes - 1;           // R

// Root of the encoder's binary search trees that means "no node".
static const unsigned kNil = kRingSize;

// The upper 6 bits of a match position are Huffman-coded with these fixed tables.
static const Byte kPosEncodeLen[64] =
{
  0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06,
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08
};

static const Byte kPosEncodeCode[64] =
{
  0x00, 0x20, 0x30, 0x40, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x94, 0x98, 0x9C,
  0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE,
  0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
  0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

static const Byte kPosDecodeCode[256] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
  0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
  0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
  0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
  0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
  0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B, 0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
  0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23, 0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
  0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B, 0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};

static const Byte kPosDecodeLen[256] =
{
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08
};


// Adaptive Huffman tree over the literal and match-length symbols.
// Leaves are nodes >= kNumNodes; Parent[kNumNodes + s] is the leaf of symbol s.
class CHuffmanTree
{
  void Rebuild();

public:
  unsigned Freq[kNumNodes + 1]; // the extra entry is a sentinel for Update()
  unsigned Parent[kNumNodes + kNumSymbols];
  unsigned Son[kNumNodes];

  void Init();
  void Update(unsigned symbol);
};

void CHuffmanTree::Init()
{
  unsigned i;
  for (i = 0; i < kNumSymbols; i++)
  {
    Freq[i] = 1;
    Son[i] = i + kNumNodes;
    Parent[i + kNumNodes] = i;
  }
  i = 0;
  for (unsigned j = kNumSymbols; j <= kRoot; j++, i += 2)
  {
    Freq[j] = Freq[i] + Freq[i + 1];
    Son[j] = i;
    Parent[i] = Parent[i + 1] = j;
  }
  Freq[kNumNodes] = 0xFFFF;
  Parent[kRoot] = 0;
}

// Halves all frequencies and rebuilds the tree from the leaves.
void CHuffmanTree::Rebuild()
{
  unsigned j = 0;
  for (unsigned i = 0; i < kNumNodes; i++)
  {
    if (Son[i] >= kNumNodes)
    {
      Freq[j] = (Freq[i] + 1) / 2;
      Son[j] = Son[i];
      j++;
    }
  }

  for (unsigned i = 0, n = kNumSymbols; n < kNumNodes; i += 2, n++)
  {
    const unsigned f = Freq[i] + Freq[i + 1];
    Freq[n] = f;
    unsigned k = n - 1;
    while (f < Freq[k])
      k--;
    k++;
    const size_t moveSize = (n - k) * sizeof(Freq[0]);
    memmove(&Freq[k + 1], &Freq[k], moveSize);
    Freq[k] = f;
    memmove(&Son[k + 1], &Son[k], moveSize);
    Son[k] = i;
  }

  for (unsigned i = 0; i < kNumNodes; i++)
  {
    const unsigned k = Son[i];
    if (k >= kNumNodes)
      Parent[k] = i;
    else
      Parent[k] = Parent[k + 1] = i;
  }
}

// Increments the frequency of a symbol and restores the sibling order.
void CHuffmanTree::Update(unsigned symbol)
{
  if (Freq[kRoot] == kMaxFreq)
    Rebuild();

  unsigned c = Parent[symbol + kNumNodes];
  do
  {
    const unsigned k = ++Freq[c];
    unsigned l = c + 1;
    if (k > Freq[l])
    {
      while (k > Freq[++l])
      {
      }
      l--;
      Freq[c] = Freq[l];
      Freq[l] = k;

      const unsigned i = Son[c];
      Parent[i] = l;
      if (i < kNumNodes)
        Parent[i + 1] = l;

      const unsigned j = Son[l];
      Son[l] = i;
      Parent[j] = c;
      if (j < kNumNodes)
        Parent[j + 1] = c;
      Son[c] = j;

      c = l;
    }
    c = Parent[c];
  }
  while (c != 0);
}


class CEncoder
{
  CHuffmanTree _tree;

  // The window, followed by a copy of its first kMaxMatch - 1 bytes so that
  // matches can be compared without wrapping.
  Byte _ring[kRingSize + kMaxMatch - 1];

  // Binary search trees of all window positions, one per first byte.
  unsigned _left[kRingSize + 1];
  unsigned _right[kRingSize + 257];
  unsigned _parent[kRingSize + 1];

  unsigned _matchPos;
  unsigned _matchLen;

  UInt32 _bitBuf;
  unsigned _numBits;
  CByteBuffer _out;
  size_t _outPos;

  void InitTrees();
  void InsertNode(unsigned r);
  void DeleteNode(unsigned p);

  void WriteByte(unsigned b);
  void WriteBits(unsigned numBits, unsigned code);
  void EncodeSymbol(unsigned symbol);
  void EncodePosition(unsigned pos);

public:
  void Encode(const Byte *data, size_t size, CByteBuffer &stream);
};

void CEncoder::InitTrees()
{
  for (unsigned i = kRingSize + 1; i <= kRingSize + 256; i++)
    _right[i] = kNil;
  for (unsigned i = 0; i < kRingSize; i++)
    _parent[i] = kNil;
}

// Inserts the string at ring position (r) and finds the longest match for it.
void CEncoder::InsertNode(unsigned r)
{
  const Byte *key = &_ring[r];
  unsigned p = kRingSize + 1 + key[0];
  int cmp = 1;
  _right[r] = _left[r] = kNil;
  _matchLen = 0;

  for (;;)
  {
    if (cmp >= 0)
    {
      if (_right[p] == kNil)
      {
        _right[p] = r;
        _parent[r] = p;
        return;
      }
      p = _right[p];
    }
    else
    {
      if (_left[p] == kNil)
      {
        _left[p] = r;
        _parent[r] = p;
        return;
      }
      p = _left[p];
    }

    unsigned i;
    for (i = 1; i < kMaxMatch; i++)
      if ((cmp = (int)key[i] - (int)_ring[p + i]) != 0)
        break;

    if (i > kThreshold)
    {
      const unsigned pos = ((r - p) & kRingMask) - 1;
      if (i > _matchLen)
      {
        _matchPos = pos;
        if ((_matchLen = i) >= kMaxMatch)
          break;
      }
      if (i == _matchLen && pos < _matchPos)
        _matchPos = pos;
    }
  }

  // (p) holds the same string as (r): replace it.
  _parent[r] = _parent[p];
  _left[r] = _left[p];
  _right[r] = _right[p];
  _parent[_left[p]] = r;
  _parent[_right[p]] = r;
  if (_right[_parent[p]] == p)
    _right[_parent[p]] = r;
  else
    _left[_parent[p]] = r;
  _parent[p] = kNil;
}

void CEncoder::DeleteNode(unsigned p)
{
  if (_parent[p] == kNil)
    return;

  unsigned q;
  if (_right[p] == kNil)
    q = _left[p];
  else if (_left[p] == kNil)
    q = _right[p];
  else
  {
    q = _left[p];
    if (_right[q] != kNil)
    {
      do
        q = _right[q];
      while (_right[q] != kNil);
      _right[_parent[q]] = _left[q];
      _parent[_left[q]] = _parent[q];
      _left[q] = _left[p];
      _parent[_left[p]] = q;
    }
    _right[q] = _right[p];
    _parent[_right[p]] = q;
  }
  _parent[q] = _parent[p];
  if (_right[_parent[p]] == p)
    _right[_parent[p]] = q;
  else
    _left[_parent[p]] = q;
  _parent[p] = kNil;
}

void CEncoder::WriteByte(unsigned b)
{
  if (_outPos == _out.Size())
    _out.ChangeSize_KeepData(_out.Size() * 2, _outPos);
  _out[_outPos++] = (Byte)b;
}

// Appends the top (numBits) bits of the 16-bit value (code).
void CEncoder::WriteBits(unsigned numBits, unsigned code)
{
  _bitBuf |= code >> _numBits;
  _numBits += numBits;
  if (_numBits < 8)
    return;
  WriteByte(_bitBuf >> 8);
  _numBits -= 8;
  if (_numBits >= 8)
  {
    WriteByte(_bitBuf);
    _numBits -= 8;
    _bitBuf = code << (numBits - _numBits);
  }
  else
    _bitBuf <<= 8;
}

void CEncoder::EncodeSymbol(unsigned symbol)
{
  unsigned code = 0;
  unsigned numBits = 0;
  unsigned k = _tree.Parent[symbol + kNumNodes];
  do
  {
    code >>= 1;
    if (k & 1) // odd nodes are the second child
      code += 0x8000;
    numBits++;
  }
  while ((k = _tree.Parent[k]) != kRoot);
  WriteBits(numBits, code);
  _tree.Update(symbol);
}

void CEncoder::EncodePosition(unsigned pos)
{
  const unsigned upper = pos >> 6;
  WriteBits(kPosEncodeLen[upper], (unsigned)kPosEncodeCode[upper] << 8);
  WriteBits(6, (pos & 0x3F) << 10);
}

void CEncoder::Encode(const Byte *data, size_t size, CByteBuffer &stream)
{
  _out.Alloc(size / 2 + 64);
  _outPos = 0;
  for (unsigned i = 0; i < kSizePrefixSize; i++)
    WriteByte((Byte)(size >> (8 * i)));
  _bitBuf = 0;
  _numBits = 0;

  _tree.Init();
  InitTrees();
  // Bytes past the end of short inputs take part in match searches, so they
  // must be deterministic.
  memset(_ring, 0, sizeof(_ring));

  size_t inPos = 0;
  unsigned s = 0;
  unsigned r = kRingSize - kMaxMatch;
  memset(_ring, ' ', r);

  unsigned len;
  for (len = 0; len < kMaxMatch && inPos < size; len++)
    _ring[r + len] = data[inPos++];
  for (unsigned i = 1; i <= kMaxMatch; i++)
    InsertNode(r - i);
  InsertNode(r);

  do
  {
    if (_matchLen > len)
      _matchLen = len;
    if (_matchLen <= kThreshold)
    {
      _matchLen = 1;
      EncodeSymbol(_ring[r]);
    }
    else
    {
      EncodeSymbol(255 - kThreshold + _matchLen);
      EncodePosition(_matchPos);
    }

    const unsigned lastMatchLen = _matchLen;
    unsigned i;
    for (i = 0; i < lastMatchLen && inPos < size; i++)
    {
      DeleteNode(s);
      const Byte c = data[inPos++];
      _ring[s] = c;
      if (s < kMaxMatch - 1)
        _ring[s + kRingSize] = c;
      s = (s + 1) & kRingMask;
      r = (r + 1) & kRingMask;
      InsertNode(r);
    }
    for (; i < lastMatchLen; i++)
    {
      DeleteNode(s);
      s = (s + 1) & kRingMask;
      r = (r + 1) & kRingMask;
      if (--len != 0)
        InsertNode(r);
    }
  }
  while (len != 0);

  if (_numBits != 0)
    WriteByte(_bitBuf >> 8);

  stream.CopyFrom(_out, _outPos);
}


class CDecoder
{
  CHuffmanTree _tree;
  Byte _ring[kRingSize];

  const Byte *_in;
  size_t _inSize;
  size_t _inPos;
  unsigned _numBytesPastEnd;

  UInt32 _bitBuf;
  unsigned _numBits;

  unsigned ReadByte();
  void FillBits();
  unsigned ReadBit();
  unsigned ReadBits8();
  unsigned DecodeSymbol();
  unsigned DecodePosition();

public:
  bool Decode(const Byte *stream, size_t streamSize, Byte *dest, UInt32 destSize);
};

unsigned CDecoder::ReadByte()
{
  if (_inPos < _inSize)
    return _in[_inPos++];
  _numBytesPastEnd++;
  return 0;
}

void CDecoder::FillBits()
{
  while (_numBits <= 8)
  {
    _bitBuf |= (UInt32)ReadByte() << (8 - _numBits);
    _numBits += 8;
  }
}

unsigned CDecoder::ReadBit()
{
  FillBits();
  const UInt32 b = _bitBuf;
  _bitBuf <<= 1;
  _numBits--;
  return (b >> 15) & 1;
}

unsigned CDecoder::ReadBits8()
{
  FillBits();
  const UInt32 b = _bitBuf;
  _bitBuf <<= 8;
  _numBits -= 8;
  return (b >> 8) & 0xFF;
}

unsigned CDecoder::DecodeSymbol()
{
  unsigned c = _tree.Son[kRoot];
  while (c < kNumNodes)
    c = _tree.Son[c + ReadBit()];
  c -= kNumNodes;
  _tree.Update(c);
  return c;
}

unsigned CDecoder::DecodePosition()
{
  unsigned i = ReadBits8();
  const unsigned upper = (unsigned)kPosDecodeCode[i] << 6;
  for (unsigned n = kPosDecodeLen[i] - 2u; n != 0; n--)
    i = (i << 1) + ReadBit();
  return upper | (i & 0x3F);
}

bool CDecoder::Decode(const Byte *stream, size_t streamSize, Byte *dest, UInt32 destSize)
{
  _in = stream + kSizePrefixSize;
  _inSize = streamSize - kSizePrefixSize;
  _inPos = 0;
  _numBytesPastEnd = 0;
  _bitBuf = 0;
  _numBits = 0;

  _tree.Init();
  unsigned r = kRingSize - kMaxMatch;
  memset(_ring, ' ', r);
  memset(_ring + r, 0, kRingSize - r);

  // A valid stream ends at most two bytes before the last refill of the bit buffer.
  const unsigned kMaxBytesPastEnd = 2;

  UInt32 pos = 0;
  while (pos < destSize)
  {
    const unsigned symbol = DecodeSymbol();
    if (symbol < 256)
    {
      dest[pos++] = (Byte)symbol;
      _ring[r] = (Byte)symbol;
      r = (r + 1) & kRingMask;
    }
    else
    {
      const unsigned start = (r - DecodePosition() - 1) & kRingMask;
      const unsigned len = symbol - 255 + kThreshold;
      if (len > destSize - pos)
        return false;
      for (unsigned k = 0; k < len; k++)
      {
        const Byte b = _ring[(start + k) & kRingMask];
        dest[pos++] = b;
        _ring[r] = b;
        r = (r + 1) & kRingMask;
      }
    }
    if (_numBytesPastEnd > kMaxBytesPastEnd)
      return false;
  }
  return true;
}


void Encode(const Byte *data, size_t size, CByteBuffer &stream)
{
  if (size == 0)
  {
    stream.Alloc(kSizePrefixSize);
    SetUi32((Byte *)stream, 0)
    return;
  }
  CEncoder *encoder = new CEncoder; // ~60 KB, too large for the stack
  encoder->Encode(data, size, stream);
  delete encoder;
}

bool GetUnpackSize(const Byte *stream, size_t streamSize, UInt32 &unpackSize)
{
  if (streamSize < kSizePrefixSize)
    return false;
  unpackSize = GetUi32(stream);
  return true;
}

bool Decode(const Byte *stream, size_t streamSize, CByteBuffer &dest, size_t maxUnpackSize)
{
  UInt32 unpackSize;
  if (!GetUnpackSize(stream, streamSize, unpackSize))
    return false;
  if (unpackSize > maxUnpackSize || unpackSize > (UInt64)streamSize * kMaxExpansion)
    return false;

  dest.Alloc(unpackSize);
  if (unpackSize == 0)
    return true;
  CDecoder *decoder = new CDecoder;
  const bool ok = decoder->Decode(stream, streamSize, dest, unpackSize);
  delete decoder;
  return ok;
}

} // namespace NLzhuf
} // namespace NXdb
} // namespace NArchive
