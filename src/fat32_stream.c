#include "../src/fat32_stream.h"

// Optimized near memory move using MVN instruction for 65816 processor
// Must disable optimization for this function due to inline assembly
__attribute__((optnone, noinline, leaf))
void near_mvn(char* dest, char* src, uint16_t count);
// near_mvn implementation for using MVN in cpu memory space
// dest: __rc2, __rc3
// src: __rc4, __rc5
// count: A, X  (save to self-mod count)

  asm(
      ".text\n"
      ".global near_mvn\n"
      "near_mvn:\n"

      "sta __count_low\n" // save count low
      "stx __count_high\n" // save count high

      "php\n"           // will save everything, including mx bits.
      "sei\n"
      "clc\n"
      ".byte $fb\n"      // XCE instruction
      ".byte $c2, $30\n" // REP #$30

      "ldx __rc4\n"
      "ldy __rc2\n"
      ".byte $a9\n" // LDA immediate
      "__count_low:\n"
      ".byte $00\n" // placeholder for count low
      "__count_high:\n"
      ".byte $00\n" // placeholder for count high
      ".byte $3a\n" // DEC A

      ".byte $54\n" // MVN
      "__near_dest_bank:\n"
      ".byte $00\n"
      "__near_src_bank:\n"
      ".byte $00\n"
      "sec\n"
      ".byte $fb\n" // XCE instruction
       "plp\n"
      "rts\n");

void fast_memcpy_asm(void* dest, const void* src, uint16_t count);
// a,x: count is 256 or less.
// dest: __rc2, __rc3
// src: __rc4, __rc5
    asm(
        ".text\n"
        ".global fast_memcpy_asm\n"
        "fast_memcpy_asm:\n"
        "tax\n" // move lower byte of count to X
        "ldy #0\n"
        "1:\n"
        "lda (__rc4),y\n"    // Load from source
        "sta (__rc2),y\n"    // Store to destination
        "iny\n"
        "dex\n"
        "bne 1b\n"
        "rts\n");


static bool sd_read_sector(uint32_t sector, uint8_t* buffer);
static uint32_t get_next_cluster(uint32_t cluster);
static inline uint32_t cluster_to_sector(uint32_t cluster);

static inline void fast_memcpy(void* dest, const void* src, uint16_t count) {
    // Safety check: Y register is 8-bit, so max count is 256
    if (count == 0) return;
    if (count > 256) {
        // Fall back to standard memcpy for large blocks
        memcpy(dest, src, count);
        return;
    }
    fast_memcpy_asm(dest, src, count);
    
    // uint8_t *d = (uint8_t*)dest;
    // const uint8_t *s = (const uint8_t*)src;
    // uint8_t c = (uint8_t)count;  // Lower byte of count
    
    // // Use inline assembly with Y as index, comparing after increment
    // __asm__ volatile (
    //     "ldy #0\n"
    //     "1:\n"
    //     "lda (%1),y\n"    // Load from source
    //     "sta (%0),y\n"    // Store to destination
    //     "iny\n"
    //     "cpy %2\n"        // Compare Y to lower byte of count
    //     "bne 1b\n"
    //     : // no outputs
    //     : "r"(d), "r"(s), "r"(c)
    //     : "a", "y", "memory"
    // );
}


uint32_t g_fat_start_sector;
uint32_t g_data_start_sector;
uint32_t g_root_dir_cluster;
uint8_t g_sectors_per_cluster;
uint8_t g_spc_shift;  // log2(sectors_per_cluster) - for fast multiply via shift
uint32_t g_last_partition_start;

// Static sector buffer for directory operations
static uint8_t sector_buf[SECTOR_SIZE];


#define SPI_CTRL_REG8 (*(volatile uint8_t*)SPI_CTRL_REG)
#define SPI_DATA_REG8 (*(volatile uint8_t*)SPI_DATA_REG)

static bool s_fast_clock_enabled = false;

static inline uint8_t spi_exchange(uint8_t value) {
    SPI_DATA_REG8 = value;
    while (SPI_CTRL_REG8 & SPI_CTRL_BUSY) {
        // wait for transfer
    }
    return SPI_DATA_REG8;
}

