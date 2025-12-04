#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fat32.h"

#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define FAT32_EOC      0x0FFFFFFF

static FILE *fp = NULL;
static char *fp_name = NULL;
static BPB bpb;
static long image_size = 0;
static unsigned int current_cluster = 0;
static char current_path[256] = "/";

OPEN_FILE open_files_table[10];

static unsigned int fat_start_off = 0;

//helpers

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

static void make_short_name(const char *src, unsigned char dest[11]) {
    for (int i = 0; i < 11; i++) {
        dest[i] = ' ';
    }
    int i = 0;
    while (src[i] != '\0' && i < 11) {
        dest[i] = (unsigned char)toupper((unsigned char)src[i]);
        i++;
    }
}

//fat helpers

static unsigned int fat_get(unsigned int cluster) {
    unsigned int val;
    unsigned int offset = fat_start_off + (cluster * 4);

    fseek(fp, offset, SEEK_SET);
    fread(&val, 4, 1, fp);

    return val & 0x0FFFFFFF;
}

static void write_cluster(unsigned int cluster, unsigned int next) {
    // write to all FAT copies
    for (int i = 0; i < bpb.BPB_NumFATs; i++) {
        unsigned int fat_base_off =
            fat_start_off + (i * bpb.BPB_FATSz32 * bpb.BPB_BytsPerSec);
        unsigned int off = fat_base_off + (cluster * 4);
        fseek(fp, off, SEEK_SET);
        fwrite(&next, 4, 1, fp);
    }
}

static void fat_free_chain(unsigned int start) {
    unsigned int cluster = start;

    while (cluster >= 2) {
        unsigned int next = fat_get(cluster);
        // mark as free
        write_cluster(cluster, 0x00000000);

        if (next == 0 || next >= 0x0FFFFFF8) {
            break;
        }
        cluster = next;
    }
}

// find a free FAT entry (cluster >= 2), returns 0 if none
unsigned int find_new_cluster() {
    unsigned int bytes_per_fat = bpb.BPB_FATSz32 * bpb.BPB_BytsPerSec;
    unsigned int entries = bytes_per_fat / 4;

    for (unsigned int i = 2; i < entries; i++) {
        unsigned int offset = fat_start_off + (i * 4);
        unsigned int val;
        fseek(fp, offset, SEEK_SET);
        fread(&val, 4, 1, fp);

        if ((val & 0x0FFFFFFF) == 0) {
            return i;
        }
    }
    return 0;
}

//directory helpers

int is_valid_entry(DIR_ENTRY *entry) {
    if (entry->DIR_Name[0] == 0x00) {
        return 0;
    }
    if (entry->DIR_Attr == 0x0F) { 
        return 0;
    }
    if (entry->DIR_Name[0] == 0xE5) { 
        return 0;
    }
    if (entry->DIR_Name[0] == 0x5E) { 
        return 0;
    }
    return 1;
}

DIR_ENTRY* find_entry(const char *target) {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) return NULL;

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);

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
    if (!buffer) return 0;

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    DIR_ENTRY *parent_entry = &entries[1]; 
    unsigned int parent_cluster =
        ((unsigned int) parent_entry->DIR_FstClusHI << 16) |
         parent_entry->DIR_FstClusLO;

    free(buffer);
    if (parent_cluster == 0) {
        return bpb.BPB_RootClus;
    }
    return parent_cluster;
}

//mount

