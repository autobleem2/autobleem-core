#!/usr/bin/env python3
"""The .xz fixtures tests/core/test_xz_file.cpp reads, from nothing but the standard library.

The payload is "line 0\\n" .. "line 19999\\n" (the test rebuilds it to compare):
  test_crc64.xz      one stream, CRC64 (xz's default)
  test_two.xz        the first 100000 bytes as a SHA-256 stream, four bytes of stream padding, the rest
                     as a CRC32 stream - what `cat a.xz b.xz` gives, which xz reads as one file
  test_damaged.xz    test_crc64.xz with one byte of the compressed data flipped
  test_truncated.xz  the first half of test_crc64.xz
  test_blocks.xz     one stream of four 64 KiB blocks (needs the xz command)
  test_image.xz      a 9 MB disk image for the flasher's tests (see below)
"""
import lzma
import os
import subprocess

here = os.path.dirname(os.path.abspath(__file__))
payload = b"".join(b"line %d\n" % i for i in range(20000))


def write(name, data):
    with open(os.path.join(here, name), "wb") as f:
        f.write(data)


one = lzma.compress(payload, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64)
write("test_crc64.xz", one)
write("test_two.xz",
      lzma.compress(payload[:100000], format=lzma.FORMAT_XZ, check=lzma.CHECK_SHA256)
      + b"\0\0\0\0"
      + lzma.compress(payload[100000:], format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32))
damaged = bytearray(one)
damaged[len(damaged) // 2] ^= 0x55
write("test_damaged.xz", bytes(damaged))
write("test_truncated.xz", one[: len(one) // 2])
# several blocks in one stream, as `xz -T0` writes a stick image - the standard library cannot, the xz CLI can
blocks = subprocess.run(["xz", "-c", "--block-size=65536", "--check=crc64"], input=payload,
                        stdout=subprocess.PIPE, check=True).stdout
write("test_blocks.xz", blocks)
# a disk image for tests/installer/test_flasher_job.cpp: 144 runs of 64 KiB (run k is byte k) and 1200 bytes of
# "tail" - more than one of the flasher's 4 MiB chunks, and not a whole number of 512-byte sectors
image = b"".join(bytes([k & 0xff]) * 65536 for k in range(144)) + b"tail" * 300
write("test_image.xz", lzma.compress(image, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64))
print("payload %d bytes, test_crc64.xz %d bytes" % (len(payload), len(one)))