static bool spi_wait_ready(void) {
    uint8_t timeout = 2;  // matches ~508ms timeout in reference asm
    do {
        for (uint16_t x = 0; x < 256; ++x) {
            for (uint16_t y = 0; y < 256; ++y) {
                if (spi_exchange(0xFF) == 0xFF) {
                    return true;
                }
            }
        }
    } while (--timeout);
    return false;
}

static inline void spi_deselect(void) {
    SPI_CTRL_REG8 &= (uint8_t)~SPI_CTRL_SELECT_MASK;
    spi_exchange(0xFF);  // ensure bus idle
}

static inline void spi_disable(void) {
    spi_deselect();
}

static bool spi_select(void) {
    SPI_CTRL_REG8 |= SPI_CTRL_SELECT_SDCARD;
    spi_exchange(0xFF);
    if (!spi_wait_ready()) {
        spi_deselect();
        return false;
    }
    return true;
}

static inline void spi_set_slow_clock(void) {
    SPI_CTRL_REG8 = SPI_CTRL_SLOWCLK;
    s_fast_clock_enabled = false;
}

static inline void spi_set_fast_clock(void) {
    SPI_CTRL_REG8 &= (uint8_t)~SPI_CTRL_SLOWCLK;
    s_fast_clock_enabled = true;
}

static bool sd_wait_data_token(uint8_t expected_token);

bool sd_wait_data_token_asm(uint8_t expected_token);
    // expected_token: A

    asm(
        ".text\n"
        ".global sd_wait_data_token_asm\n"
        "sd_wait_data_token_asm:\n"
        "sta __expected_token\n"
        "ldx #0\n"
        "1:\n"
        "ldy #0\n"
        "2:\n"
        "lda #$FF\n"
        "sta $DD01\n"       // Write 0xFF to SPI_DATA
        "3:\n"
        "bit $DD00\n"       // Test SPI_CTRL
        "bmi 3b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "cmp __expected_token\n"
        "beq __found_token\n"
        "iny\n"
        "bne 2b\n"
        "inx\n"
        "bne 1b\n"
        // timeout
        "lda #$00\n"
        "rts\n"
        "__found_token:\n"
        "lda #$01\n"
        "rts\n"
        "__expected_token:\n"
        ".byte $00\n" // placeholder for expected_token
    );


void sd_read_data_block_asm(uint8_t * buffer);
    // buffer: __rc2, __rc3
    asm(
        ".text\n"
        ".global sd_read_data_block_asm\n"
        "sd_read_data_block_asm:\n"
        "ldy #0\n"
        "ldx #$FF\n"        // 0xFF for writing to SPI_DATA
    "1:\n"
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "2:\n"
        "bit $DD00\n"       // Test SPI_CTRL 
        "bmi 2b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"   // Store to buffer
        "iny\n"
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "22:\n"
        "bit $DD00\n"       // Test SPI_CTRL 
        "bmi 22b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"   // Store to buffer
        "iny\n"    
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "23:\n"
        "bit $DD00\n"       // Test SPI_CTRL 
        "bmi 23b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"   // Store to buffer
        "iny\n" 
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "24:\n"
        "bit $DD00\n"       // Test SPI_CTRL 
        "bmi 24b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"   // Store to buffer
        "iny\n"                      
        "bne 1b\n"
    
    // Second 256 bytes - assembly loop  
        "inc __rc3\n" // increment high byte of buffer pointer

        "ldy #0\n"
    "3:\n"
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "4:\n"
        "bit $DD00\n"       // Test SPI_CTRL
        "bmi 4b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"      // Store to buffer+256
        "iny\n"
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "42:\n"
        "bit $DD00\n"       // Test SPI_CTRL
        "bmi 42b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"      // Store to buffer+256
        "iny\n"
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "43:\n"
        "bit $DD00\n"       // Test SPI_CTRL
        "bmi 43b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"      // Store to buffer+256
        "iny\n"
        "stx $DD01\n"       // Write 0xFF to SPI_DATA
    "44:\n"
        "bit $DD00\n"       // Test SPI_CTRL
        "bmi 44b\n"          // Loop while BUSY (bit 7 set)
        "lda $DD01\n"       // Read SPI_DATA
        "sta (__rc2),y\n"      // Store to buffer+256
        "iny\n"               
        "bne 3b\n"   
    // Discard CRC bytes (2 bytes)
        "stx $DD01\n"
    "5:\n"
        "bit $DD00\n"
        "bmi 5b\n"
        "lda $DD01\n"       // discard
        "stx $DD01\n"
    "6:\n"
        "bit $DD00\n"
        "bmi 6b\n"
        "lda $DD01\n"       // discard
        "rts\n"            
    );


