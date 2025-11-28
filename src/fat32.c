#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat32.h"

int fat32_mount(const char *filename) {
    // mount image
    printf("you are trying to mount a fat32 image.\n");
    return 1;
}

void fat32_unmount() {
    // unmount image
    printf("you are trying to unmount a fat32 image.\n");
}

const char* fat32_get_image_name() {
    // return image name
    printf("you are trying to get the image name.\n");
    return "";
}