int fat32_mount(const char *filename) {
    if ((fp = fopen(filename, "rb+")) == NULL) {
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
    fat_start_off = bpb.BPB_RsvdSecCnt * bpb.BPB_BytsPerSec;

    // initialize open file table
    for (int i = 0; i < 10; i++) {
        open_files_table[i].using = 0;
        memset(open_files_table[i].name, 0, sizeof(open_files_table[i].name));
        open_files_table[i].cluster = 0;
        open_files_table[i].offset = 0;
        open_files_table[i].mode = 0;
        memset(open_files_table[i].path, 0, sizeof(open_files_table[i].path));
    }

    return 0;
}

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

//commands

void info() {
    printf("Root cluster: %u\n", bpb.BPB_RootClus);
    printf("Bytes per sector: %u\n", bpb.BPB_BytsPerSec);
    printf("Sectors per cluster: %u\n", bpb.BPB_SecPerClus);

    int dataSectors =
        (int)bpb.BPB_TotSec32 -
        (int)(bpb.BPB_RsvdSecCnt + bpb.BPB_NumFATs * bpb.BPB_FATSz32);
    int totalClusters = dataSectors / bpb.BPB_SecPerClus;
    printf("Total clusters in data region: %u\n", totalClusters);

    unsigned int entriesPerFAT =
        (bpb.BPB_FATSz32 * bpb.BPB_BytsPerSec) / 4;
    printf("# of entries in one FAT: %u\n", entriesPerFAT);

    printf("Size of image (bytes): %ld\n", image_size);
}

void ls() {
    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for ls.\n");
        return;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);

    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;

        char name[12];
        memcpy(name, entries[i].DIR_Name, 11);
        name[11] = '\0';

        for (int j = 10; j >= 0; j--) {
            if (name[j] == ' ') name[j] = '\0';
            else break;
        }

        printf("%s\n", name);
    }

    free(buffer);
}

void cd(char *name) {
    if (!name) {
        printf("Error: cd needs a directory name.\n");
        return;
    }

    if (strcmp(name, "..") == 0) {
        if (current_cluster == bpb.BPB_RootClus) {
            return;
        }

        unsigned int parent = get_parent_cluster();
        current_cluster = parent;

        int len = (int)strlen(current_path);
        if (len > 1 && current_path[len - 1] == '/') {
            current_path[len - 1] = '\0';
            len--;
        }

        for (int i = len - 1; i >= 0; i--) {
            if (current_path[i] == '/') {
                current_path[i + 1] = '\0';
                break;
            }
        }
        return;
    }

    DIR_ENTRY *e = find_entry(name);
    if (!e) {
        printf("Error: directory not found.\n");
        return;
    }

    if ((e->DIR_Attr & ATTR_DIRECTORY) == 0) {
        printf("Error: %s is not a directory.\n", name);
        return;
    }

    unsigned int clus =
        ((unsigned int)e->DIR_FstClusHI << 16) | e->DIR_FstClusLO;

    if (clus == 0) {
        printf("Error: invalid directory.\n");
        return;
    }

    current_cluster = clus;
    strcat(current_path, name);
    strcat(current_path, "/");
}