// Optimized sector read - reads 512 bytes with minimal overhead
// Uses inline assembly for the tight inner loop on 6502
__attribute__((optnone))
static bool sd_read_data_block(uint8_t* buffer) {
    if (!sd_wait_data_token(0xFE)) {
        return false;
    }
   sd_read_data_block_asm(buffer);

    return true;
}

static bool sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t* response_out) {
    spi_deselect();
    if (!spi_select()) {
        return false;
    }

    spi_exchange(0x40 | cmd);
    spi_exchange((uint8_t)(arg >> 24));
    spi_exchange((uint8_t)(arg >> 16));
    spi_exchange((uint8_t)(arg >> 8));
    spi_exchange((uint8_t)arg);

    uint8_t crc = 0x01;
    if (cmd == 0) {
        crc = 0x95;
    } else if (cmd == 8) {
        crc = 0x87;
    }
    spi_exchange(crc);

    uint8_t response = 0xFF;
    for (uint8_t i = 0; i < 10; ++i) {
        response = spi_exchange(0xFF);
        if ((response & 0x80) == 0) {
            if (response_out) {
                *response_out = response;
            }
            return true;
        }
    }

    spi_deselect();
    return false;
}

static bool sd_wait_data_token(uint8_t expected_token) {

    for (uint16_t x = 0; x < 256; ++x) {
        for (uint16_t y = 0; y < 256; ++y) {
            uint8_t token = spi_exchange(0xFF);
            if (token == expected_token) {
                return true;
            }
        }
    }
    
    return false;
}

static bool s_card_block_addressing = false;

static bool sd_init(void) {
    spi_deselect();
    spi_set_slow_clock();

    for (uint8_t i = 0; i < 10; ++i) {
        spi_exchange(0xFF);
    }

    uint8_t r1;
    if (!sd_send_cmd(0, 0x00000000, &r1) || r1 != 0x01) {
        return false;
    }

    if (!sd_send_cmd(8, 0x000001AA, &r1) || r1 != 0x01) {
        spi_deselect();
        return false;
    }

    // consume remaining R7 response bytes
    spi_exchange(0xFF);
    spi_exchange(0xFF);
    spi_exchange(0xFF);
    spi_exchange(0xFF);

    do {
        if (!sd_send_cmd(55, 0x00000000, &r1)) {
            return false;
        }
        if (!sd_send_cmd(41, 0x40000000, &r1)) {
            return false;
        }
    } while (r1 != 0x00);

    if (!sd_send_cmd(58, 0x00000000, &r1) || r1 != 0x00) {
        return false;
    }

    uint8_t ocr[4];
    for (int i = 0; i < 4; ++i) {
        ocr[i] = spi_exchange(0xFF);
    }

    if ((ocr[0] & 0x40) == 0) {
        spi_deselect();
        return false;  // require block addressing like reference implementation
    }

    s_card_block_addressing = true;

    spi_deselect();
    spi_set_fast_clock();
    return true;
}

static bool sd_read_sector(uint32_t sector, uint8_t* buffer) {
    if (!s_card_block_addressing) {
        return false;
    }

    bool fast_clock_was_enabled = s_fast_clock_enabled;
    bool clock_downgraded = false;

    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0 && fast_clock_was_enabled && !clock_downgraded) {
            spi_set_slow_clock();
            spi_exchange(0xFF);
            clock_downgraded = true;
        }

        uint8_t response = 0xFF;
        if (!sd_send_cmd(17, sector, &response)) {
            spi_wait_ready();
            continue;
        }

        if (response != 0x00) {
            spi_deselect();
            spi_exchange(0xFF);
            spi_wait_ready();
            continue;
        }

        if (!sd_read_data_block(buffer)) {
            spi_deselect();
            spi_exchange(0xFF);
            spi_wait_ready();
            continue;
        }

        spi_deselect();
        if (clock_downgraded && fast_clock_was_enabled) {
            spi_set_fast_clock();
        }
        return true;
    }

    if (clock_downgraded && fast_clock_was_enabled) {
        spi_set_fast_clock();
    }

    return false;
}

