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
        printf("%s%s> ", get_image_name(), get_current_path());

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

        char *cmd  = tokens->items[0];
        char *arg1 = (tokens->size > 1) ? tokens->items[1] : NULL;
        char *arg2 = (tokens->size > 2) ? tokens->items[2] : NULL;

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

        else if (strcmp(cmd, "cd") == 0) {
            cd(arg1);
        }

        else if (strcmp(cmd, "creat") == 0) {
            creat(arg1);
        }

        else if (strcmp(cmd, "mkdir") == 0) {
            mkdir(arg1);
        }

        else if (strcmp(cmd, "open") == 0) {
            open(arg1, arg2);
        }

        else if (strcmp(cmd, "close") == 0) {
            close(arg1);
        }

        else if (strcmp(cmd, "lsof") == 0) {
            lsof();
        }

        else if (strcmp(cmd, "lseek") == 0) {
            if (!arg1 || !arg2) {
                printf("Error: lseek requires [FILENAME] [OFFSET].\n");
            } else {
                unsigned int off = (unsigned int)strtoul(arg2, NULL, 10);
                lseek(arg1, off);
            }
        }

        else if (strcmp(cmd, "write") == 0) {
            if (!arg1) {
                printf("Error: write requires [FILENAME] [STRING].\n");
            } else {
                char *first_quote = strchr(input, '\"');
                char *last_quote  = NULL;
                if (first_quote != NULL) {
                    last_quote = strrchr(first_quote + 1, '\"');
                }

                if (!first_quote || !last_quote ||
                    last_quote <= first_quote + 1) {
                    printf("Error: STRING must be enclosed in quotes.\n");
                } else {
                    size_t len = (size_t)(last_quote - first_quote - 1);
                    char *str = (char *)malloc(len + 1);
                    if (!str) {
                        printf("Error: memory allocation failed.\n");
                    } else {
                        memcpy(str, first_quote + 1, len);
                        str[len] = '\0';
                        write_cmd(arg1, str);
                        free(str);
                    }
                }
            }
        }

        else if (strcmp(cmd, "mv") == 0) {
            if (!arg1 || !arg2) {
                printf("Error: mv requires [SRC] [DST].\n");
            } else {
                mv_cmd(arg1, arg2);
            }
        }

        else if (strcmp(cmd, "rm") == 0) {
            if (!arg1) {
                printf("Error: rm requires [FILENAME].\n");
            } else {
                rm_cmd(arg1);
            }
        }

        else if (strcmp(cmd, "rmdir") == 0) {
            if (!arg1) {
                printf("Error: rmdir requires [DIRNAME].\n");
            } else {
                rmdir_cmd(arg1);
            }
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
