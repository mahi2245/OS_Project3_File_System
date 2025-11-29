#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "fat32.h"

int main(int argc, char *argv[]) {

    // print an error message if user does not mount image file
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <fat32 image>\n", argv[0]);
        return 1;
    }

    // open the FAT32 image
    if (fat32_mount(argv[1]) != 0) {
        fprintf(stderr, "Error: failed to open FAT32 image.\n");
        return 1;
    }

    while (1) {
        // print initial prompt
        printf("%s/> ", get_image_name());

        // get user input
        char *input = get_input();
        if (input == NULL) {
            printf("\n");
            break;
        }

        tokenlist *tokens = get_tokens(input);

        if (tokens->size == 0) {
            free(input);
            free_tokens(tokens);
            continue;
        }

        char *cmd = tokens->items[0];

        // implement commands
        if (strcmp(cmd, "exit") == 0) {
            free(input);
            free_tokens(tokens);
            break;
        }

        else if (strcmp(cmd, "info") == 0) {
            info();
        }

        else if (strcmp(cmd, "ls") == 0) {
            ls();
        }

        else {
            printf("Error: not a valid command\n");
        }

        free(input);
        free_tokens(tokens);
    }

    fat32_unmount();
    return 0;
}