static bool sd_read_data_block_split(uint8_t* buf1, uint16_t count1, uint8_t* buf2) {
    if (!sd_wait_data_token(0xFE)) {
        return false;
    }

    // Read first count1 bytes to buf1
    for (uint16_t i = 0; i < count1; ++i) {
        *buf1++ = spi_exchange(0xFF);
    }

    // Read remaining bytes to buf2 or discard
    uint16_t remaining = 512 - count1;
    for (uint16_t i = 0; i < remaining; ++i) {
        if (buf2) {
            *buf2++ = spi_exchange(0xFF);
        } else {
            spi_exchange(0xFF);  // Discard
        }
    }
    
    // Discard CRC bytes (2 bytes)
    spi_exchange(0xFF);
    spi_exchange(0xFF);

    return true;
}

static bool sd_read_sector_split(uint32_t sector, uint8_t* buf1, uint16_t count1, uint8_t* buf2) {
    if (!s_card_block_addressing) {
        return false;
    }

    bool fast_clock_was_enabled = s_fast_clock_enabled;
    bool clock_downgraded = false;

    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0 && fast_clock_was_enabled && !clock_downgraded) {
            spi_set_slow_clock();
            spi_exchange(0xFF);
            clock_downgraded = true;
        }

        uint8_t response = 0xFF;
        if (!sd_send_cmd(17, sector, &response)) {
            spi_wait_ready();
            continue;
        }

        if (response != 0x00) {
            spi_deselect();
            spi_exchange(0xFF);
            spi_wait_ready();
            continue;
        }

        if (!sd_read_data_block_split(buf1, count1, buf2)) {
            spi_deselect();
            spi_exchange(0xFF);
            spi_wait_ready();
            continue;
        }

        spi_deselect();
        if (clock_downgraded && fast_clock_was_enabled) {
            spi_set_fast_clock();
        }
        return true;
    }

    if (clock_downgraded && fast_clock_was_enabled) {
        spi_set_fast_clock();
    }

    return false;
}

static bool sd_read_multiple_sectors(uint32_t start_sector, uint8_t* buffer, uint16_t num_sectors) {
    if (!s_card_block_addressing) {
        return false;
    }

    if (num_sectors == 0) {
        return true;
    }

    uint8_t response;
    if (!sd_send_cmd(18, start_sector, &response) || response != 0x00) {
        spi_deselect();
        return false;
    }

    for (uint16_t i = 0; i < num_sectors; ++i) {
        if (!sd_read_data_block(buffer + (i * SECTOR_SIZE))) {
            spi_deselect();
            return false;
        }
    }

    // Stop transmission (CMD12)
    spi_exchange(0xFF);  // Ncr
    spi_exchange(0x40 | 12);
    spi_exchange(0x00);
    spi_exchange(0x00);
    spi_exchange(0x00);
    spi_exchange(0x00);
    spi_exchange(0x01);

    for (int i = 0; i < 10; ++i) {
        uint8_t stop = spi_exchange(0xFF);
        if ((stop & 0x80) == 0) {
            break;
        }
    }

    spi_wait_ready();
    spi_deselect();
    return true;
}


//-----------------------------------------------------------------------------
// FAT32 utility functions
//-----------------------------------------------------------------------------

static uint32_t read_uint32(uint8_t* buf, uint16_t offset) {
    return (uint32_t)buf[offset] | ((uint32_t)buf[offset+1] << 8) |
           ((uint32_t)buf[offset+2] << 16) | ((uint32_t)buf[offset+3] << 24);
}

static uint16_t read_uint16(uint8_t* buf, uint16_t offset) {
    return buf[offset] | (buf[offset+1] << 8);
}

static uint32_t get_next_cluster(uint32_t cluster) {
    // cluster*4 is shift by 2, /512 is shift by 9, net = shift right 7
    // %512 is just low 9 bits after the *4
    uint32_t fat_sector = g_fat_start_sector + (cluster >> 7);
    uint16_t fat_offset = (cluster << 2) & 0x1FF;

    if (!sd_read_sector(fat_sector, sector_buf)) return 0;

    uint32_t next = read_uint32(sector_buf, fat_offset) & 0x0FFFFFFF;
    return (next >= 0x0FFFFFF8) ? 0 : next;
}

