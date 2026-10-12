#define _POSIX_C_SOURCE 200809L

#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <omp.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256
#define NUM_THREADS 4

struct chunk_result {
    int count;
    char **instances;
    int error;
};

/* Count complete-word matches */
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

/* Copy a line without surrounding whitespace */
static char *trim_copy(const char *line) {
    while (isspace((unsigned char)*line)) line++;

    size_t len = strlen(line);

    while (len > 0 &&
           isspace((unsigned char)line[len - 1])) {
        len--;
    }

    char *copy = malloc(len + 1);
    if (!copy) return NULL;

    memcpy(copy, line, len);
    copy[len] = '\0';
    return copy;
}

/* Find the first complete line in a chunk */
static int seek_chunk_start(FILE *fp, long start) {
    if (start == 0) return fseek(fp, 0, SEEK_SET);

    if (fseek(fp, start - 1, SEEK_SET) != 0) return -1;

    if (fgetc(fp) == '\n') return 0;

    if (fseek(fp, start, SEEK_SET) != 0) return -1;

    int c;
    while ((c = fgetc(fp)) != EOF && c != '\n') {
    }

    return 0;
}

/* Search one chunk */
static struct chunk_result search_chunk(
    char *filename,
    char *target,
    long start,
    long end,
    bool save_instances
) {
    struct chunk_result result = {0, NULL, 0};

    FILE *fp = fopen(filename, "rb");

    if (!fp) {
        result.error = 1;
        return result;
    }

    if (seek_chunk_start(fp, start) != 0) {
        result.error = 1;
        fclose(fp);
        return result;
    }

    char *line = NULL;
    size_t capacity = 0;

    while (1) {
        long position = ftell(fp);

        if (position < 0) {
            result.error = 1;
            break;
        }

        if (position >= end) break;

        if (getline(&line, &capacity, fp) == -1) {
            if (ferror(fp)) result.error = 1;
            break;
        }

        int matches = count_words(line, target);

        if (!save_instances) {
            result.count += matches;
            continue;
        }

        for (int j = 0; j < matches; j++) {
            char *copy = trim_copy(line);

            if (!copy) {
                result.error = 1;
                break;
            }

            char **temp = realloc(
                result.instances,
                (size_t)(result.count + 1) * sizeof(char *)
            );

            if (!temp) {
                free(copy);
                result.error = 1;
                break;
            }

            result.instances = temp;
            result.instances[result.count++] = copy;
        }

        if (result.error) break;
    }

    free(line);
    fclose(fp);

    return result;
}

static void free_chunk(struct chunk_result *result) {
    for (int i = 0; i < result->count; i++) {
        free(result->instances[i]);
    }

    free(result->instances);
}

static long get_file_size(char *filename) {
    struct stat st;

    if (stat(filename, &st) != 0) return -1;

    return (long)st.st_size;
}

/* Parallel word counting */
int search_count(char *filename, char *target) {
    long file_size = get_file_size(filename);

    if (file_size < 0) return -1;

    struct chunk_result partial[NUM_THREADS] = {0};

    long chunk_size = file_size / NUM_THREADS;

    #pragma omp parallel for num_threads(NUM_THREADS)
    for (int i = 0; i < NUM_THREADS; i++) {
        long start = i * chunk_size;
        long end = (i == NUM_THREADS - 1)
            ? file_size
            : (i + 1) * chunk_size;

        partial[i] = search_chunk(
            filename, target, start, end, false
        );
    }

    int total = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        if (partial[i].error) return -1;
        total += partial[i].count;
    }

    return total;
}

/* Parallel instance searching */
struct count_result search_instance(
    char *filename,
    char *target
) {
    struct count_result result = {0, NULL};

    long file_size = get_file_size(filename);

    if (file_size < 0) {
        result.count = -1;
        return result;
    }

    struct chunk_result partial[NUM_THREADS] = {0};

    long chunk_size = file_size / NUM_THREADS;

    #pragma omp parallel for num_threads(NUM_THREADS)
    for (int i = 0; i < NUM_THREADS; i++) {
        long start = i * chunk_size;
        long end = (i == NUM_THREADS - 1)
            ? file_size
            : (i + 1) * chunk_size;

        partial[i] = search_chunk(
            filename, target, start, end, true
        );
    }

    int total = 0;
    bool failed = false;

    for (int i = 0; i < NUM_THREADS; i++) {
        if (partial[i].error) failed = true;
        total += partial[i].count;
    }

    if (failed) {
        for (int i = 0; i < NUM_THREADS; i++) {
            free_chunk(&partial[i]);
        }

        result.count = -1;
        return result;
    }

    if (total > 0) {
        result.instances = malloc(
            (size_t)total * sizeof(char *)
        );

        if (!result.instances) {
            for (int i = 0; i < NUM_THREADS; i++) {
                free_chunk(&partial[i]);
            }

            result.count = -1;
            return result;
        }
    }

    /* Merge results in original file order */
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = 0; j < partial[i].count; j++) {
            result.instances[result.count++] =
                partial[i].instances[j];
        }

        free(partial[i].instances);
    }

    return result;
}