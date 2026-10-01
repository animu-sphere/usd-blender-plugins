# Zstandard

This directory vendors the decompression-only single-file library and public
header from [Zstandard](https://github.com/facebook/zstd) release `v1.5.7`,
commit `f8745da6ff1ad1e7bab384bd1f9d742439278e99`.

Source archive:
https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz

`zstddeclib.c` is generated without local modifications using the upstream tool.
From `build/single_file_libs` in the extracted release:

```sh
python3 combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstddeclib.c zstddeclib-in.c
```

Copy the generated file, `lib/zstd.h`, `lib/zstd_errors.h`, and `LICENSE`
together when updating.
The reader compiles the decoder once into `blendFile`; no installed Zstandard
package, shared library, network access, or Python is needed during the build.

Zstandard is used under its BSD-3-Clause license, reproduced in `LICENSE`.
The decoder is a maintained upstream implementation with fuzzing coverage;
the reader must still enforce its own input, output, and window limits before
claiming compressed-file support.