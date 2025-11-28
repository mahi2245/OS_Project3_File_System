#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat32.h"

static FILE *img = NULL;
static char *img_name = NULL;

int fat32_mount(const char *filename) {
    // mount image
    img = fopen(filename, "rb");
    if (!img) return -1;

    img_name = strdup(filename);
    if (!img_name) {
        fclose(img);
        img = NULL;
        return -1;
    }

    return 0;
}

void fat32_unmount() {
    // unmount image
    if (img) {
        fclose(img);
        img = NULL;
    }

    if (img_name) {
        free(img_name);
        img_name = NULL;
    }
}

const char* get_image_name() {
    // return image name
    return img_name;
}