static uint32_t cluster_to_sector(uint32_t cluster) {
    // sectors_per_cluster is always power of 2 in FAT32, so use shift
    return g_data_start_sector + ((cluster - 2) << g_spc_shift);
}

// Calculate the FAT LFN checksum for an 8.3 short name (11 bytes)
static inline uint8_t calc_lfn_checksum(const uint8_t* sfn) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i];
    }
    return sum;
}

//-----------------------------------------------------------------------------
// Public API functions
//-----------------------------------------------------------------------------

bool fat32_init(void) {
    if (!sd_init()) return false;

    if (!sd_read_sector(0, sector_buf)) {
        return false;
    }

    uint8_t partition_type = sector_buf[0x1BE + 4];
    uint32_t partition_start = read_uint32(sector_buf, 0x1BE + 8);
    g_last_partition_start = partition_start;

    bool superfloppy = (partition_type == 0 || partition_start == 0);

    if (!superfloppy && partition_start >= (1UL << 31)) {
        superfloppy = true;
        partition_start = 0;
    }

    if (superfloppy) {
        // sector_buf already contains sector 0 from MBR read, no need to re-read
    } else {
        if (!sd_read_sector(partition_start, sector_buf)) {
            if (!sd_read_sector(0, sector_buf)) {
                return false;
            }
            superfloppy = true;
            partition_start = 0;
        }
    }

    if (sector_buf[510] != 0x55 || sector_buf[511] != 0xAA) {
        return false;
    }

    uint16_t reserved_sectors = read_uint16(sector_buf, 14);
    uint8_t num_fats = sector_buf[16];
    uint32_t sectors_per_fat = read_uint32(sector_buf, 36);
    g_sectors_per_cluster = sector_buf[13];
    g_root_dir_cluster = read_uint32(sector_buf, 44);

    // Calculate log2(sectors_per_cluster) for fast multiply via shift
    // FAT32 spec requires this to be a power of 2 (1,2,4,8,16,32,64,128)
    g_spc_shift = 0;
    for (uint8_t spc = g_sectors_per_cluster; spc > 1; spc >>= 1) {
        g_spc_shift++;
    }

    g_fat_start_sector = partition_start + reserved_sectors;
    g_data_start_sector = g_fat_start_sector + (num_fats * sectors_per_fat);

    return true;
}

