#ifndef FAT32_STREAM_H
#define FAT32_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// SPI Interface registers
#define SPI_CTRL_REG    0xDD00
#define SPI_DATA_REG    0xDD01

// SPI Control bits
#define SPI_CTRL_SELECT_SDCARD 0x01
#define SPI_CTRL_SELECT_MASK   0x01
#define SPI_CTRL_SLOWCLK       0x02
#define SPI_CTRL_AUTOTX        0x08
#define SPI_CTRL_BUSY          0x80

// FAT32 constants
#define SECTOR_SIZE     512
#define MAX_FILENAME    11  // 8.3 format
#define DIR_ENTRY_SIZE  32

// File stream structure
typedef struct {
    uint8_t sector_buffer[SECTOR_SIZE];
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t current_sector;
    uint16_t sector_offset;
    uint32_t file_size;
    uint32_t bytes_read;
    bool buffer_valid;
    bool error_flag;       // Set when I/O error occurs
    // Cached values for performance optimization
    uint32_t cluster_start_sector;  // Cached cluster_to_sector(current_cluster)
    uint8_t sectors_per_cluster;    // Cached g_sectors_per_cluster
    uint8_t sector_in_cluster;      // Current sector index within cluster (0 to sectors_per_cluster-1)
} fat32_file_t;

// Global FAT32 info (populated during init)
extern uint32_t g_fat_start_sector;
extern uint32_t g_data_start_sector;
extern uint32_t g_root_dir_cluster;
extern uint8_t g_sectors_per_cluster;
extern uint32_t g_last_partition_start;

// Streaming state globals (zero-page for maximum performance)
extern uint8_t* g_stream_buffer_ptr;
extern uint16_t g_stream_offset;
extern uint16_t g_stream_avail;

// Function prototypes
bool fat32_init(void);
bool fat32_open(fat32_file_t* file, const char* filename);
int fat32_read(fat32_file_t* file, uint8_t* buffer, uint16_t bytes);
bool fat32_eof(fat32_file_t* file);
bool fat32_error(fat32_file_t* file);
void fat32_close(fat32_file_t* file);
char* fat32_gets(char *buffer, int maxlen, fat32_file_t *file);
void fat32_stream_begin(fat32_file_t* file);
bool fat32_stream_refill(fat32_file_t* file);
void fat32_stream_end(fat32_file_t* file);
uint8_t fat32_read_tiny_asm(uint8_t* dest, uint8_t count);

#endif // FAT32_STREAM_H

