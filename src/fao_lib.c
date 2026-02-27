#include "f256lib.h"
#include "../src/fao_lib.h"



extern FAOHeader header; // from main.c
#define CHUNK_BUFFER_SIZE 4096
#define DMA_FILL_VAL16   0xDF02
#define DMA_16_BIT      0x40

void far_mvn(uint32_t dest, uint32_t src, uint16_t count);
// far_mvn implementation
// dest: A,X, __rc2   __rc3 unused
// src: __rc4, __rc5, __rc6    __rc7 unused
// count: __rc8, __rc9

  asm(
      ".text\n"
      ".global far_mvn\n"
      "far_mvn:\n"

      "pha\n"  // src low
      "stx __rc7\n"  // src mid

      "lda __rc6\n"
      "sta __src_bank\n"
      "lda __rc2\n"
      "sta __dest_bank\n"
      "pla\n"  // src low
      "sta __rc6\n"

      "php\n"           // will save everything, including mx bits.
      "sei\n"
      "lda $00\n"
      "ora #$08\n"
      "sta $00\n" 
      "clc\n"
      ".byte $fb\n"      // XCE instruction
      ".byte $c2, $30\n" // REP #$30

      "ldx __rc4\n"
      "ldy __rc6\n"
      "lda __rc8\n"
      ".byte $3a\n" // DEC A

      ".byte $54\n" // MVN
      "__dest_bank:\n"
      ".byte $00\n"
      "__src_bank:\n"
      ".byte $00\n"
      "sec\n"
      ".byte $fb\n" // XCE instruction
      "lda $00\n"
      "and #$F7\n"
      "sta $00\n" // ensure bit 3 is clear on return
      "plp\n"
      "rts\n");

__attribute__((aligned(2)))
static uint8_t chunk_buffer[CHUNK_BUFFER_SIZE];
__attribute__((optnone,noinline))
void dma16cpy(uint32_t dest, uint32_t src, uint32_t word_count) {
	// while (PEEKW(RAST_ROW_L) < 482); // Wait for VBL.

	asm("sei");
	POKE(DMA_CTRL, DMA_CTRL_ENABLE | DMA_16_BIT);
	POKEA(DMA_DST_ADDR, dest);
	POKEA(DMA_SRC_ADDR, src);
	POKEA(DMA_COUNT, word_count);

	POKE(DMA_CTRL, PEEK(DMA_CTRL) | DMA_CTRL_START);
    asm (
        "1: lda $DF01\n"
        "   bmi 1b\n"
        "   stz $DF00\n"
    );
    asm("cli");

}
__attribute__((optnone,noinline))
void dma16fill(uint32_t dest, uint32_t word_count, uint32_t value) {

	asm("sei");
	POKE(DMA_CTRL, DMA_CTRL_FILL | DMA_CTRL_ENABLE | DMA_16_BIT);
	POKEA(DMA_DST_ADDR, dest);
	POKEA(DMA_COUNT, word_count);
	POKEW(DMA_FILL_VAL16, value);

	POKE(DMA_CTRL, PEEK(DMA_CTRL) | DMA_CTRL_START);
    asm (
        "1: lda $DF01\n"
        "   bmi 1b\n"
        "   stz $DF00\n"
    );

	asm("cli");

}
// MEMTEXT cpu address for chunk data 4 frames of 2048 bytes then 1 frame of 1408 bytes
static uint8_t * memtext_cpu_addr[10] = {
    (uint8_t *)0x8000, // chunk ID 0
    (uint8_t *)0x8800, // chunk ID 1
    (uint8_t *)0x9000, // chunk ID 2
    (uint8_t *)0x9800, // chunk ID 3
    (uint8_t *)0xA000 // chunk ID 4

};

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

#define MEMTEXT_BASE 0x8000
// for memtext each frame 

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


int readFAOHeader(fat32_file_t *f, FAOHeader *h) {
    uint16_t bytes_read=0;
    bytes_read = fat32_read(f, (uint8_t *) h, sizeof(FAOHeader));
    if (bytes_read != sizeof(FAOHeader)) {
        return -1; // Error reading header
    }   
    return sizeof(FAOHeader);
}

