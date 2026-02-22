#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
#include <stdio.h>
#include <string.h>
#include "../src/fao_lib.h"
#include "../src/timer.h"
#include "../src/fat32_stream.h"

#define BA_FAO_FILE "ba_memtext.fao"
#define BA_MP3_FILE "ba.mp3"
#define CHUNK2K 0x0800
#define CHUNK8K 0x2000
#define CHUNK256B 0x0100
#define CHUNK64B 0x40
#define MP3_FRAME_BYTES 1024  // cap bytes sent to FIFO per frame, 64k mp3 ~ 800 bytes per frame at 10 fps
//VS1053b CTRL modes
#define    CTRL_Start   0x01 //1: start transfer, followed by 0 to stop
#define    CTRL_RWn     0x02 //1: read mode, 0: write mode
#define    CTRL_Busy    0x80 //if set, spi transfer is busy

#define VS_SCI_CTRL  0xD700
//VS1053b CTRL modes
#define    CTRL_Start   0x01 //1: start transfer, followed by 0 to stop
#define    CTRL_RWn     0x02 //1: read mode, 0: write mode
#define    CTRL_Busy    0x80 //if set, spi transfer is busy

#define VS_SCI_ADDR  0xD701
//VS1053b specific SCI addresses
#define VS_SCI_ADDR_MODE     0x00
#define VS_SCI_ADDR_STATUS   0x0001
#define VS_SCI_ADDR_BASS     0x0002
#define VS_SCI_ADDR_CLOCKF   0x0003
#define VS_SCI_ADDR_WRAM     0x0006
#define VS_SCI_ADDR_WRAMADDR 0x0007
#define VS_SCI_ADDR_VOL      0x000B

#define SM_RESET  0x0004         /* bit 2 */
#define SM_CANCEL 0x0008         /* bit 3 */

#define VS_SCI_DATA  0xD702   //2 bytes
#define VS_FIFO_COUNT 0xD704   //2 bytes
#define VS_FIFO_DATA 0xD707

#define SDI_MAX_TRANSFER_SIZE 32
#define PAR_END_FILL_BYTE 0x1e06 /* VS1063, VS1053 */
#define SDI_END_FILL_BYTES 2050  /* 2050 bytes of endFillByte */

FAOHeader header;
FAOChunkHeader chunkHeader;
char filename[256]={0};
char base_filename[32]={0};
char base_mp3filename[32]={0};
char g_base_dir[256]={0};
fat32_file_t playlist;
fat32_file_t fao;
fat32_file_t mp3file;

uint16_t bufferIndex=0; //index for the local 8k buffer register
char buffer[CHUNK2K]; //4x the size of the VS FIFO buffer
uint16_t rawFIFOCount=0;
uint16_t bytesToTopOff=0;
uint16_t bytes_read=0;
uint32_t total_bytes_read=0;
uint16_t mp3_frame_bytes=0;
bool use_playlist=false;
bool playMP3=false;
bool mp3Done=false;
bool anim_started = false;

uint16_t vs1053_read_sci(uint8_t addr) {

  // while (PEEK(0xD700) & CTRL_Busy);  // make sure not busy    
  POKE(VS_SCI_ADDR, addr);
  POKE(VS_SCI_CTRL, CTRL_Start | CTRL_RWn);  // Activate xCS and start read
  POKE(VS_SCI_CTRL, 0);                      // Deactivate xCS

  while (PEEK(0xD700) & CTRL_Busy)
  ;
//   for(uint8_t i = 0; i < 50; i++) {
//     __asm__("nop");
//   }  
    uint16_t ret = ((uint16_t)PEEK(0xD703) << 8) | PEEK(0xD702);
    return ret;
}

