#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fat32.h"

static FILE *fp = NULL;
static char *fp_name = NULL;
static BPB bpb;
static long image_size = 0;
static unsigned int current_cluster = 0;
static char current_path[256] = "/";

// ========================= HELPER FUNCTIONS =========================
const char* get_image_name() {
    return fp_name;
}

const char* get_current_path() {
    return current_path;
}

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

int is_valid_entry(DIR_ENTRY *entry) {
    if (entry->DIR_Name[0] == 0x00) { // entry is not being used
        return 0;
    }
    if (entry->DIR_Attr == 0x0F) { // long filename entry
        return 0;
    }
    if (entry->DIR_Name[0] == 0xE5) { // deleted entry
        return 0;
    }
    return 1;
}

DIR_ENTRY* find_entry(const char *target) {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        return NULL;
    }

    read_cluster(current_cluster, buffer);

    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / sizeof(DIR_ENTRY);

    static DIR_ENTRY result;

    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;

        char name[12];
        memcpy(name, entries[i].DIR_Name, 11);
        name[11] = '\0';

        for (int j = 10; j >= 0; j--) {
            if (name[j] == ' ') name[j] = '\0';
            else break;
        }

        if (strcmp(name, target) == 0) {
            memcpy(&result, &entries[i], sizeof(DIR_ENTRY));
            free(buffer);
            return &result;
        }
    }

    free(buffer);
    return NULL;
}

unsigned int get_parent_cluster() {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        return 0;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    DIR_ENTRY *parent_entry = &entries[1];
    unsigned int parent_cluster = ((unsigned int) parent_entry->DIR_FstClusHI << 16) | parent_entry->DIR_FstClusLO;

    free(buffer);
    if (parent_cluster == 0) {
        return bpb.BPB_RootClus;
    }
    return parent_cluster;
}


// ========================= MAIN FUNCTIONS =========================
// mount image
int fat32_mount(const char *filename) {
    if((fp = fopen(filename, "rb+")) == NULL) {
        return -1;
    }

    fp_name = malloc(strlen(filename) + 1);
    if (!fp_name) {
        fclose(fp);
        fp = NULL;
        return -1;
    }
    strcpy(fp_name, filename);

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
        if (!is_valid_entry(&entries[i])) continue;

        char name[12];
        memcpy(name, entries[i].DIR_Name, 11);
        name[11] = '\0';
        printf("%s\n", name);
    }

    free(buffer);
}

// calls cd function
void cd(char *name) {
    if (name == NULL) {
        printf("Error: cd needs a directory name.\n");
        return;
    }

    if (strcmp(name, "..") == 0) {
        if (current_cluster == bpb.BPB_RootClus) {
            return;
        }

        unsigned int parent = get_parent_cluster();
        current_cluster = parent;

        int len = strlen(current_path);
        if (len > 1 && current_path[len-1] == '/') {
            current_path[len-1] = '\0';
            len--;
        }

        for (int i = len - 1; i >= 0; i--) {
            if (current_path[i] == '/') {
                current_path[i+1] = '\0';
                break;
            }
        }

        return;
    }

    DIR_ENTRY *e = find_entry(name);
    if (e == NULL) {
        printf("Error: directory not found.\n");
        return;
    }

    if ((e->DIR_Attr & 0x10) == 0) {
        printf("Error: %s is not a directory.\n", name);
        return;
    }

    unsigned int clus = ((unsigned int)e->DIR_FstClusHI << 16) | e->DIR_FstClusLO;

    if (clus == 0) {
        printf("Error: invalid directory.\n");
        return;
    }

    current_cluster = clus;
    strcat(current_path, name);
    strcat(current_path, "/");
}

void mkdir(char * dirname) {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        printf("Error: could not allocate memory for ls.\n");
        return;
    }
    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;

    //create the short name
    char short_dirname[11];
    for (int i = 0; i < 11; i++) {
        short_dirname[i] = ' ';
    }
    int i = 0;
    // does this need length checking? maybe
    while (dirname[i] != '\0') {
        short_dirname[i] = toupper(dirname[i]);
        i+=1;
    }

 int name_exists = 0;
    int num = size / sizeof(DIR_ENTRY);
    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00) { 
            break;    
        }
        if (memcmp(short_dirname, entries[i].DIR_Name, 11) == 0) {  
            printf("error, filename already exists here");
            name_exists = 1;
            break;
        }
    }
    if (name_exists == 1) {
        // do nothing
    } 
    else {


    }



}

void creat(char * filename) {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        printf("Error: could not allocate memory for ls.\n");
        return;
    }
    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;

    //create the short name
    char short_filename[11];
    for (int i = 0; i < 11; i++) {
        short_filename[i] = ' ';
    }
    int i = 0;
    // does this need length checking? maybe
    while (filename[i] != '\0') {
        short_filename[i] = toupper(filename[i]);
        i+=1;
    }

    int file_exists = 0;
    int num = size / sizeof(DIR_ENTRY);
    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00) { 
            break;    
        }
        if (memcmp(short_filename, entries[i].DIR_Name, 11) == 0) {  
            printf("error, filename already exists here");
            file_exists = 1;
            break;
        }
    }
    if (file_exists == 1) {
        // do nothing
    } 
    else {
        for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00) { 
            DIR_ENTRY *entry = &entries[i];
            memcpy(entry->DIR_Name, short_filename, 11);
            entry->DIR_Attr = 0x20;

            entry->DIR_FstClusHI = 0;
            entry->DIR_FstClusLO = 0;
            entry->DIR_FileSize = 0;

            unsigned int sector = cluster_to_sector(current_cluster);
            unsigned int offset = sector * bpb.BPB_BytsPerSec;
            fseek(fp, offset, SEEK_SET);
            fwrite(buffer, size, 1, fp);
            

            break;
        }
    }
    }
}
