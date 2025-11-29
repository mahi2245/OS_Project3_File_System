#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat32.h"

static FILE *fp = NULL;
static char *fp_name = NULL;
static BPB bpb;
static long image_size = 0;
static unsigned int current_cluster = 0;

// ========================= HELPER FUNCTIONS =========================
unsigned int cluster_size() {
    return bpb.BPB_BytsPerSec * bpb.BPB_SecPerClus;
}

unsigned int first_data_sector() {
    return bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32);
}

unsigned int cluster_to_sector(unsigned int cluster) {
    return first_data_sector() + (cluster - 2) * bpb.BPB_SecPerClus;
}

void read_cluster(unsigned int cluster, unsigned char *buffer) {
    unsigned int sector = cluster_to_sector(cluster);
    unsigned int clusterSize = cluster_size();

    unsigned int offset = sector * bpb.BPB_BytsPerSec;

    fseek(fp, offset, SEEK_SET);
    fread(buffer, clusterSize, 1, fp);
}


// ========================= MAIN FUNCTIONS =========================
// mount image
int fat32_mount(const char *filename) {
    if((fp = fopen(filename, "rb")) == NULL) {
        return -1;
    }

    fp_name = strdup(filename);
    if (!fp_name) {
        fclose(fp);
        fp = NULL;
        return -1;
    }

    // read BPB
    fseek(fp, 0, SEEK_END);
    image_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    fread(&bpb, sizeof(BPB), 1, fp);

    // set current directory to root
    current_cluster = bpb.BPB_RootClus;

    return 0;
}

// unmount image
void fat32_unmount() {
    if (fp) {
        fclose(fp);
        fp = NULL;
    }

    if (fp_name) {
        free(fp_name);
        fp_name = NULL;
    }
}

// return image name
const char* get_image_name() {
    return fp_name;
}

// calls info function
void info() {
    // position of root cluster
    printf("Root cluster: %u\n", bpb.BPB_RootClus);

    // bytes per sector
    printf("Bytes per sector: %u\n", bpb.BPB_BytsPerSec);

    // sectors per cluster
    printf("Sectors per cluster: %u\n", bpb.BPB_SecPerClus);

    // total # of clusters in data region
    int dataSectors = bpb.BPB_TotSec32 - (bpb.BPB_RsvdSecCnt + bpb.BPB_NumFATs * bpb.BPB_FATSz32);
    int totalClusters = dataSectors / bpb.BPB_SecPerClus;
    printf("Total clusters in data region: %u\n", totalClusters);

    // # of entries in one FAT
    unsigned int entriesPerFAT = (bpb.BPB_FATSz32 * bpb.BPB_BytsPerSec) / 4;
    printf("# of entries in one FAT: %u\n", entriesPerFAT);

    // size of image (in bytes)
    printf("Size of image (bytes): %ld\n", image_size);

}

// calls ls function
void ls() {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        printf("Error: could not allocate memory for ls.\n");
        return;
    }

    read_cluster(current_cluster, buffer);

    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / sizeof(DIR_ENTRY);

    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00) { // entry is not being used
            continue;
        }
        if (entries[i].DIR_Attr == 0x0F) { // long filename entry
            continue;
        }
        if (entries[i].DIR_Name[0] == 0xE5) { // deleted entry
            continue;
        }

        char name[12];
        memcpy(name, entries[i].DIR_Name, 11);
        name[11] = '\0';
        printf("%s\n", name);
    }

    free(buffer);
}