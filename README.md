Animation player based on the file format in fao_lib.h.

Only chunks for MEMTEXT have been implemented in this library.

examples:
save the playlist.txt and the bin files to the same directory as the pgz.
it should work from a subdir, but the pgz looks for the playlist.txt in curdir

the fat32 reader is a claude assisted attempt to get faster block reads from the SD.
its actually faster than kernel for 512+ reads because it skips a buffer write.
It's an experimental hack.

rudimentary mp3 playback is working with limits
- use lowbit rate mp3.  examples use 64bps mp3. player buffering based on that rate.
- on the cli the mp3 file is an optional second argument
- in the playlist add the mp3 after the animation bin filename with a space in between.

TODOs:
- implement additional chunk types - (i will probably never get to this.)

NEW:
- added support for future frame mode color double buffering
- added xiph.org Derf’s Test Media Collection test video encodings

