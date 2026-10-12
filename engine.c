#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256

// Count occurrences of target in one line
static int count_words(const char *line, const char *target) {
    int count = 0;
    size_t len = strlen(target);

    if (len == 0) return 0;

    const char *p = line;

    while ((p = strstr(p, target)) != NULL) {
        count++;
        p += len;
    }

    return count;
}
// Remove leading and trailing whitespace
static char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) {
        str++;
    }

    char *end = str + strlen(str);

    while (end > str && isspace((unsigned char)end[-1])) {
        end--;
    }

    *end = '\0';

    return str;
}

int search_count(char *filename, char *target) {
    FILE *fp = fopen(filename, "r");

    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    int count = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        count += count_words(line, target);
    }

    fclose(fp);

    return count;
}

struct count_result search_instance(char *filename, char *target) {
    struct count_result result = {0, NULL};

    FILE *fp = fopen(filename, "r");

    if (fp == NULL) {
        perror("fopen");
        result.count = -1;
        return result;
    }

    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), fp) != NULL) {
        int matches = count_words(line, target);

        if (matches == 0) {
            continue;
        }

        char *trimmed = trim_whitespace(line);

        // Add one array entry per occurrence
        for (int i = 0; i < matches; i++) {
            char **temp = realloc(
                result.instances,
                (result.count + 1) * sizeof(char *)
            );

            if (temp == NULL) {
                perror("realloc");
                goto error;
            }

            result.instances = temp;

            size_t length = strlen(trimmed) + 1;

            result.instances[result.count] = malloc(length);

            if (result.instances[result.count] == NULL) {
                perror("malloc");
                goto error;
            }

            memcpy(result.instances[result.count], trimmed, length);

            result.count++;
        }
    }

    fclose(fp);

    return result;

error:
    fclose(fp);

    for (int i = 0; i < result.count; i++) {
        free(result.instances[i]);
    }

    free(result.instances);

    result.instances = NULL;
    result.count = -1;

    return result;
}