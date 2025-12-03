#include "f256lib.h"
#include "../src/fao_lib.h"

/*
## File Format
### Header - 8 bytes
- Magic Number: set to $A8
- Version: set to $01
- Frame Duration: set to the number of 60Hz ticks between animation frames
- Mode: $00 = Text, $01 = Tile, $02 = MEMTEXT  Tile mode always uses 8x8 Tiles.
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
- Chunk ID: $00-$03 # ID of the chunk that is filling part of the frame
- Chunk Length: 2400 bytes
- Chunk Data: 2400 bytes to fill the Text Matrix.  four chunks per frame for a total of 9600 bytes
### Text Fixed Frame Color for MEMTEXT
- Chunk Type: $04
- Chunk ID: $00-$03 # ID of the chunk that is filling part of the frame
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
- Chunk ID: $00-$03 # ID of the chunk that is filling part of the frame
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
- Chunk ID: $00 - $03
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
- Chunk Data: optimized palette data where each entry is 4 bytes (B,R,G,A)
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
*/
extern FAOHeader header; // from main.c
#define CHUNK_BUFFER_SIZE 4096

__attribute__((aligned(2)))
static uint8_t chunk_buffer[CHUNK_BUFFER_SIZE];

// use MMU for MEMTEXT
// enable MMU edit mode by setting MMU Memory control to $b3
// MEMTEXT text memory 0.
// slot address $000c = 8 for 0x10000
// slot address $000d = 9 for 0x12000
// MEMTEXT color memory 0.
// slot address $000c = 10 for 0x14000
// slot address $000d = 11 for 0x16000
// MEMTEXT text memory 1.
// slot address $000c = 12 for 0x18000
// slot address $000d = 13 for 0x1A000
// MEMTEXT color memory 1.
// slot address $000c = 14 for 0x1C000
// slot address $000d = 15 for 0x1E000
// MEMTEXT memory will be writable at address 0x8000 in the CPU address space
// MEMTEXT control registers are used to set the start address for display memory
// 0xD304 Text Address [low byte]
// 0xD305 Text Address [mid byte]
// 0xD306 Text Address [high byte]
// 0xD308 Color Address [low byte]
// 0xD309 Color Address [mid byte]
// 0xD30A Color Address [high byte]
// Logic works as follows:
// When a text or color chunk is received, the MMU is changed to point
// to the inactive MEMTEXT page (0 or 1).  The chunk data is written
// is written starting at address 0x8000.
// At the end of the frame, the MEMTEXT addresses are swapped to point
// to the newly updated page and the displayed_page variable is updated.

volatile uint8_t displayed_page = 0; // which MEMTEXT page is currently being displayed

// Chunk offsets from base address 0x8000
#define CHUNK_SIZE 2400
#define MEMTEXT_BASE 0x8000

// __attribute__((noinline, leaf))
// void near_mvn(char* dest, char* src, uint16_t count);
// // near_mvn implementation for using MVN in cpu memory space
// // dest: __rc2, __rc3
// // src: __rc4, __rc5
// // count: A, X  (save to 05/06)

//   asm(
//       ".text\n"
//       ".global near_mvn\n"
//       "near_mvn:\n"

//       "sta $05\n" // save count low
//       "stx $06\n" // save count high

//       "php\n"           // will save everything, including mx bits.
//       "sei\n"
//       "clc\n"
//       ".byte $fb\n"      // XCE instruction
//       ".byte $c2, $30\n" // REP #$30

//       "ldx __rc4\n"
//       "ldy __rc2\n"
//       "lda $05\n"
//       ".byte $3a\n" // DEC A

//       ".byte $54\n" // MVN
//       "__near_dest_bank:\n"
//       ".byte $00\n"
//       "__near_src_bank:\n"
//       ".byte $00\n"
//       "sec\n"
//       ".byte $fb\n" // XCE instruction
//        "plp\n"
//       "rts\n");    

int16_t buf_fread(void *buf, uint16_t nbytes, uint16_t nmemb, uint8_t *fd) {
	char    *data     = (char *)buf;
	int16_t  read     = 0;
	uint16_t bytes    = nbytes * nmemb;
	int16_t  returned;

	while (read < bytes) {
        uint16_t to_read = bytes - read;
        if(to_read >255) to_read = 255;
        
		returned = fread(data+read,1,to_read,fd);
		if (returned < 0) return -1;
		if (returned == 0) break;
		read += returned;
	}

	return read / nbytes;
}


int readFAOHeader(FILE *f, FAOHeader *h) {
    return fread(h, sizeof(FAOHeader), 1, f);
}

int readFAOChunkHeader(FILE *f, FAOChunkHeader *ch) {
    return fread(ch, sizeof(FAOChunkHeader), 1, f);
}

