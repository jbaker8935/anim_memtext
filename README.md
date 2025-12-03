Animation player based on the file format in fao_lib.h.

Only chunks for MEMTEXT have been implemented in this library.

TODOs:
1. Add non-RLE chunk processing
2. Change RLE to use a 2-byte count so all data is word aligned
3. Test core2x 16-bit VDMA for literal and repeat RLE for longer runs.
4. Test playing mp3 concurrently with anim playback
5. Faster SD reads

