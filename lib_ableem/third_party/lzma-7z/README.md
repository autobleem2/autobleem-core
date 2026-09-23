# lzma-7z

The 7z archive reader out of the LZMA SDK 23.01 (https://www.7-zip.org/sdk.html, public domain - see
`lzma-sdk.txt`): the files `C/Util/7z/makefile.gcc` lists for `7zdec`, minus the codecs that libchdr's
vendored `lzma-24.05` already builds (`LzmaDec`, `Bra*`, `Delta`, `CpuArch`, `Alloc`) - these compile
against that copy's headers and link its target, so no symbol exists twice. Extract only
(`Z7_EXTRACT_ONLY`), single-threaded (`Z7_ST`). `ableem::SevenZipArchive` is its user.

Since 2026-09-23 also the `.xz` decoder from the same SDK release (`lzma2301.7z`, sha256
`317dd834d6bbfd95433488b832e823cd3d4d420101436422c03af88507dd1370`): `Xz.c`, `XzDec.c`, `XzCrc64.c`,
`XzCrc64Opt.c`, `Sha256.c`, `Sha256Opt.c` and their headers, unchanged - the subblock filter
(`USE_SUBBLOCK`) and the multi-threaded decoder (`MtDec`, off under `Z7_ST`) are not taken.
`ableem::XzFile` (a PC stick image streamed onto a disk by the flasher) is its user.