void mkdir(char *dirname) {
    if (!dirname) {
        printf("Error: mkdir needs a name.\n");
        return;
    }

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for mkdir.\n");
        return;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;

    unsigned char short_dirname[11];
    make_short_name(dirname, short_dirname);

    int num = size / (int)sizeof(DIR_ENTRY);
    int name_exists = 0;
    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00) break;
        if (memcmp(short_dirname, entries[i].DIR_Name, 11) == 0) {
            printf("Error: name already exists in directory.\n");
            name_exists = 1;
            break;
        }
    }

    if (name_exists) {
        free(buffer);
        return;
    }

    int free_index = -1;
    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00 ||
            entries[i].DIR_Name[0] == 0x5E ||
            entries[i].DIR_Name[0] == 0xE5) {
            free_index = i;
            break;
        }
    }

    if (free_index < 0) {
        printf("Error: no space in directory.\n");
        free(buffer);
        return;
    }

    DIR_ENTRY *entry = &entries[free_index];
    memcpy(entry->DIR_Name, short_dirname, 11);
    entry->DIR_Attr = ATTR_DIRECTORY;

    unsigned int my_cluster = find_new_cluster();
    if (my_cluster == 0) {
        printf("Error: no free clusters for directory.\n");
        free(buffer);
        return;
    }
    write_cluster(my_cluster, FAT32_EOC);

    entry->DIR_FstClusHI = (unsigned short)(my_cluster >> 16);
    entry->DIR_FstClusLO = (unsigned short)(my_cluster & 0xFFFF);
    entry->DIR_FileSize  = 0;

    unsigned int sector2 = cluster_to_sector(current_cluster);
    unsigned int offset2 = sector2 * bpb.BPB_BytsPerSec;
    fseek(fp, offset2, SEEK_SET);
    fwrite(buffer, size, 1, fp);
    free(buffer);

    unsigned int size2 = cluster_size();
    unsigned char *buffer2 = calloc(1, size2);
    DIR_ENTRY *entries2 = (DIR_ENTRY *)buffer2;

    unsigned char dot[11];
    memset(dot, ' ', 11);
    dot[0] = '.';

    DIR_ENTRY *entry2 = &entries2[0];
    memcpy(entry2->DIR_Name, dot, 11);
    entry2->DIR_Attr      = ATTR_DIRECTORY;
    entry2->DIR_FstClusHI = (unsigned short)(my_cluster >> 16);
    entry2->DIR_FstClusLO = (unsigned short)(my_cluster & 0xFFFF);
    entry2->DIR_FileSize  = 0;

    unsigned char dot2[11];
    memset(dot2, ' ', 11);
    dot2[0] = '.';
    dot2[1] = '.';

    DIR_ENTRY *entry3 = &entries2[1];
    memcpy(entry3->DIR_Name, dot2, 11);
    entry3->DIR_Attr      = ATTR_DIRECTORY;
    entry3->DIR_FstClusHI = (unsigned short)(current_cluster >> 16);
    entry3->DIR_FstClusLO = (unsigned short)(current_cluster & 0xFFFF);
    entry3->DIR_FileSize  = 0;

    unsigned int sector3 = cluster_to_sector(my_cluster);
    unsigned int offset3 = sector3 * bpb.BPB_BytsPerSec;
    fseek(fp, offset3, SEEK_SET);
    fwrite(buffer2, size2, 1, fp);
    free(buffer2);
}

void creat(char *filename) {
    if (!filename) {
        printf("Error: creat needs a filename.\n");
        return;
    }

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for creat.\n");
        return;
    }

    unsigned char short_filename[11];
    make_short_name(filename, short_filename);

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);

    int file_exists = 0;
    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00) break;
        if (memcmp(short_filename, entries[i].DIR_Name, 11) == 0) {
            printf("Error: filename already exists here.\n");
            file_exists = 1;
            break;
        }
    }

    if (file_exists) {
        free(buffer);
        return;
    }

    int free_index = -1;
    for (int i = 0; i < num; i++) {
        if (entries[i].DIR_Name[0] == 0x00 ||
            entries[i].DIR_Name[0] == 0x5E ||
            entries[i].DIR_Name[0] == 0xE5) {
            free_index = i;
            break;
        }
    }

    if (free_index < 0) {
        printf("Error: no space in directory.\n");
        free(buffer);
        return;
    }

    DIR_ENTRY *entry = &entries[free_index];
    memcpy(entry->DIR_Name, short_filename, 11);
    entry->DIR_Attr = ATTR_ARCHIVE;
    entry->DIR_FstClusHI = 0;
    entry->DIR_FstClusLO = 0;
    entry->DIR_FileSize  = 0;

    unsigned int sector = cluster_to_sector(current_cluster);
    unsigned int offset = sector * bpb.BPB_BytsPerSec;
    fseek(fp, offset, SEEK_SET);
    fwrite(buffer, size, 1, fp);
    free(buffer);
}

