#ifndef FAT32_H
#define FAT32_H

#include <stdio.h>

int fat32_mount(const char *filename);
void fat32_unmount();

const char* fat32_get_image_name();

#endif