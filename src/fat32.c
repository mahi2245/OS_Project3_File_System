#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat32.h"

static FILE *fp = NULL;
static char *fp_name = NULL;
static BPB bpb;
static long image_size = 0;

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

    fclose(fp);
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