void open(char *filename, char *flags) {
    if (!filename || !flags) {
        printf("Error: open needs filename and flags.\n");
        return;
    }

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for open.\n");
        return;
    }

    unsigned char short_filename[11];
    make_short_name(filename, short_filename);

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    DIR_ENTRY *cur_entry = NULL;
    int num = size / (int)sizeof(DIR_ENTRY);
    int file_exists = 0;

    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;
        if (memcmp(short_filename, entries[i].DIR_Name, 11) == 0) {
            cur_entry = &entries[i];
            file_exists = 1;
            break;
        }
    }

    if (!file_exists) {
        printf("Error: file does not exist.\n");
        free(buffer);
        return;
    }

    if (cur_entry->DIR_Attr & ATTR_DIRECTORY) {
        printf("Error: cannot open a directory.\n");
        free(buffer);
        return;
    }

    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            memcmp(open_files_table[i].name, short_filename, 11) == 0) {
            printf("Error: file already open.\n");
            free(buffer);
            return;
        }
    }

    int idx = -1;
    for (int i = 0; i < 10; i++) {
        if (!open_files_table[i].using) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        printf("Error: open file table full.\n");
        free(buffer);
        return;
    }

    open_files_table[idx].using = 1;
    memcpy(open_files_table[idx].name, short_filename, 11);
    open_files_table[idx].name[11] = '\0';

    unsigned int hi = cur_entry->DIR_FstClusHI;
    unsigned int lo = cur_entry->DIR_FstClusLO;
    open_files_table[idx].cluster = (hi << 16) | lo;
    open_files_table[idx].offset = 0;

    if (strcmp(flags, "-r") == 0) {
        open_files_table[idx].mode = 0;
    } else if (strcmp(flags, "-w") == 0) {
        open_files_table[idx].mode = 1;
    } else if (strcmp(flags, "-rw") == 0 || strcmp(flags, "-wr") == 0) {
        open_files_table[idx].mode = 2;
    } else {
        printf("Error: invalid mode.\n");
        open_files_table[idx].using = 0;
        free(buffer);
        return;
    }

    // store path at time of open
    strncpy(open_files_table[idx].path, get_current_path(),
            sizeof(open_files_table[idx].path) - 1);
    open_files_table[idx].path[sizeof(open_files_table[idx].path) - 1] = '\0';

    free(buffer);
}

void close(char *filename) {
    if (!filename) {
        printf("Error: close needs filename.\n");
        return;
    }

    unsigned char short_filename[11];
    make_short_name(filename, short_filename);

    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            memcmp(open_files_table[i].name, short_filename, 11) == 0) {
            open_files_table[i].using = 0;
            memset(open_files_table[i].name, 0, sizeof(open_files_table[i].name));
            open_files_table[i].cluster = 0;
            open_files_table[i].offset = 0;
            open_files_table[i].mode = 0;
            memset(open_files_table[i].path, 0, sizeof(open_files_table[i].path));
            return;
        }
    }

    printf("Error: file not open.\n");
}

void lsof() {
    int any = 0;
    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using) {
            any = 1;
            printf("index: %d | ", i);
            printf("name: %s | ", open_files_table[i].name);
            printf("cluster: %u | ", open_files_table[i].cluster);
            printf("mode: %d | ", open_files_table[i].mode);
            printf("offset: %u | ", open_files_table[i].offset);
            printf("Path: %s\n", open_files_table[i].path);
        }
    }
    if (!any) {
        printf("No files are currently open.\n");
    }
}

void lseek(char *filename, unsigned int offset) {
    if (!filename) {
        printf("Error: lseek needs a filename.\n");
        return;
    }

    unsigned char short_filename[11];
    make_short_name(filename, short_filename);

    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            memcmp(open_files_table[i].name, short_filename, 11) == 0) {
            open_files_table[i].offset = offset;
            return;
        }
    }

    printf("Error: file not open.\n");
}

//write and mv