// Helper function to search for a directory or file entry in the current directory cluster
static bool find_entry(uint32_t dir_cluster, const char* name, bool is_directory, 
                       uint32_t* out_cluster, uint32_t* out_size, uint16_t* entry_offset) {
    // Buffer to reconstruct long filename
    char lfn_buffer[256] = {0};
    int lfn_length = 0;
    bool lfn_in_progress = false;
    uint8_t lfn_max_seq = 0;
    uint8_t lfn_checksum = 0;

    while (dir_cluster != 0) {
        uint32_t sector = cluster_to_sector(dir_cluster);

        for (uint8_t s = 0; s < g_sectors_per_cluster; s++) {
            if (!sd_read_sector(sector + s, sector_buf)) {
                return false;
            }

            for (uint16_t entry = 0; entry < SECTOR_SIZE; entry += DIR_ENTRY_SIZE) {
                // Check if entry is free
                if (sector_buf[entry] == 0) {
                    return false;  // End of directory
                }
                if (sector_buf[entry] == 0xE5) {
                    // Deleted entry - reset LFN parsing
                    lfn_in_progress = false;
                    lfn_length = 0;
                    lfn_max_seq = 0;
                    continue;
                }
                
                // Check if this is a Long File Name entry
                if ((sector_buf[entry + 11] & 0x0F) == 0x0F) {
                    uint8_t seq = sector_buf[entry] & 0x1F;
                    bool is_last = (sector_buf[entry] & 0x40) != 0;
                    uint8_t type = sector_buf[entry + 12];
                    uint8_t chksum = sector_buf[entry + 13];

                    if (type != 0 || seq == 0) {
                        lfn_in_progress = false;
                        lfn_length = 0;
                        lfn_max_seq = 0;
                        continue;
                    }

                    if (is_last) {
                        memset(lfn_buffer, 0, sizeof(lfn_buffer));
                        lfn_length = 0;
                        lfn_in_progress = true;
                        lfn_max_seq = seq;
                        lfn_checksum = chksum;
                    } else if (!lfn_in_progress || seq > lfn_max_seq) {
                        lfn_in_progress = false;
                        lfn_length = 0;
                        lfn_max_seq = 0;
                        continue;
                    }

                    if (lfn_in_progress) {
                        int lfn_pos = (seq - 1) * 13;
                        int char_count = 0;

                        for (int i = 1; i < 11; i += 2) {
                            uint16_t uc = sector_buf[entry + i] | (sector_buf[entry + i + 1] << 8);
                            if (uc == 0x0000) break;
                            if (uc == 0xFFFF) continue;
                            if (lfn_pos + char_count < 255 && uc < 128) {
                                lfn_buffer[lfn_pos + char_count] = (char)uc;
                            }
                            char_count++;
                        }

                        for (int i = 14; i < 26; i += 2) {
                            uint16_t uc = sector_buf[entry + i] | (sector_buf[entry + i + 1] << 8);
                            if (uc == 0x0000) break;
                            if (uc == 0xFFFF) continue;
                            if (lfn_pos + char_count < 255 && uc < 128) {
                                lfn_buffer[lfn_pos + char_count] = (char)uc;
                            }
                            char_count++;
                        }

                        for (int i = 28; i < 32; i += 2) {
                            uint16_t uc = sector_buf[entry + i] | (sector_buf[entry + i + 1] << 8);
                            if (uc == 0x0000) break;
                            if (uc == 0xFFFF) continue;
                            if (lfn_pos + char_count < 255 && uc < 128) {
                                lfn_buffer[lfn_pos + char_count] = (char)uc;
                            }
                            char_count++;
                        }

                        int entry_end = lfn_pos + char_count;
                        if (entry_end > lfn_length) {
                            lfn_length = entry_end;
                        }
                    }
                    continue;
                }
                
                // Regular directory entry
                uint8_t attr = sector_buf[entry + 11];
                if (attr & 0x08) {
                    // Volume label - skip and reset LFN state
                    lfn_in_progress = false;
                    lfn_length = 0;
                    lfn_max_seq = 0;
                    continue;
                }

                // Compare filename
                bool match = false;
                if (lfn_in_progress && lfn_length > 0) {
                    uint8_t sfn_checksum = calc_lfn_checksum(&sector_buf[entry]);
                    if (sfn_checksum == lfn_checksum) {
                        lfn_buffer[lfn_length] = '\0';
                        match = (strcmp(lfn_buffer, name) == 0);
                    }
                }

                if (!match) {
                    // Compare with 8.3 short name
                    char fat_name[11];
                    memset(fat_name, ' ', 11);
                    
                    const char* dot = strchr(name, '.');
                    int name_len = dot ? (dot - name) : strlen(name);
                    if (name_len > 8) name_len = 8;
                    
                    for (int i = 0; i < name_len; i++) {
                        fat_name[i] = (name[i] >= 'a' && name[i] <= 'z') ?
                                      name[i] - 32 : name[i];
                    }
                    
                    if (dot) {
                        int ext_len = strlen(dot + 1);
                        if (ext_len > 3) ext_len = 3;
                        for (int i = 0; i < ext_len; i++) {
                            fat_name[8 + i] = (dot[1 + i] >= 'a' && dot[1 + i] <= 'z') ?
                                              dot[1 + i] - 32 : dot[1 + i];
                        }
                    }
                    
                    match = (memcmp(&sector_buf[entry], fat_name, 11) == 0);
                }
                
                if (match) {
                    // Check if type matches what we're looking for
                    bool is_dir = (attr & 0x10) != 0;
                    if (is_dir != is_directory) {
                        // Type mismatch - keep searching
                        lfn_in_progress = false;
                        lfn_length = 0;
                        lfn_max_seq = 0;
                        continue;
                    }

                    // Found it!
                    uint16_t cluster_low = read_uint16(sector_buf, entry + 26);
                    uint16_t cluster_high = read_uint16(sector_buf, entry + 20);
                    *out_cluster = cluster_low | ((uint32_t)cluster_high << 16);
                    *out_size = read_uint32(sector_buf, entry + 28);
                    if (entry_offset) *entry_offset = entry;
                    return true;
                }
                
                // Reset LFN state after processing this directory entry
                lfn_in_progress = false;
                lfn_length = 0;
                lfn_max_seq = 0;
            }
        }

        dir_cluster = get_next_cluster(dir_cluster);
    }

    return false;
}