void vs1053_write_sci(uint8_t addr, uint16_t data) {

  // while (PEEK(0xD700) & CTRL_Busy);  // make sure not busy
  POKE(VS_SCI_ADDR, addr);
  POKEW(VS_SCI_DATA, data);
  POKE(VS_SCI_CTRL, CTRL_Start);  // start write
  POKE(VS_SCI_CTRL, 0);           // deactivate
  while (PEEK(0xD700) & CTRL_Busy);
//   for(uint8_t i = 0; i < 2; i++) {
//     __asm__("nop");
//   }


  return;
}

static inline void vs1053_write_mem(uint16_t wram_addr, uint16_t data) {
    vs1053_write_sci(VS_SCI_ADDR_WRAM, wram_addr);
    vs1053_write_sci(VS_SCI_ADDR_WRAM, data);
}
// make inline after debugging
uint16_t vs1053_read_mem(uint16_t wram_addr) {
    vs1053_write_sci(VS_SCI_ADDR_WRAM, wram_addr);
    return vs1053_read_sci(VS_SCI_ADDR_WRAM);
}



//codec enable all lines
void openAllCODEC()
{
	POKE(0xD620, 0x1F);
	POKE(0xD621, 0x2A);
	POKE(0xD622, 0x01);
	while(PEEK(0xD622) & 0x01);
}

void boostVSClock()
{
//target the clock register
POKEW(VS_SCI_ADDR, VS_SCI_ADDR_CLOCKF);
//aim for 2.5X clock multiplier, no frills
POKEW(VS_SCI_DATA,0xC000);
//trigger the command
POKE(VS_SCI_CTRL,CTRL_Start);
POKE(VS_SCI_CTRL,0);
//check to see if it's done
	while (PEEK(VS_SCI_CTRL) & CTRL_Busy)
		;
}
__attribute__((noinline, optnone))
void flush_mp3buffer()
{
  uint8_t endFillByte = vs1053_read_mem(PAR_END_FILL_BYTE);

  for (uint16_t i = 0; i < SDI_END_FILL_BYTES; i += SDI_MAX_TRANSFER_SIZE) {
    uint16_t toWrite = (SDI_END_FILL_BYTES - i >= SDI_MAX_TRANSFER_SIZE)
                           ? SDI_MAX_TRANSFER_SIZE
                           : (SDI_END_FILL_BYTES - i);
    for (uint16_t j = 0; j < toWrite; j++) POKE(VS_FIFO_DATA, endFillByte);
  }  
    
  unsigned short oldMode = vs1053_read_sci(VS_SCI_ADDR_MODE);
  vs1053_write_sci(VS_SCI_ADDR_MODE, oldMode | SM_CANCEL);
  // flush after cancel
  for (uint16_t i = 0; i < SDI_END_FILL_BYTES; i += SDI_MAX_TRANSFER_SIZE) {
    uint16_t toWrite = (SDI_END_FILL_BYTES - i >= SDI_MAX_TRANSFER_SIZE)
                           ? SDI_MAX_TRANSFER_SIZE
                           : (SDI_END_FILL_BYTES - i);
    for (uint16_t j = 0; j < toWrite; j++) POKE(VS_FIFO_DATA, endFillByte);
    if(!(vs1053_read_sci(VS_SCI_ADDR_MODE) & SM_CANCEL)) {
            break;
    }
  } 
//   // reset
//   vs1053_write_sci(VS_SCI_ADDR_MODE, oldMode | SM_RESET);
//   // wait for reset to complete
//   for(uint8_t i = 0; i < 100; i++) {
//     __asm__("nop");
//   }
}