void write_cmd(char *filename, const char *string) {
    if (!filename || !string) {
        printf("Error: write requires a filename and a string.\n");
        return;
    }

    unsigned char short_filename[11];
    make_short_name(filename, short_filename);

    int of_idx = -1;
    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            memcmp(open_files_table[i].name, short_filename, 11) == 0) {
            of_idx = i;
            break;
        }
    }

    if (of_idx < 0) {
        printf("Error: file is not opened.\n");
        return;
    }

    OPEN_FILE *of = &open_files_table[of_idx];
    if (of->mode != 1 && of->mode != 2) {
        printf("Error: file not opened for writing.\n");
        return;
    }

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for write.\n");
        return;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);
    DIR_ENTRY *entry = NULL;

    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;
        if (memcmp(entries[i].DIR_Name, short_filename, 11) == 0) {
            entry = &entries[i];
            break;
        }
    }

    if (!entry) {
        printf("Error: file not found in current directory.\n");
        free(buffer);
        return;
    }

    if (entry->DIR_Attr & ATTR_DIRECTORY) {
        printf("Error: cannot write to a directory.\n");
        free(buffer);
        return;
    }

    unsigned int first_cluster =
        ((unsigned int)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    unsigned int file_size = entry->DIR_FileSize;

    if (first_cluster == 0) {
        unsigned int new_cluster = find_new_cluster();
        if (new_cluster == 0) {
            printf("Error: no free clusters for file data.\n");
            free(buffer);
            return;
        }
        write_cluster(new_cluster, FAT32_EOC);
        first_cluster = new_cluster;

        entry->DIR_FstClusHI = (unsigned short)(new_cluster >> 16);
        entry->DIR_FstClusLO = (unsigned short)(new_cluster & 0xFFFF);
        of->cluster = new_cluster;
    }

    unsigned int clus_size = cluster_size();
    unsigned int offset = of->offset;
    unsigned int len = (unsigned int)strlen(string);
    unsigned int remaining = len;
    unsigned int file_offset_after = offset;

    unsigned int cluster = first_cluster;
    unsigned int cluster_index = offset / clus_size;
    unsigned int in_cluster_offset = offset % clus_size;

    for (unsigned int k = 0; k < cluster_index; k++) {
        unsigned int next = fat_get(cluster);
        if (next >= 0x0FFFFFF8 || next == FAT32_EOC) {
            unsigned int new_cluster = find_new_cluster();
            if (new_cluster == 0) {
                printf("Error: no free clusters while extending file.\n");
                free(buffer);
                return;
            }
            write_cluster(new_cluster, FAT32_EOC);
            write_cluster(cluster, new_cluster);
            cluster = new_cluster;
        } else {
            cluster = next;
        }
    }

    const char *p = string;
    while (remaining > 0) {
        unsigned int sector = cluster_to_sector(cluster);
        unsigned int cluster_byte_offset = sector * bpb.BPB_BytsPerSec;
        unsigned int space = clus_size - in_cluster_offset;
        unsigned int to_write = (remaining < space) ? remaining : space;

        fseek(fp, cluster_byte_offset + in_cluster_offset, SEEK_SET);
        fwrite(p, 1, to_write, fp);

        p += to_write;
        remaining -= to_write;
        file_offset_after += to_write;
        in_cluster_offset = 0;

        if (remaining > 0) {
            unsigned int next = fat_get(cluster);
            if (next >= 0x0FFFFFF8 || next == FAT32_EOC) {
                unsigned int new_cluster = find_new_cluster();
                if (new_cluster == 0) {
                    printf("Error: no free clusters while extending file.\n");
                    free(buffer);
                    return;
                }
                write_cluster(new_cluster, FAT32_EOC);
                write_cluster(cluster, new_cluster);
                cluster = new_cluster;
            } else {
                cluster = next;
            }
        }
    }

    of->offset = file_offset_after;
    if (file_offset_after > file_size) {
        entry->DIR_FileSize = file_offset_after;
    }

    unsigned int sector = cluster_to_sector(current_cluster);
    unsigned int off = sector * bpb.BPB_BytsPerSec;
    fseek(fp, off, SEEK_SET);
    fwrite(buffer, size, 1, fp);

    free(buffer);
}