bool fat32_open(fat32_file_t* file, const char* filename) {
    // Parse path: skip optional drive number, split into directory components
    const char* path = filename;
    
    // Skip drive number if present (e.g., "0:")
    if (path[0] != '\0' && path[1] == ':') {
        path += 2;
    }
    
    // Start at root directory
    uint32_t dir_cluster = g_root_dir_cluster;
    
    // Parse path components
    char component[256];
    while (*path != '\0') {
        // Extract next path component
        const char* slash = strchr(path, '/');
        int len;
        if (slash) {
            len = slash - path;
            if (len >= 256) len = 255;
        } else {
            len = strlen(path);
            if (len >= 256) len = 255;
        }
        
        memcpy(component, path, len);
        component[len] = '\0';
        
        if (len == 0) {
            // Empty component (e.g., leading or double slash) - skip
            path = slash + 1;
            continue;
        }
        
        // Is this the last component?
        bool is_last = (slash == NULL);
        
        if (is_last) {
            // This is the filename - search for file
            uint32_t file_cluster;
            uint32_t file_size;
            if (!find_entry(dir_cluster, component, false, &file_cluster, &file_size, NULL)) {
                return false;
            }
            
            // Initialize file structure
            file->first_cluster = file_cluster;
            file->current_cluster = file_cluster;
            file->current_sector = cluster_to_sector(file_cluster);
            file->sector_offset = 0;
            file->file_size = file_size;
            file->bytes_read = 0;
            file->buffer_valid = false;
            file->error_flag = false;
            file->cluster_start_sector = file->current_sector;
            file->sectors_per_cluster = g_sectors_per_cluster;
            file->sector_in_cluster = 0;
            
            return true;
        } else {
            // This is a directory - search for it and navigate into it
            uint32_t next_cluster;
            uint32_t dummy_size;
            if (!find_entry(dir_cluster, component, true, &next_cluster, &dummy_size, NULL)) {
                return false;  // Directory not found
            }
            
            dir_cluster = next_cluster;
            path = slash + 1;
        }
    }
    
    // Empty path
    return false;
}

