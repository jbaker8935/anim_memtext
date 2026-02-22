Animation player based on the file format in fao_lib.h.

Only chunks for MEMTEXT have been implemented in this library.

examples:
save the playlist.txt and the bin files to the same directory as the pgz.
it should work from a subdir, but the pgz looks for the playlist.txt in curdir

the fat32 reader is a claude assisted attempt to get faster block reads from the SD.
its actually faster than kernel for 512+ reads because it skips a buffer write.
It's an experimental hack.

TODOs:
- working concurrent mp3 playback
- implement additional chunk types - (i will probably never get to this.)