void mv_cmd(char *src, char *dst) {
    if (!src || !dst) {
        printf("Error: mv requires source and destination.\n");
        return;
    }

    unsigned char src_short[11];
    unsigned char dst_short[11];
    make_short_name(src, src_short);
    make_short_name(dst, dst_short);

    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            memcmp(open_files_table[i].name, src_short, 11) == 0) {
            printf("Error: file must be closed before mv.\n");
            return;
        }
    }

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for mv.\n");
        return;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);

    int src_idx = -1;
    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;
        if (memcmp(entries[i].DIR_Name, src_short, 11) == 0) {
            src_idx = i;
            break;
        }
    }

    if (src_idx < 0) {
        printf("Error: source does not exist.\n");
        free(buffer);
        return;
    }

    DIR_ENTRY *src_entry = &entries[src_idx];

    int dst_idx = -1;
    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;
        if (memcmp(entries[i].DIR_Name, dst_short, 11) == 0) {
            dst_idx = i;
            break;
        }
    }

    if (dst_idx >= 0) {
        // destination exists
        DIR_ENTRY *dst_entry = &entries[dst_idx];
        if (!(dst_entry->DIR_Attr & ATTR_DIRECTORY)) {
            printf("Error: destination is not a directory.\n");
            free(buffer);
            return;
        }

        unsigned int dest_cluster =
            ((unsigned int)dst_entry->DIR_FstClusHI << 16) |
             dst_entry->DIR_FstClusLO;
        if (dest_cluster == 0) {
            printf("Error: invalid destination directory.\n");
            free(buffer);
            return;
        }

        unsigned int dsize = cluster_size();
        unsigned char *dbuf = malloc(dsize);
        if (!dbuf) {
            printf("Error: could not allocate memory for mv dest.\n");
            free(buffer);
            return;
        }

        read_cluster(dest_cluster, dbuf);
        DIR_ENTRY *dentries = (DIR_ENTRY *)dbuf;
        int dnum = dsize / (int)sizeof(DIR_ENTRY);

        for (int j = 0; j < dnum; j++) {
            if (!is_valid_entry(&dentries[j])) continue;
            if (memcmp(dentries[j].DIR_Name, src_entry->DIR_Name, 11) == 0) {
                printf("Error: name already exists in destination directory.\n");
                free(dbuf);
                free(buffer);
                return;
            }
        }

        int dfree = -1;
        for (int j = 0; j < dnum; j++) {
            if (dentries[j].DIR_Name[0] == 0x00 ||
                dentries[j].DIR_Name[0] == 0x5E ||
                dentries[j].DIR_Name[0] == 0xE5) {
                dfree = j;
                break;
            }
        }

        if (dfree < 0) {
            printf("Error: no space in destination directory.\n");
            free(dbuf);
            free(buffer);
            return;
        }

        memcpy(&dentries[dfree], src_entry, sizeof(DIR_ENTRY));

        unsigned int dsector = cluster_to_sector(dest_cluster);
        unsigned int doff = dsector * bpb.BPB_BytsPerSec;
        fseek(fp, doff, SEEK_SET);
        fwrite(dbuf, dsize, 1, fp);
        free(dbuf);

        int has_after = 0;
        for (int j = src_idx + 1; j < num; j++) {
            if (entries[j].DIR_Name[0] != 0x00) {
                has_after = 1;
                break;
            }
        }
        entries[src_idx].DIR_Name[0] = has_after ? 0x5E : 0x00;

        unsigned int sector = cluster_to_sector(current_cluster);
        unsigned int off = sector * bpb.BPB_BytsPerSec;
        fseek(fp, off, SEEK_SET);
        fwrite(buffer, size, 1, fp);
        free(buffer);
    } else {
        // rename
        memcpy(src_entry->DIR_Name, dst_short, 11);

        unsigned int sector = cluster_to_sector(current_cluster);
        unsigned int off = sector * bpb.BPB_BytsPerSec;
        fseek(fp, off, SEEK_SET);
        fwrite(buffer, size, 1, fp);
        free(buffer);
    }
}

//RM and RMDIR

