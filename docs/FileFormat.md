## File Format
### Header - 8 bytes
- Magic Number: set to $A8
- Version: set to $01
- Frame Duration: set to the number of 60Hz ticks between animation frames
- Mode: $00 = Text, $01 = Tile.  Tile mode always uses 8x8 Tiles.
- Columns: width of the animated area in tiles or characters
- Rows: height of the animated area in tiles or characters
- XOffset: X offset from upper left of the Tile Map or Text Display
- YOffset: Y offset from upper left of the Tile Map or Text Display
### Chunk Format - variable length data
- Chunk Type: 1 byte
- Chunk ID: 1 byte
- Chunk Length: 2 bytes (little endian)
- Chunk Data: of chunk length bytes
### Frame Start
- Chunk Type: $00
- Chunk ID: $00
- Chunk Length: 0 bytes
- Chunk Data: empty
### Frame End
- Chunk Type: $FF
- Chunk ID: $00
- Chunk Length: 0 bytes
- Chunk Data: empty
### Text Color LUT
- Chunk Type: $01
- Chunk ID: $00 
- Chunk Length: 128 bytes # for MEMTEXT the Chunk Length will be 2048 (2 * 256 * 4)
- Chunk Data: FG LUT data followed by BG LUT data
### Text Color LUT for MEMTEXT
- Chunk Type: $01
- Chunk ID: $00 
- Chunk Length: 2048 (2 * 256 * 4)
- Chunk Data: FG LUT data followed by BG LUT data.
### Text Font Data
- Chunk Type: $02
- Chunk ID: $00
- Chunk Length: 2048 bytes
- Chunk Data: 256 x 8 byte font character definitions (2048 bytes)
### Text Font Data for MEMTEXT
- Chunk Type: $02
- Chunk ID: $00-$03 # ID of the Font Set
- Chunk Length: 2048 bytes
- Chunk Data: 256 x 8 byte font character definitions (2048 bytes)
### Text Fixed Frame Character
- Chunk Type $03
- Chunk ID: $00
- Chunk Length: 4800 bytes
- Chunk Data: 4800 bytes to fill the Text Matrix
### Text Fixed Frame Color
- Chunk Type: $04
- Chunk ID: $00
- Chunk Length: 4800 bytes
- Chunk Data: 4800 bytes to fill the Color Matrix
### Text Fixed Frame Character for MEMTEXT
- Chunk Type $03
- Chunk ID: $00 # Set to $00 for the first 3 chunks of frame, Set to $01 on last chunk of frame
- Chunk Length: 2400 bytes
- Chunk Data: 2400 bytes to fill the Text Matrix.  four chunks per frame for a total of 9600 bytes
### Text Fixed Frame Color for MEMTEXT
- Chunk Type: $04
- Chunk ID: $00 # Set to $00 for the first 3 chunks of frame, Set to $01 on last chunk of frame
- Chunk Length: 2400 bytes
- Chunk Data: 2400 bytes to fill the Color Matrix. Four chunks per frame for a total of 9600 bytes
### Text RLE Frame Character
- Chunk Type: $05
- Chunk ID: $00   
- Chunk Length: variable based on Chunk Data
- Chunk Data: run length encoded character stream
- RLE Encoding: 1 byte count value. If bit 7 is clear bits 0-6 provides the repeat count for the next byte.
If bit 7 is set, then bits 0-6 provide the count of following bytes to output.   For example: ($05,$20) means output $20 5 times,
($85, $20, $30, $31, $42, $51) means output the next 5 characters without repeats.  The total count of characters represented by RLE
encoding is 4800 bytes per frame.
### Text RLE Frame Character for MEMTEXT
- Chunk Type: $05
- Chunk ID: $00   # Set to $00 for the first 3 chunks of frame, Set to $01 on last chunk of frame
- Chunk Length: variable based on Chunk Data
- Chunk Data: run length encoded character stream
- RLE Encoding: 1 byte count value. If bit 7 is clear bits 0-6 provides the repeat count for the next 2 bytes.
If bit 7 is set, then bits 0-6 provide the count of following words (2 bytes) to output.   For example: ($05,$20,$21) means output ($20, $21) 5 times,
($85, $20, $30, $31, $42, $51) means output the next 5 words without repeats.  The total count of characters represented by RLE encoding is 2400 decoded bytes per chunk (2 * 80 * 15).  All four chunks for MEMTEXT will output a total of 9600 bytes.
### Text RLE Frame Color
- Chunk Type: $06
- Chunk ID: $00
- Chunk Length: variable based on Chunk Data
- Chunk Data: RLE encoded data, as above, for the color matrix.  The total count of characters represented by RLE
encoding is 4800 bytes per frame.
### Text RLE Frame Color for MEMTEXT
- Chunk Type: $06
- Chunk ID: $00 # Set to $00 for the first 3 chunks of frame, Set to $01 on last chunk of frame
- Chunk Length: variable based on Chunk Data
- Chunk Data: RLE encoded data, as above, for the color matrix.  The total count of characters represented by RLE encoding is 2400 decoded bytes per chunk (2 * 80 * 15).  All four chunks for MEMTEXT will output a total of 9600 bytes.
### Text RLE Font Data
- Chunk Type: $07
- Chunk ID: $00
- Chunk Length: variable based on Chunk Data
- Chunk Data: RLE encoded data, as above, for the font data.  The total count of characters represented by RLE encoding is 2048 bytes per frame
### Text RLE Font Data for MEMTEXT
- Chunk Type: $07
- Chunk ID: $00-$03 # ID of the Font Set
- Chunk Length: variable based on Chunk Data
- Chunk Data: RLE encoded data, as above, for the font data.  The total count of characters represented by RLE encoding is 2048 bytes per frame
### Graphics Color LUT
- Chunk Type: $08
- Chunk ID: Graphics CLUT id value 0-3
- Chunk Length: 1024 bytes
- Chunk Data: optimized palette data where each entry is 4 bytes (B,G,R,A)
### Graphics Tile Set Data
- Chunk Type: $09
- Chunk ID: Tile Set Id value 0-7
- Chunk Length: 16384 bytes
- Chunk Data: tile pixel data linear arrangement, output 64 bytes of tile 0, followed by 64 bytes of tile 1, etc.
### Graphics Tile Map
- Chunk Type: $0A
- Chunk ID: Tile Map Id value 0-2 (ignored)
- Chunk Length: 8 + Columns X Rows x 2 bytes
- Chunk Data: 8 bytes Tile Set Map + Columns x Rows 2 byte Tile Map entries