__attribute__((hot, noinline))
int fat32_read(fat32_file_t* file, uint8_t* buffer, uint16_t bytes) {
    // Check for EOF first - must be done before any read
    if (file->bytes_read >= file->file_size) {
        return 0;
    }
    
    // Limit read to remaining file size
    uint32_t remaining = file->file_size - file->bytes_read;
    if (bytes > remaining) {
        bytes = (uint16_t)remaining;
    }
    
    // Ultra-fast path: valid buffer + small read that fits in remaining sector
    // This path has minimal overhead - critical for streaming small chunks
    if (file->buffer_valid) {
        uint16_t off = file->sector_offset;
        uint16_t avail = SECTOR_SIZE - off;
        if (bytes <= avail) {

            fast_memcpy(buffer, file->sector_buffer + off, bytes);

            off += bytes;
            file->sector_offset = off;
            file->bytes_read += bytes;
            
            // Only do sector advance logic if we hit the boundary
            if (off >= SECTOR_SIZE) {
                file->sector_offset = 0;
                file->buffer_valid = false;
                file->current_sector++;
                uint8_t sic = file->sector_in_cluster + 1;
                file->sector_in_cluster = sic;
                if (sic >= file->sectors_per_cluster) {
                    uint32_t next = get_next_cluster(file->current_cluster);
                    if (next == 0) return bytes;
                    file->current_cluster = next;
                    file->current_sector = cluster_to_sector(next);
                    file->sector_in_cluster = 0;
                }
            }
            return bytes;
        }
    }

    // Standard path (buffer not valid or read spans sectors)
    file->error_flag = false;
    if (bytes == 0) {
        return 0;
    }

    // Small-chunk path for <=256 bytes when buffer isn't valid or doesn't have enough
    if (bytes <= 256) {
        // If at sector start, read to cache and copy
        if (file->sector_offset == 0) {
            if (!sd_read_sector(file->current_sector, file->sector_buffer)) {
                file->error_flag = true;
                return 0;
            }
            file->buffer_valid = true;

            fast_memcpy_asm(buffer, file->sector_buffer, bytes);

            file->sector_offset = bytes;
            file->bytes_read += bytes;
            return bytes;
        }

    }

    // General path: cache hot fields in locals to reduce repeated struct traffic
    uint8_t spc = file->sectors_per_cluster;
    uint8_t sic = file->sector_in_cluster;
    uint16_t sector_offset = file->sector_offset;
    uint32_t cur_sector = file->current_sector;
    uint32_t cur_cluster = file->current_cluster;
    uint32_t bytes_read = file->bytes_read;
    bool buffer_valid = file->buffer_valid;

    uint16_t done = 0;

    while (done < bytes) {
        uint16_t remaining_this_call = bytes - done;

        // Fast path: aligned, whole sectors available
        if ((sector_offset == 0) && (remaining_this_call >= SECTOR_SIZE)) {
            uint16_t sectors_to_read = remaining_this_call >> 9; // /512
            uint8_t sectors_left_in_cluster = (uint8_t)(spc - sic);
            if (sectors_to_read > sectors_left_in_cluster) {
                sectors_to_read = sectors_left_in_cluster;
            }

            if (sectors_to_read >= 2) {
                if (!sd_read_multiple_sectors(cur_sector, buffer + done, sectors_to_read)) {
                    file->error_flag = true;
                    break;
                }
            } else {
                if (!sd_read_sector(cur_sector, buffer + done)) {
                    file->error_flag = true;
                    break;
                }
                sectors_to_read = 1;
            }

            uint16_t advanced_bytes = (uint16_t)(sectors_to_read << 9);
            done       += advanced_bytes;
            bytes_read += advanced_bytes;
            cur_sector += sectors_to_read;
            sic        += sectors_to_read;
            buffer_valid = false;

            if (sic >= spc) {
                cur_cluster = get_next_cluster(cur_cluster);
                if (cur_cluster == 0) {
                    break;
                }
                cur_sector = cluster_to_sector(cur_cluster);
                sic = 0;
            }
            continue;
        }

        // Partial-sector path (or tail after an unaligned start)
        if (!buffer_valid) {
            if (!sd_read_sector(cur_sector, file->sector_buffer)) {
                file->error_flag = true;
                break;
            }
            buffer_valid = true;
        }

        uint16_t sector_remaining = SECTOR_SIZE - sector_offset;
        uint16_t to_copy = remaining_this_call;
        if (to_copy > sector_remaining) {
            to_copy = sector_remaining;
        }

        fast_memcpy(buffer + done, file->sector_buffer + sector_offset, to_copy);

        done       += to_copy;
        bytes_read += to_copy;
        sector_offset += to_copy;

        if (sector_offset >= SECTOR_SIZE) {
            sector_offset = 0;
            buffer_valid = false;
            cur_sector++;
            sic++;

            if (sic >= spc) {
                cur_cluster = get_next_cluster(cur_cluster);
                if (cur_cluster == 0) {
                    break;
                }
                cur_sector = cluster_to_sector(cur_cluster);
                sic = 0;
            }
        }
    }

    // Write back cached state
    file->sector_in_cluster = sic;
    file->sector_offset = sector_offset;
    file->current_sector = cur_sector;
    file->current_cluster = cur_cluster;
    file->bytes_read = bytes_read;
    file->buffer_valid = buffer_valid;

    return done;
}

bool fat32_eof(fat32_file_t* file) {
    return (file->bytes_read >= file->file_size) && !file->error_flag;
}

bool fat32_error(fat32_file_t* file) {
    return file->error_flag;
}

void fat32_close(fat32_file_t* file) {
    file->buffer_valid = false;
    spi_disable();
}

char* fat32_gets(char *buffer, int maxlen, fat32_file_t *file) {
    if (maxlen <= 0) return NULL;

    int count = 0;
    while (count < (maxlen - 1)) {
        uint8_t ch;
        int res = fat32_read(file, &ch, 1);
        if (res == 0) {
            break; // EOF or error
        }
        buffer[count++] = (char)ch;
        if (ch == '\n') {
            break; // Newline
        }
    }

    if (count == 0) {
        return NULL; // No data read
    }

    buffer[count] = '\0'; // Null-terminate
    return buffer;
}