int readFAOChunkHeader(fat32_file_t *f, FAOChunkHeader *ch) {
    uint16_t bytes_read=0;
    bytes_read = fat32_read(f, (uint8_t *) ch, sizeof(FAOChunkHeader));
    if (bytes_read != sizeof(FAOChunkHeader)) {
     
        return -1; // Error reading chunk header
    }   
    return sizeof(FAOChunkHeader);
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
__attribute__((noinline))
int processTextColorLUT(fat32_file_t *f, FAOChunkHeader *ch) {
    uint16_t bytes_read=0;
    switch (header.mode) {
        case 0x00:
        // Standard Text LUT not implemented
              return -1; // Invalid chunk length
        case 0x02:
            bytes_read = fat32_read(f, chunk_buffer, ch->chunkLength);
          
            if (fat32_error(f) || bytes_read != ch->chunkLength) {
                textGotoXY(0,10);
                textPrint("Error reading MEMTEXT LUT data, read ");
                textGotoXY(25,10);
                textPrintUInt(bytes_read);
                getchar();
                return -1; // Error reading LUT Data
            }
            // copy to MEMTEXT Color LUT
            POKE(MMU_IO_CTRL, 0x08);  // IO PAGE 4 (bits 3,1,0)
            // uint16_t lut_length = ch->chunkLength / 2;
            // if (ch->chunkID == 0) {
            //     for (uint16_t i = 0; i < lut_length; i++) {
            //         POKE(0xC000 + i, chunk_buffer[i]);
            //         POKE(0xC800 + i, chunk_buffer[i]);
            //     }
            // } else {
            //     for (uint16_t i = 0; i < lut_length; i++) {
            //         POKE(0xC400 + i, chunk_buffer[i]);
            //         POKE(0xCC00 + i, chunk_buffer[i]);
            //     }
            // }
            for (uint16_t i = 0; i < ch->chunkLength; i++) {
                POKE(0xC000 + i, chunk_buffer[i]);
            }
            for (uint16_t i = 0; i < ch->chunkLength; i++) {
                POKE(0xC800 + i, chunk_buffer[i]);
            }                
            POKE(MMU_IO_CTRL, 0x00);  // IO PAGE 0
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

__attribute__((noinline))
int processTextFontData(fat32_file_t *f, FAOChunkHeader *ch) {
    uint16_t bytes_read=0;
    if (ch->chunkLength != 2048) {
        return -1; // Invalid Text Font Data chunk
    }
    bytes_read = fat32_read(f, chunk_buffer, ch->chunkLength);
    if (fat32_error(f) || bytes_read != ch->chunkLength) {
        return -1; // Error reading font data
    }
    if (header.mode == 0x02) { // MEMTEXT mode
        uint16_t font_base_addr = 0xC000 + (ch->chunkID * ch->chunkLength);
        // copy to MEMTEXT Font Data
        POKE(MMU_IO_CTRL,0x09); // IO PAGE 5 (page bits 3,1,0)

        for (uint16_t i = 0; i < ch->chunkLength; i++) {
            POKE(font_base_addr + i, chunk_buffer[i]);
        }
        POKE(MMU_IO_CTRL,0x00); // IO PAGE 0
    }
    return 0;
}
__attribute__((optnone,noinline))
int processTextFixedFrameCharacter(fat32_file_t *f, FAOChunkHeader *ch) {
    if (header.mode != 0x02) {
        return -1; // Only MEMTEXT mode supported
    }
    // Write to the OPPOSITE of displayed_page
    if (displayed_page == 0) {
        POKE(MMU_MEM_BANK_4, 12); 
        POKE(MMU_MEM_BANK_5, 13);
    } else {
        POKE(MMU_MEM_BANK_4, 8); 
        POKE(MMU_MEM_BANK_5, 9);
    }
    uint16_t bytes_read=0;
    bytes_read = fat32_read(f, (uint8_t *) memtext_cpu_addr[ch->chunkID], ch->chunkLength);
    if (fat32_error(f) || bytes_read != ch->chunkLength) {
        return -1; // Error reading RLE data
    }
    return 0;
}
__attribute__((optnone,noinline))
int processTextFixedFrameColor(fat32_file_t *f, FAOChunkHeader *ch) {
    if (header.mode != 0x02) {
        return -1; // Only MEMTEXT mode supported
    }
    // Write to the OPPOSITE of displayed_page
    if (displayed_page == 0) {
        POKE(MMU_MEM_BANK_4, 14); 
        POKE(MMU_MEM_BANK_5, 15);
    } else {
        POKE(MMU_MEM_BANK_4, 10); 
        POKE(MMU_MEM_BANK_5, 11);
    }
    uint16_t bytes_read=0;
    bytes_read = fat32_read(f, (uint8_t *) memtext_cpu_addr[ch->chunkID], ch->chunkLength);
    if (fat32_error(f) || bytes_read != ch->chunkLength) {
        return -1; // Error reading RLE data
    }
    return 0;
}

// // Declare zero-page globals as extern so C can access them
// extern __attribute__((section(".zp.bss"))) uint8_t* g_stream_buffer_ptr;
// extern __attribute__((section(".zp.bss"))) uint16_t g_stream_offset;
// extern __attribute__((section(".zp.bss"))) uint16_t g_stream_avail;

// static void decode_rle_frame_asm(uint16_t chunkLength, uint8_t chunkID);
// // chunkLength: A, X
// // chunkID: __rc2
// // Uses globals: g_stream_buffer_ptr, g_stream_offset, g_stream_avail
// // Returns: A = bytes actually copied (may be less than requested if buffer exhausted)
// //          Returns 0 only if buffer is completely empty (needs refill first)
//     asm(
//         ".section .zp.bss,\"aw\",@nobits\n"
//         ".global g_uint16\n"
//         "g_uint16: .fill 2\n"
//         ".global g_src_ptr\n"
//         "g_src_ptr: .fill 2\n"
//         ".global g_dest_ptr\n"
//         "g_dest_ptr: .fill 2\n"
//         ".text\n"
//         ".global decode_rle_frame_asm\n"
//         "decode_rle_frame_asm:\n"
//         // code goes here
//         "sta g_uint16\n"
//         "stx g_uint16+1\n"
//         "lda chunk_buffer\n"
//         "sta g_src_ptr\n"
//         "lda chunk_buffer+1\n"
//         "sta g_src_ptr+1\n"
//         // Calculate destination pointer based on chunkID
//         "lda chunkID\n"
//         "asl\n" // multiply by 2
//         "tax\n"
//         "lda memtext_cpu_addr,x\n"
//         "sta g_dest_ptr\n"
//         "lda memtext_cpu_addr+1,x\n"
//         "sta g_dest_ptr+1\n"
//     "1:\n"
//         // Load count word
//         "clc\n"
//         "adc chunkID\n"
//         "rts"
//     );


// Helper function to decode RLE data into specified output buffer
// Uses volatile pointers since output is memory-mapped via MMU
// NOTE: Only supports MEMTEXT RLE format currently header.mode == 0x02
// Requires word-aligned input and output
__attribute__((noinline))
static void decodeRLEFrame(uint16_t chunkLength, uint8_t chunkID) {
    uint16_t *in_ptr = (uint16_t*)chunk_buffer;
    uint16_t *in_end = (uint16_t*)(chunk_buffer + chunkLength); // chunkLength in words and must be even
    // Calculate output pointer: base + (chunkID * 2400)
    volatile uint16_t *out_ptr = (volatile uint16_t *) memtext_cpu_addr[chunkID];
    
    while (in_ptr < in_end) {
        uint16_t countWord = *in_ptr++;
        
        if (countWord & 0x8000) {
            // Literal run: copy (count & 0x7FFF) words directly
            uint16_t literalCount = countWord & 0x7FFF;
            
            if(literalCount > 16 ) {
                far_mvn((uint32_t)out_ptr, (uint32_t)in_ptr, literalCount*2);  // count in bytes
                out_ptr += literalCount;
                in_ptr += literalCount;
            } else {
                // for small copies use simple loop
                while (literalCount--) {
                    POKEW((uint32_t)out_ptr, *in_ptr);
                    out_ptr++;
                    in_ptr++;
                }
            }
        } else {
            // Repeated run: repeat next word (count & 0x7FFF) times
            uint16_t repeatCount = countWord & 0x7FFF;
            uint16_t value = *in_ptr++;
            if(repeatCount > 16 ) {
                dma16fill((uint32_t)out_ptr, repeatCount, value);
                out_ptr += repeatCount;
            } else {
                // for small fills use simple loop
                while (repeatCount--) {
                    POKEW((uint32_t)out_ptr, value);
                    out_ptr++;
                }
            }
            
        }
    }
}

__attribute__((optnone, noinline))
int processTextRLEFrameCharacter(fat32_file_t *f, FAOChunkHeader *ch) {
    uint16_t bytes_read=0;
    bytes_read = fat32_read(f, chunk_buffer, ch->chunkLength);
    if (fat32_error(f) || bytes_read != ch->chunkLength) {
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

    decodeRLEFrame(ch->chunkLength, ch->chunkID);

    return 0;
}

__attribute__((optnone, noinline))
int processTextRLEFrameColor(fat32_file_t *f, FAOChunkHeader *ch) {
    uint16_t bytes_read=0;
    bytes_read = fat32_read(f, chunk_buffer, ch->chunkLength);
    if (fat32_error(f) || bytes_read != ch->chunkLength) {
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

    decodeRLEFrame(ch->chunkLength, ch->chunkID);

    return 0;
}

int processTextRLEFontData(fat32_file_t *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x07 || ch->chunkID != 0x00) {
        return -1; // Invalid Text RLE Font Data chunk
    }

    return 0;
}

int processGraphicsColorLUT(fat32_file_t *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x08 || ch->chunkID > 3 || ch->chunkLength != 1024) {
        return -1; // Invalid Graphics Color LUT chunk
    }

    // Handle graphics color LUT data here
    return 0;
}

int processGraphicsTileSetData(fat32_file_t *f, FAOChunkHeader *ch) {
    if (ch->chunkType != 0x09 || ch->chunkID > 7 || ch->chunkLength != 16384) {
        return -1; // Invalid Graphics Tile Set Data chunk
    }

    // Handle graphics tile set data here
    return 0;
}

int processGraphicsTileMap(fat32_file_t *f, FAOChunkHeader *ch, uint8_t columns, uint8_t rows) {
    if (ch->chunkType != 0x0A || ch->chunkID > 2 || ch->chunkLength != (8 + columns * rows * 2)) {
        return -1; // Invalid Graphics Tile Map chunk
    }

    // Handle graphics tile map data here

    return 0;
}