int processFrameStart(FAOChunkHeader *ch) {
    // Handle frame start logic here

    return 0;
}

__attribute__((noinline))
int processFrameEnd(FAOChunkHeader *ch) {
    // Toggle displayed_page: we were writing to !displayed_page, now show it
    // If displayed_page was 0, we wrote to page 1, now display page 1
    // If displayed_page was 1, we wrote to page 0, now display page 0
    if (displayed_page == 0) {
        displayed_page = 1;
        // Show page 1: text at 0x018000, color at 0x01C000
        POKE(0xD304, 0x00);
        POKE(0xD305, 0x80);
        POKE(0xD306, 0x01);
        POKE(0xD308, 0x00);
        POKE(0xD309, 0xC0);
        POKE(0xD30A, 0x01);
    } else {
        displayed_page = 0;
        // Show page 0: text at 0x010000, color at 0x014000
        POKE(0xD304, 0x00);
        POKE(0xD305, 0x00);
        POKE(0xD306, 0x01);
        POKE(0xD308, 0x00);
        POKE(0xD309, 0x40);
        POKE(0xD30A, 0x01);
    }
    return 0;
}

int processTextColorLUT(FILE *f, FAOChunkHeader *ch) {
    uint16_t bytes_read=0;
    switch (header.mode) {
        case 0x00:
        // Standard Text LUT not implemented
              return -1; // Invalid chunk length
        case 0x02:
            bytes_read = buf_fread(chunk_buffer, sizeof(uint8_t), 2048, f);
            if (bytes_read != 2048) {
                textGotoXY(0,10);
                textPrint("Error reading MEMTEXT LUT data, read ");
                textGotoXY(25,10);
                textPrintUInt(bytes_read);
                getchar();
                return -1; // Error reading LUT Data
            }
            // copy to MEMTEXT Color LUT
            POKE(MMU_IO_CTRL,0x08); // IO PAGE 4 (bits 3,1,0)
            for (uint16_t i = 0; i < 2048; i++) {
                POKE(0xC000 + i, chunk_buffer[i]);
            }
            for (uint16_t i = 0; i < 2048; i++) {
                POKE(0xC800 + i, chunk_buffer[i]);
            }            
            POKE(MMU_IO_CTRL,0x00); // IO PAGE 0
            break;
        default:
            textGotoXY(0,10);
            textPrint("Invalid header.mode ");
            textGotoXY(18,10);
            textPrintUInt(header.mode);
            getchar();
            return -1; 
    }
    return 0;
} 


int processTextFontData(FILE *f, FAOChunkHeader *ch) {
    if (ch->chunkLength != 2048) {
        return -1; // Invalid Text Font Data chunk
    }
    if (buf_fread(chunk_buffer, sizeof(uint8_t), 2048, f) != 2048) {
        return -1; // Error reading font data
    }
    if (header.mode == 0x02) { // MEMTEXT mode
        uint16_t font_base_addr = 0xC000 + (ch->chunkID * 2048);
        // copy to MEMTEXT Font Data
        POKE(MMU_IO_CTRL,0x09); // IO PAGE 5 (page bits 3,1,0)

        for (uint16_t i = 0; i < 2048; i++) {
            POKE(font_base_addr + i, chunk_buffer[i]);
        }
        POKE(MMU_IO_CTRL,0x00); // IO PAGE 0
    }
    return 0;
}

int processTextFixedFrameCharacter(FILE *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x03 || ch->chunkID != 0x00 || ch->chunkLength != 4800) {
        return -1; // Invalid Text Fixed Frame Character chunk
    }
    uint8_t frameData[4800];
    if (buf_fread(frameData, sizeof(uint8_t), 4800, f) != 4800) {
        return -1; // Error reading frame data
    }
    // Handle fixed frame character data here
    return 0;
}
int processTextFixedFrameColor(FILE *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x04 || ch->chunkID != 0x00 || ch->chunkLength != 4800) {
        return -1; // Invalid Text Fixed Frame Color chunk
    }
    uint8_t colorData[4800];
    if (buf_fread(colorData, sizeof(uint8_t), 4800, f) != 4800) {
        return -1; // Error reading color data
    }
    // Handle fixed frame color data here
    return 0;
}

