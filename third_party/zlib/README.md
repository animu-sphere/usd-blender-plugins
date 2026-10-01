# zlib

This directory vendors the inflate decoder and checksum sources from
[zlib](https://zlib.net/) release `1.3.2` (2026-02-17).

Source archive: https://zlib.net/zlib-1.3.2.tar.gz

Archive SHA-256:
`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`

All upstream files are copied without modification. To reproduce this subset,
verify the archive checksum, extract it, and copy these files together:

```text
adler32.c crc32.c inflate.c inffast.c inftrees.c zutil.c
crc32.h inffast.h inffixed.h inflate.h inftrees.h
zconf.h zlib.h zutil.h gzguts.h LICENSE
```

The reader compiles the six C sources directly into `blendFile`, with the
upstream `Z_PREFIX` option to prefix zlib symbols. Its include directory and
compile definition are private; no zlib headers or dependency targets are
exported. The compression encoder, gzip file I/O, examples, and upstream build
system are not included. `gzguts.h` is retained because `zutil.c` includes it.
No installed zlib package, shared library, or network access is needed to build
or consume the installed reader.

zlib is used under its zlib license, reproduced in `LICENSE`. Release 1.3.2
includes fixes from the upstream security audit. Vendoring and the decoder
smoke test do not establish gzip `.blend` support: the reader must still add
bounded decoding, diagnostic handling, and compressed-file tests.