#ifndef FAT32_H
#define FAT32_H

#include <stdio.h>

// FAT32 BPB structure
#pragma pack(push, 1)
typedef struct __attribute__((packed)) {
    unsigned char  BS_jmpBoot[3];
    unsigned char  BS_OEMName[8];
    unsigned short BPB_BytsPerSec;
    unsigned char  BPB_SecPerClus;
    unsigned short BPB_RsvdSecCnt;
    unsigned char  BPB_NumFATs;
    unsigned short BPB_RootEntCnt;
    unsigned short BPB_TotSec16;
    unsigned char  BPB_Media;
    unsigned short BPB_FATSz16;
    unsigned short BPB_SecPerTrk;
    unsigned short BPB_NumHeads;
    unsigned int   BPB_HiddSec;
    unsigned int   BPB_TotSec32;

    // FAT32 extended BPB
    unsigned int   BPB_FATSz32;
    unsigned short BPB_ExtFlags;
    unsigned short BPB_FSVer;
    unsigned int   BPB_RootClus;
    unsigned short BPB_FSInfo;
    unsigned short BPB_BkBootSec;
    unsigned char  BPB_Reserved[12];
    unsigned char  BS_DrvNum;
    unsigned char  BS_Reserved1;
    unsigned char  BS_BootSig;
    unsigned int   BS_VolID;
    unsigned char  BS_VolLab[11];
    unsigned char  BS_FilSysType[8];
} BPB;
#pragma pack(pop)

// FAT32 Directory Entry (short name version)
typedef struct __attribute__((packed)) {
    unsigned char  DIR_Name[11];
    unsigned char  DIR_Attr;
    unsigned char  DIR_NTRes;
    unsigned char  DIR_CrtTimeTenth;
    unsigned short DIR_CrtTime;
    unsigned short DIR_CrtDate;
    unsigned short DIR_LstAccDate;
    unsigned short DIR_FstClusHI;
    unsigned short DIR_WrtTime;
    unsigned short DIR_WrtDate;
    unsigned short DIR_FstClusLO;
    unsigned int   DIR_FileSize;
} DIR_ENTRY;

// for opened files
typedef struct {
    int using;
    char name[12];
    unsigned int cluster;
    unsigned int offset;
    int mode;
} OPEN_FILE;




// helper functions
const char* get_image_name();
const char* get_current_path();
unsigned int cluster_size();
unsigned int first_data_sector();
unsigned int cluster_to_sector(unsigned int cluster);
void read_cluster(unsigned int cluster, unsigned char *buffer);
int is_valid_entry(DIR_ENTRY *entry);
DIR_ENTRY* find_entry(const char *target);
unsigned int get_parent_cluster();

int fat32_mount(const char *filename);
void fat32_unmount();
void info();
void ls();
void cd(char *name);
void creat(char * filename);


#endif