// Helper function to decode RLE data into specified output buffer
// Uses volatile pointers since output is memory-mapped via MMU
__attribute__((noinline))
static void decodeRLEFrame(uint16_t chunkLength, uint8_t chunkID, uint8_t *output_buffer) {
    uint8_t *in_ptr = chunk_buffer;
    uint8_t *in_end = chunk_buffer + chunkLength;
    // Calculate output pointer: base + (chunkID * 2400)
    volatile uint8_t *out_ptr = (volatile uint8_t *)(MEMTEXT_BASE + (uint16_t)chunkID * CHUNK_SIZE);
    
    while (in_ptr < in_end) {
        uint8_t countByte = *in_ptr++;
        
        if (countByte & 0x80) {
            // Literal run: copy (count & 0x7F) words directly
            uint8_t literalCount = countByte & 0x7F;
            
            while (literalCount--) {
                *out_ptr++ = *in_ptr++;
                *out_ptr++ = *in_ptr++;
            }
        } else {
            // Repeated run: repeat next word (count & 0x7F) times
            uint8_t repeatCount = countByte & 0x7F;
            uint8_t value_lo = *in_ptr++;
            uint8_t value_hi = *in_ptr++;
            
            while (repeatCount--) {
                *out_ptr++ = value_lo;
                *out_ptr++ = value_hi;
            }
        }
    }
}

#pragma clang optimize off
__attribute__((noinline))
int processTextRLEFrameCharacter(FILE *f, FAOChunkHeader *ch) {
    if (buf_fread(chunk_buffer, sizeof(uint8_t), ch->chunkLength, f) != ch->chunkLength) {
        return -1; // Error reading RLE data
    }
    // Write to the OPPOSITE of displayed_page
    if (displayed_page == 0) {
        POKE(MMU_MEM_BANK_4, 12); 
        POKE(MMU_MEM_BANK_5, 13);
    } else {
        POKE(MMU_MEM_BANK_4, 8); 
        POKE(MMU_MEM_BANK_5, 9);
    }

    decodeRLEFrame(ch->chunkLength, ch->chunkID, (uint8_t *) 0x8000);

    return 0;
}

__attribute__((noinline))
int processTextRLEFrameColor(FILE *f, FAOChunkHeader *ch) {
    if (buf_fread(chunk_buffer, sizeof(uint8_t), ch->chunkLength, f) != ch->chunkLength) {
        return -1; // Error reading RLE data
    }
    // Write to the OPPOSITE of displayed_page
    if (displayed_page == 0) {
        POKE(MMU_MEM_BANK_4, 14); 
        POKE(MMU_MEM_BANK_5, 15);
    } else {
        POKE(MMU_MEM_BANK_4, 10); 
        POKE(MMU_MEM_BANK_5, 11);
    }

    decodeRLEFrame(ch->chunkLength, ch->chunkID, (uint8_t *) 0x8000);

    return 0;
}
#pragma clang optimize on

int processTextRLEFontData(FILE *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x07 || ch->chunkID != 0x00) {
        return -1; // Invalid Text RLE Font Data chunk
    }
    uint8_t *rleData = (uint8_t *)malloc(ch->chunkLength);
    if (!rleData) {
        return -1; // Memory allocation error
    }
    if (buf_fread(rleData, sizeof(uint8_t), ch->chunkLength, f) != ch->chunkLength) {
        free(rleData);
        return -1; // Error reading RLE data
    }
    // Handle RLE font data here
    free(rleData);
    return 0;
}

int processGraphicsColorLUT(FILE *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x08 || ch->chunkID > 3 || ch->chunkLength != 1024) {
        return -1; // Invalid Graphics Color LUT chunk
    }
    uint8_t lut[1024];
    if (buf_fread(lut, sizeof(uint8_t), 1024, f) != 1024) {
        return -1; // Error reading LUT data
    }
    // Handle graphics color LUT data here
    return 0;
}

int processGraphicsTileSetData(FILE *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x09 || ch->chunkID > 7 || ch->chunkLength != 16384) {
        return -1; // Invalid Graphics Tile Set Data chunk
    }
    uint8_t tileSetData[16384];
    if (buf_fread(tileSetData, sizeof(uint8_t), 16384, f) != 16384) {
        return -1; // Error reading tile set data
    }
    // Handle graphics tile set data here
    return 0;
}

int processGraphicsTileMap(FILE *f, FAOChunkHeader *ch, uint8_t columns, uint8_t rows) {
    if (ch->chunkType != 0x0A || ch->chunkID > 2 || ch->chunkLength != (8 + columns * rows * 2)) {
        return -1; // Invalid Graphics Tile Map chunk
    }
    uint8_t *tileMapData = (uint8_t *)malloc(ch->chunkLength);
    if (!tileMapData) {
        return -1; // Memory allocation error
    }
    if (buf_fread(tileMapData, sizeof(uint8_t), ch->chunkLength, f) != ch->chunkLength) {
        free(tileMapData);
        return -1; // Error reading tile map data
    }
    // Handle graphics tile map data here
    free(tileMapData);
    return 0;
}