__attribute__((noinline))
void mp3_stream_reader() {
    // uint8_t bytes_read = 0;  // local 8 bit def
    while (anim_started && playMP3 && !mp3Done) {
        if(mp3_frame_bytes >= MP3_FRAME_BYTES) {
            return; // proceed to next frame
        }

        rawFIFOCount = PEEKW(VS_FIFO_COUNT);
        rawFIFOCount &= 0x0FFF; //mask to 12 bits
        if(rawFIFOCount > 2048-512) {
            return; // skip, enough data in FIFO
        }

        bytes_read = fat32_read(&mp3file, (uint8_t*)buffer, 512); 

        if (fat32_error(&mp3file)) {
            // Error reading MP3 data
            fat32_close(&fao);
            fat32_close(&mp3file);
            textReset();
            textGotoXY(0,10);
            textPrint("Error reading MP3 data");
            getchar();
            mp3Done = true;
            playMP3 = false;
            return;
        }
        
        {
            /* 32-way unrolled write loop for optimal throughput */
            // uint16_t i = 0;
            // uint8_t blocks = bytes_read / 32;
            // while (blocks--) {
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            //     POKE(VS_FIFO_DATA, buffer[i++]); POKE(VS_FIFO_DATA, buffer[i++]);
            // }
            // uint8_t rem = bytes_read & 31u; /* remaining bytes */
            // while(rem--) {
            //     POKE(VS_FIFO_DATA, buffer[i++]);
            // }            
            for(uint16_t i = 0; i < bytes_read; i++ ) {
                POKE(VS_FIFO_DATA, buffer[i]);
                for (uint8_t j = 0; j < 4; j++) {
                    __asm__("nop");
                }                                                                                                
            }
        }
        mp3_frame_bytes += bytes_read;
        total_bytes_read += bytes_read;
        if(total_bytes_read == mp3file.file_size) {
            mp3Done = true;
            fat32_close(&mp3file);
            playMP3 = false;
            flush_mp3buffer();
           
        }
    }

}
__attribute((noinline))
uint8_t play_animation(char *anim_filename, char *mp3_filename) {
    strcpy(filename, g_base_dir);

    strcat(filename, anim_filename);

    if (!fat32_open(&fao, filename)) {
        textReset();
        textGotoXY(0,10);
        textPrint("could not open animation file: ");
        textPrint(anim_filename);
        getchar();
        return -1;
    }
    // now open optional mp3 file
    strcpy(filename, g_base_dir);
    if(mp3_filename != NULL && strlen(mp3_filename) > 0) {
        strcat(filename, mp3_filename);
                
        if (!fat32_open(&mp3file, filename)) {
            textReset();
            textGotoXY(0,10);
            textPrint("could not open MP3 file: ");
            textPrint(mp3_filename);
            getchar();
            return -1;
        } else {
            playMP3 = true;
        }
    }


    openAllCODEC();
    boostVSClock();

    // zero out MEMTEXT high memory 0x10000-0x1FFFF
    graphicsWaitVerticalBlank();
    dma16fill((uint32_t)0x10000, 0x10000u, 0x00);

    // enable MEMTEXT mode
    POKE(0xD300, 0x01);
    POKE(VKY_MSTR_CTRL_0, 0x01); // BLOCK MODE TEXT

    if (readFAOHeader(&fao, &header) <= 0) {
        fat32_close(&fao);
        textReset();
        textGotoXY(0,10);
        textPrint("could not read FAO header:");
        textGotoXY(0,25);
        textPrintUInt((uint16_t) fao.bytes_read);
        getchar();
        return -1; // Error reading header
    }


    if (header.magic != 0xA8 || header.version != 0x01) {
        fat32_close(&fao);

        return -1; // Invalid file format
    }
    setTimer0();
    while (readFAOChunkHeader(&fao, &chunkHeader) > 0) {
        int result = 0;
       
        switch (chunkHeader.chunkType) {
            case 0x00:
                result = processFrameStart(&chunkHeader);
                mp3_frame_bytes = 0;

                break;
            case 0xFF:
                // using timer0 instead of polling timer1, hard-coded to 10 fps
                while(!isTimerDone())
                    ;
                setTimer0();
                result = processFrameEnd(&chunkHeader);
                // set anim_started when first frame is processed
                anim_started = true;
                POKE(VKY_MSTR_CTRL_1, 0x40); // ENABLE MEMTEXT OVERRIDE BLOCK MODE 
                break;
            case 0x01:
                result = processTextColorLUT(&fao, &chunkHeader);
                break;
            case 0x02:
                result = processTextFontData(&fao, &chunkHeader);
                break;
            case 0x03:
                result = processTextFixedFrameCharacter(&fao, &chunkHeader);
                break;
            case 0x04:
                result = processTextFixedFrameColor(&fao, &chunkHeader);
                break;
            case 0x05:
                result = processTextRLEFrameCharacter(&fao, &chunkHeader);
                // debugMemTextChunk();
                break;
            case 0x06:
                result = processTextRLEFrameColor(&fao, &chunkHeader);
                // debugMemTextChunk();
                break;
            case 0x07:
                result = processTextRLEFontData(&fao, &chunkHeader);
                break;
            case 0x08:
                result = processGraphicsColorLUT(&fao, &chunkHeader);
                break;
            case 0x09:
                result = processGraphicsTileSetData(&fao, &chunkHeader);
                break;
            case 0x0A:
                result = processGraphicsTileMap(&fao, &chunkHeader, header.columns, header.rows);
                break;
            default:
                // Unknown chunk type
                result = -1;
                break;
        }
        // TODO: maybe not this error handling here
        if (result != 0) {
            fat32_close(&fao);
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
        // MP3 stream here
        mp3_stream_reader();
    }
    if(playMP3) {
        fat32_close(&mp3file);
        mp3Done = true;
        playMP3 = false;
        flush_mp3buffer();
        openAllCODEC();
        boostVSClock();         
    } 
    fat32_close(&fao);
    return 0; // Success
}

int main(int argc, char *argv[]) {

    // set global base path
    char *last_slash = strrchr(argv[0], '/');
    if (last_slash != NULL) {
        uint8_t dir_len = last_slash - argv[0] + 1;
        strncpy(g_base_dir, argv[0], dir_len);
        g_base_dir[dir_len] = '\0';
    } 

    // init fast sd stream reading
    if (!fat32_init()) {
        printf("FAILED: FAT32 initialization failed\n");
        getchar();
        return -1;
    }
    // if no argument, try to open "playlist.txt" in base dir
    if (argc == 1) {
        strcpy(filename, g_base_dir);
        strcat(filename, "playlist.txt");
        if (!fat32_open(&playlist, filename)) {
            textReset();
            textGotoXY(0,0);
            printf("No playlist.txt found in base dir\n");
            printf("or no anim file specified\n");
            printf("anim_memtext: anim_file [mp3_file]\n");
            printf("key to exit\n");
            getchar();
            return -1;
        } else {
            use_playlist = true;
        }

    }

    // if use playlist is true, read line from playlist.txt
    // split line with space as a separator into anim file and optional mp3 file
    // set base_filename to anim file
    // set base_mp3filename to mp3 file
    // call play_animation with those filenames
    // continue until EOF
    if (use_playlist) {
        char line[128]={0};
        while (fat32_gets(line, sizeof(line), &playlist) != NULL) {
            // parse line
            char *token = strtok(line, " \t\r\n");
            if (token != NULL) {
                strcpy(base_filename, token);
                token = strtok(NULL, " \t\r\n");
                if (token != NULL) {
                    strcpy(base_mp3filename, token);
                } else {
                    base_mp3filename[0] = '\0';
                }
                if(strlen(base_filename) == 0) {
                    continue; // skip empty lines
                } 
                // printf("Playing animation: %s MP3: %s\n", base_filename, base_mp3filename);
                play_animation(base_filename, base_mp3filename);
            }
        }
        fat32_close(&playlist);
        return 0;

    } else {
        // argv[1] is the animation binary file and optional argv[2] is mp3 file
        play_animation(argv[1], (argc >=3) ? argv[2] : NULL);
        return 0;
    }


}
