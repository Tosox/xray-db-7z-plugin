# S.T.A.L.K.E.R. DB archive format

This describes the `.db*` / `.xdb*` archives of the X-Ray engine as implemented by this plugin.
The reference is the `db_tools` unpacker/packer in
[xray_re-tools](https://github.com/abramcumner/xray_re-tools) (`sources/utils/converter/db_tools.cxx`).

All integers are little-endian. "Offset" always means an absolute position in the archive file.

## Versions

| Version   | Used by                                                       | Header record layout | Header cipher | File codec | CRC |
|-----------|---------------------------------------------------------------|----------------------|---------------|------------|-----|
| `11xx`    | builds 1114, 1154–1472                                        | 11xx                 | –             | LZHUF      | no  |
| `2215`    | builds 1512–2232                                              | 2215                 | –             | LZO1X      | no  |
| `2945`    | builds 2571–2945                                              | 2945                 | –             | LZO1X      | yes |
| `2947ru`  | build 2947, Shadow of Chernobyl (Russian)                     | 2947                 | RU key        | LZO1X      | yes |
| `2947ww`  | build 2947/3120, Shadow of Chernobyl (worldwide), Lost Alpha  | 2947                 | WW key        | LZO1X      | yes |
| `xdb`     | Clear Sky, Call of Pripyat, most mods                         | 2947                 | –             | LZO1X      | yes |

## Chunks

The file is a flat sequence of chunks:

```
u32 id      bit 31 (0x80000000) set = payload is LZHUF-compressed
u32 size    payload size in bytes
u8  payload[size]
```

| Chunk id | Name     | Contents |
|----------|----------|----------|
| `0x000`  | DATA     | File contents, concatenated. |
| `0x001`  | HEADER   | The file table (see below). |
| `0x29A`  | USERDATA | `xdb` only. Free-form text, usually an LTX `[header]` section describing the mod. |

Readers take the first chunk of each id and ignore the rest. The order is not fixed; the reference
packer writes USERDATA, DATA, HEADER.

## Header chunk

The HEADER payload is decoded in this order:

1. For `2947ru` / `2947ww`: descramble the whole payload (see [Header cipher](#header-cipher)).
2. If bit 31 of the chunk id is set: LZHUF-decompress (see [LZHUF](#lzhuf)).

The result is a sequence of records, one per file or directory, until the end of the data.
Names are CP1251 (Windows Cyrillic), lowercase, with `\` as separator and relative to the
archive's mount point (usually `$game_data$`).

**11xx**
```
char name[]   NUL-terminated
u32  stored   1 = stored, 0 = LZHUF-compressed
u32  offset
u32  size     stored: real size; compressed: packed size
```
The real size of a compressed file is the first 4 bytes of its LZHUF stream.

**2215**
```
char name[]   NUL-terminated
u32  offset
u32  size_real
u32  size_compressed
```

**2945**
```
char name[]   NUL-terminated
u32  crc
u32  offset
u32  size_real
u32  size_compressed
```

**2947** (`2947ru`, `2947ww`, `xdb`)
```
u16  record_size   name length + 16
u32  size_real
u32  size_compressed
u32  crc
char name[record_size - 16]   not NUL-terminated
u32  offset
```

**Directories** have `offset == 0` and all sizes 0. The reference packer writes their names with a
trailing `\`.

**Files** are LZO1X-compressed when `size_real != size_compressed` and stored otherwise
(11xx: see the `stored` flag). Writers keep compressed data only when it is strictly smaller.

**CRC** is the standard CRC-32 (reflected, polynomial `0xEDB88320`, initial value and final XOR
`0xFFFFFFFF`) of the uncompressed file contents.

## Telling the versions apart

The archive does not store its version, so it has to be inferred. This plugin tries the candidates
below in order and accepts the first whose header

- parses exactly to the end of the data,
- places every file inside the DATA chunk,
- has `offset == 0` only for records with all sizes 0,
- has non-empty names without control characters (bytes below `0x20`),
- and, for 11xx, has only 0 or 1 in the `stored` field.

| Header compressed | Candidates, in order |
|-------------------|----------------------|
| yes | LZHUF as `xdb`, `2945`, `2215`, `11xx`; then WW-descramble + LZHUF as `2947ww`; then RU-descramble + LZHUF as `2947ru` |
| no  | the raw payload as `xdb`, `2945`, `2215`, `11xx` |

The rules make layouts that look alike fail on the first real file: an 11xx `stored` value of 0 read as
a 2215 offset is a "directory" with non-zero sizes, and 2947 size fields read as a 2945 name contain
control characters. A header with no files (empty or directories only) is ambiguous and is reported as
the first candidate that parses, which is `xdb` when a USERDATA chunk exists.

## LZHUF

Yoshizaki's 1988 LZHUF (LZSS with an adaptive Huffman coder), with these parameters:

| Parameter | Value |
|-----------|-------|
| Ring buffer size `N` | 4096, initially filled with spaces |
| Longest match `F` | 60 |
| Shortest match | 3 (`THRESHOLD` = 2) |
| Frequency limit before the tree is rebuilt | `0x4000` |

The stream starts with the uncompressed size as `u32`, followed by the bit stream. Decoders may read
up to two bytes past the end of a valid stream and treat them as zero.

## Header cipher

A byte-wise substitution cipher with an LCG key stream, used only by `2947ru` and `2947ww`.

```
next(seed) = 1 + seed * 0x08088405        (32-bit unsigned arithmetic)
key byte   = (next >> 24) & 0xFF
```

The substitution box starts as the identity. Then `size_mult * 256` times: draw `a`, draw `b` until
`b != a`, and swap entries `a` and `b`, both drawn from the same LCG started at `sbox_seed`.

| Key | stream seed | `sbox_seed` | `size_mult` |
|-----|-------------|-------------|-------------|
| RU  | `0x0131A9D3` | `0x01329436` | 8 |
| WW  | `0x016EB2EB` | `0x005BBC4B` | 4 |

For each byte, advance the stream seed once, then
`encrypted = sbox[plain] ^ key` and `plain = inverse_sbox[encrypted ^ key]`.
