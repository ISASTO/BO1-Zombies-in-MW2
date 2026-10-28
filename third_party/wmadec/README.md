# wmadec: Windows Media Audio v2 decoder (xWMA)

Black Ops stores most of its sounds as xWMA (WMA v2 packets, format 0x161).
MW2 plays 16-bit PCM, so the mod decodes them on the owner's machine while it
builds `bo1mw2\cache\bo1_zombie_sounds.ff`.

`wmadec.hpp` is a C++ port of FFmpeg's WMA decoder (`libavcodec/wmadec.c`,
`wma.c`, `wma_common.c`). `wma_tables.hpp` holds its tables
(`libavcodec/wmadata.h`, the AAC scale factor code from `libavcodec/aactab.c`,
`wma_freqs.c`), taken from FFmpeg n7.1.

The port keeps what xWMA uses:

- WMA v2, one or two channels;
- the bit reservoir and variable block lengths;
- VLC-coded exponents;
- noise coding.

LSP-coded exponents (another WMA mode) are refused rather than decoded.

## Licence

Copyright (c) 2002-2007 The FFmpeg Project.

These two files are distributed under the GNU Lesser General Public License,
version 2.1 or later (`COPYING.LGPLv2.1` in this directory). The rest of the
project is MIT. The decoder is compiled into `d3d9.dll`, and the complete
source of that DLL is in this repository, so anyone can rebuild it with a
modified decoder.

## Checks

- **Unit test.** `tests/bo1_sound_tests.cpp` decodes a fixture made by FFmpeg's
  encoder from a synthetic chirp, and compares it with FFmpeg's own decode
  (within 1 LSB). It also checks the fast IMDCT against its definition.
- **Real Black Ops sounds.** All 1,443 xWMA sounds in Black Ops'
  `common_zombie.ff` were checked locally against FFmpeg 6.1, and all of them
  match within 1 LSB with the same sample count. Those files are never
  committed.
