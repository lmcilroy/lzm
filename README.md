# LZM - A high speed LZ style lossless compressor

Literal and match length values are encoded using up to 32 bits.  Distances
are encoded as variable length integers using 1-4 bytes with up to 28 bits
available for the distance value.  This provides an effective sliding window
size of 256MB.

Using a reference system of an Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz
the following performance was achieved:

Fast mode compression rates range from 680MB/s up to 16GB/s
Fast mode decompression rates range from 2.9GB/s up to 29GB/s

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
$ ./lzm -b 10 silesia.tar
File silesia.tar: size 211957760 bytes
Level 0: --> 211958066,  100.0001%,  7750.3184 MB/s,  8459.4879 MB/s
Level 1: --> 99161391,   46.7836%,   686.1777 MB/s,  2913.9851 MB/s
Level 2: --> 88115385,   41.5721%,   173.0759 MB/s,  2344.6813 MB/s
Level 3: --> 87341211,   41.2069%,   154.2715 MB/s,  2306.4785 MB/s
Level 4: --> 85010124,   40.1071%,   113.5757 MB/s,  2291.1117 MB/s
Level 5: --> 81608599,   38.5023%,    40.5490 MB/s,  2476.3639 MB/s
Level 6: --> 77441366,   36.5362%,     8.5176 MB/s,  2558.6272 MB/s
```

The software in this suite has only been tested on Intel CPUs.  No specific
consideration has been made to support big endian systems in which case endian
conversion support would need to be added.
