# LZM - A high speed LZ style lossless compressor

Literal and match length values are encoded using up to 32 bits.  Distances
are encoded as variable length integers using 1-4 bytes with up to 28 bits
available for the distance value.  This provides an effective sliding window
size of 256MB.

Using a reference system of an Intel(R) Xeon(R) CPU E5-2620 v3 @ 2.40GHz the
following performance was achieved:

Fast mode compression rates range from 500MB/s up to 13GB/s
Fast mode decompression rates range from 2GB/s up to 18GB/s

Included is a utility called lzm that offers the following levels of
compression:

Level 0
  - No compression, store all data as literals.  This is only really useful
    for testing raw memcpy speed.

Level 1
  - Fast mode.  Favours throughput over compression ratio.

Levels 2-6
  - Increasing levels of compression aggressiveness with higher levels using
    a larger search window.

Here is sample benchmark output for the silesia data set:

```
$ lzm -b 5 silesia.tar
File silesia.tar: size 211957760 bytes
Level 0: --> 211958066,  100.0001%,  6213.0766 MB/s,  7550.3757 MB/s
Level 1: --> 99161391,   46.7836%,   523.8262 MB/s,  2416.7902 MB/s
Level 2: --> 88115385,   41.5721%,   133.1574 MB/s,  1851.1249 MB/s
Level 3: --> 87341211,   41.2069%,   121.4492 MB/s,  1887.1248 MB/s
Level 4: --> 85010124,   40.1071%,    89.4445 MB/s,  1948.5786 MB/s
Level 5: --> 81608599,   38.5023%,    30.4287 MB/s,  2021.8230 MB/s
Level 6: --> 77441366,   36.5362%,     9.6110 MB/s,  1868.7887 MB/s
```

The software in this suite has only been tested on Intel CPUs.  No specific
consideration has been made to support big endian systems in which case endian
conversion support would need to be added.
