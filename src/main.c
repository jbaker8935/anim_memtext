#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
// #include "../src/muUtils.h"
// #include "../src/muVS1053b.h"
#include "../src/fao_lib.h"

#define BA_FAO_FILE "ba_memtext.fao"
#define BA_MP3_FILE "ba.mp3"

FAOHeader header;
FAOChunkHeader chunkHeader;
char filename[256]={0};
char g_base_dir[256]={0};
extern uint8_t displayed_page;

#pragma clang optimize off
void debugMemTextChunk() {
    // Switch to Block Text Mode
    // Print chunkHeader.chunkID
    // Print displayed page
    // Wait for key press
    POKE(VKY_MSTR_CTRL_1, 0x00); // DISABLE MEMTEXT 
    textGotoXY(0,0);
    textPrint("Chunk ID:");
    textGotoXY(10,0);
    textPrintUInt(chunkHeader.chunkID);
    textGotoXY(0,1);
    textPrint("Displayed Page:");
    textGotoXY(18,1);
    textPrintUInt(displayed_page);
    getchar();
    // update MEMTEXT pointers to point to inactive page
    POKE(0xD304, (displayed_page == 1) ? 0x00 : 0x00); // low byte
    POKE(0xD305, (displayed_page == 1) ? 0x00 : 0x80); // mid byte
    POKE(0xD306, 0x01); // high byte
    POKE(0xD308, (displayed_page == 1) ? 0x00 : 0x00); // low byte
    POKE(0xD309, (displayed_page == 1) ? 0x40 : 0xc0); // mid byte
    POKE(0xD30A, 0x01); // high byte
    // switch back to memtext mode
    POKE(VKY_MSTR_CTRL_1, 0x40); // ENABLE MEMTEXT OVERRIDE BLOCK MODE
    // wait for key press
    getchar();
    // update MEMTEXT pointers to point to active page
    // wait for key press
    POKE(0xD304, (displayed_page == 0) ? 0x00 : 0x00); // low byte
    POKE(0xD305, (displayed_page == 0) ? 0x00 : 0x80); // mid byte
    POKE(0xD306, 0x01); // high byte
    POKE(0xD308, (displayed_page == 0) ? 0x00 : 0x00); // low byte
    POKE(0xD309, (displayed_page == 0) ? 0x40 : 0xc0); // mid byte
    POKE(0xD30A, 0x01); // high byte 
    getchar();   
}
#pragma clang optimize on

int main(int argc, char *argv[]) {

    // uint8_t old_mmu_ctrl = PEEK(MMU_MEM_CTRL);
        
    char *last_slash = strrchr(argv[0], '/');
    if (last_slash != NULL) {
        uint8_t dir_len = last_slash - argv[0] + 1;
        strncpy(g_base_dir, argv[0], dir_len);
        g_base_dir[dir_len] = '\0';
    } 

    strcpy(filename, g_base_dir);
    if(argc > 1) {
        strcat(filename, argv[1]);
    } else {
        strcat(filename, BA_FAO_FILE);
    }

    // will add MP3 playback later

    // zero out MEMTEXT high memory 0x10000-0x1FFFF
    dmaFill((uint32_t)0x10000, 0x10000u, 0x00);

    // enable MEMTEXT mode
    POKE(0xD300, 0x01);
    POKE(VKY_MSTR_CTRL_0, 0x01); // BLOCK MODE TEXT




    FILE *fao = fopen(filename, "rb");
    if (!fao) {
        return -1; // Error opening file
    }

    if (readFAOHeader(fao, &header) != 1) {
        fclose(fao);
        return -1; // Error reading header
    }

    if (header.magic != 0xA8 || header.version != 0x01) {
        fclose(fao);

        return -1; // Invalid file format
    }

    while (readFAOChunkHeader(fao, &chunkHeader) == 1) {
        int result = 0;
        switch (chunkHeader.chunkType) {
            case 0x00:
                result = processFrameStart(&chunkHeader);
                break;
            case 0xFF:
                result = processFrameEnd(&chunkHeader);
                POKE(VKY_MSTR_CTRL_1, 0x40); // ENABLE MEMTEXT OVERRIDE BLOCK MODE 
                break;
            case 0x01:
                result = processTextColorLUT(fao, &chunkHeader);
                break;
            case 0x02:
                result = processTextFontData(fao, &chunkHeader);
                break;
            case 0x03:
                result = processTextFixedFrameCharacter(fao, &chunkHeader);
                break;
            case 0x04:
                result = processTextFixedFrameColor(fao, &chunkHeader);
                break;
            case 0x05:
                result = processTextRLEFrameCharacter(fao, &chunkHeader);
                // debugMemTextChunk();
                break;
            case 0x06:
                result = processTextRLEFrameColor(fao, &chunkHeader);
                // debugMemTextChunk();
                break;
            case 0x07:
                result = processTextRLEFontData(fao, &chunkHeader);
                break;
            case 0x08:
                result = processGraphicsColorLUT(fao, &chunkHeader);
                break;
            case 0x09:
                result = processGraphicsTileSetData(fao, &chunkHeader);
                break;
            case 0x0A:
                result = processGraphicsTileMap(fao, &chunkHeader, header.columns, header.rows);
                break;
            default:
                // Unknown chunk type, skip it
                fseek(fao, chunkHeader.chunkLength, SEEK_CUR);
                break;
        }
        // TODO: maybe not this error handling here
        if (result != 0) {
            fclose(fao);
            POKE(VKY_MSTR_CTRL_0, 0x01); // BLOCK MODE TEXT
            POKE(VKY_MSTR_CTRL_1, 0x00); // DISABLE MEMTEXT OVERRIDE BLOCK MODE
            POKE(0xD300, 0x00); // disable MEMTEXT mode
            textReset();
            textGotoXY(0,10);
            textPrint("Failure processing chunk ");
            textGotoXY(10,25);
            textPrintUInt(chunkHeader.chunkType);              
            getchar();
            return -1; // Error processing chunk
        }
    }
    fclose(fao);
    return 0; // Success
}