void rm_cmd(char *filename) {
    if (!filename) {
        printf("Error: rm requires a filename.\n");
        return;
    }

    unsigned char short_filename[11];
    make_short_name(filename, short_filename);

    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            memcmp(open_files_table[i].name, short_filename, 11) == 0) {
            printf("Error: cannot rm an open file.\n");
            return;
        }
    }

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for rm.\n");
        return;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);

    int idx = -1;
    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;
        if (memcmp(entries[i].DIR_Name, short_filename, 11) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        printf("Error: file does not exist.\n");
        free(buffer);
        return;
    }

    DIR_ENTRY *entry = &entries[idx];
    if (entry->DIR_Attr & ATTR_DIRECTORY) {
        printf("Error: rm target is a directory (use rmdir).\n");
        free(buffer);
        return;
    }

    unsigned int first_cluster =
        ((unsigned int)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    if (first_cluster != 0) {
        fat_free_chain(first_cluster);
    }

    int has_after = 0;
    for (int j = idx + 1; j < num; j++) {
        if (entries[j].DIR_Name[0] != 0x00) {
            has_after = 1;
            break;
        }
    }
    entries[idx].DIR_Name[0] = has_after ? 0x5E : 0x00;

    unsigned int sector = cluster_to_sector(current_cluster);
    unsigned int off = sector * bpb.BPB_BytsPerSec;
    fseek(fp, off, SEEK_SET);
    fwrite(buffer, size, 1, fp);

    free(buffer);
}

void rmdir_cmd(char *dirname) {
    if (!dirname) {
        printf("Error: rmdir requires a directory name.\n");
        return;
    }

    unsigned char short_dirname[11];
    make_short_name(dirname, short_dirname);

    unsigned int size = cluster_size();
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        printf("Error: could not allocate memory for rmdir.\n");
        return;
    }

    read_cluster(current_cluster, buffer);
    DIR_ENTRY *entries = (DIR_ENTRY *)buffer;
    int num = size / (int)sizeof(DIR_ENTRY);

    int idx = -1;
    for (int i = 0; i < num; i++) {
        if (!is_valid_entry(&entries[i])) continue;
        if (memcmp(entries[i].DIR_Name, short_dirname, 11) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        printf("Error: directory does not exist.\n");
        free(buffer);
        return;
    }

    DIR_ENTRY *entry = &entries[idx];

    if (!(entry->DIR_Attr & ATTR_DIRECTORY)) {
        printf("Error: rmdir target is not a directory.\n");
        free(buffer);
        return;
    }

    // check if any file is open in that directory
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s%s/", get_current_path(), dirname);

    for (int i = 0; i < 10; i++) {
        if (open_files_table[i].using &&
            strcmp(open_files_table[i].path, dir_path) == 0) {
            printf("Error: a file is opened in that directory.\n");
            free(buffer);
            return;
        }
    }

    unsigned int dir_cluster =
        ((unsigned int)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;

    if (dir_cluster != 0) {
        unsigned int dsize = cluster_size();
        unsigned char *dbuf = malloc(dsize);
        if (!dbuf) {
            printf("Error: could not allocate memory for rmdir.\n");
            free(buffer);
            return;
        }

        read_cluster(dir_cluster, dbuf);
        DIR_ENTRY *dentries = (DIR_ENTRY *)dbuf;
        int dnum = dsize / (int)sizeof(DIR_ENTRY);

        for (int j = 0; j < dnum; j++) {
            if (!is_valid_entry(&dentries[j])) continue;

            char name[12];
            memcpy(name, dentries[j].DIR_Name, 11);
            name[11] = '\0';
            for (int k = 10; k >= 0; k--) {
                if (name[k] == ' ') name[k] = '\0';
                else break;
            }

            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                printf("Error: directory not empty.\n");
                free(dbuf);
                free(buffer);
                return;
            }
        }

        free(dbuf);
        fat_free_chain(dir_cluster);
    }

    int has_after = 0;
    for (int j = idx + 1; j < num; j++) {
        if (entries[j].DIR_Name[0] != 0x00) {
            has_after = 1;
            break;
        }
    }
    entries[idx].DIR_Name[0] = has_after ? 0x5E : 0x00;

    unsigned int sector = cluster_to_sector(current_cluster);
    unsigned int off = sector * bpb.BPB_BytsPerSec;
    fseek(fp, off, SEEK_SET);
    fwrite(buffer, size, 1, fp);

    free(buffer);
}
