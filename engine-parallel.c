#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256
#define NUM_THREADS 4

struct worker_args {
    char *filename;
    char *target;
    long start;
    long end;

    int count;
    char **instances;
    int error;
};

/* Count whole-word occurrences in a line */
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

/* Create a copy with leading/trailing whitespace removed */
static char *trim_copy(const char *line) {
    while (isspace((unsigned char)*line)) {
        line++;
    }

    size_t len = strlen(line);

    while (len > 0 &&
           isspace((unsigned char)line[len - 1])) {
        len--;
    }

    char *copy = malloc(len + 1);
    if (copy == NULL) return NULL;

    memcpy(copy, line, len);
    copy[len] = '\0';

    return copy;
}

/* Move to the first complete line belonging to this chunk */
static int seek_chunk_start(FILE *fp, long start) {
    if (start == 0) {
        return fseek(fp, 0, SEEK_SET);
    }

    if (fseek(fp, start - 1, SEEK_SET) != 0) {
        return -1;
    }

    int previous = fgetc(fp);

    if (previous == '\n') {
        return 0;
    }

    if (fseek(fp, start, SEEK_SET) != 0) {
        return -1;
    }

    int c;
    while ((c = fgetc(fp)) != EOF && c != '\n') {
    }

    return 0;
}

/* Thread function for counting */
static void *count_worker(void *arg) {
    struct worker_args *args = arg;

    FILE *fp = fopen(args->filename, "rb");

    if (fp == NULL) {
        args->error = 1;
        return NULL;
    }

    if (seek_chunk_start(fp, args->start) != 0) {
        args->error = 1;
        fclose(fp);
        return NULL;
    }

    char *line = NULL;
    size_t capacity = 0;

    while (1) {
        long position = ftell(fp);

        if (position < 0) {
            args->error = 1;
            break;
        }

        if (position >= args->end) {
            break;
        }

        if (getline(&line, &capacity, fp) == -1) {
            if (ferror(fp)) args->error = 1;
            break;
        }

        args->count += count_words(line, args->target);
    }

    free(line);
    fclose(fp);
    return NULL;
}

/* Thread function for collecting matching lines */
static void *instance_worker(void *arg) {
    struct worker_args *args = arg;

    FILE *fp = fopen(args->filename, "rb");

    if (fp == NULL) {
        args->error = 1;
        return NULL;
    }

    if (seek_chunk_start(fp, args->start) != 0) {
        args->error = 1;
        fclose(fp);
        return NULL;
    }

    char *line = NULL;
    size_t capacity = 0;

    while (1) {
        long position = ftell(fp);

        if (position < 0) {
            args->error = 1;
            break;
        }

        if (position >= args->end) {
            break;
        }

        if (getline(&line, &capacity, fp) == -1) {
            if (ferror(fp)) args->error = 1;
            break;
        }

        int matches = count_words(line, args->target);

        for (int j = 0; j < matches; j++) {
            char *copy = trim_copy(line);

            if (copy == NULL) {
                args->error = 1;
                break;
            }

            char **temp = realloc(
                args->instances,
                (size_t)(args->count + 1) * sizeof(char *)
            );

            if (temp == NULL) {
                free(copy);
                args->error = 1;
                break;
            }

            args->instances = temp;
            args->instances[args->count++] = copy;
        }

        if (args->error) break;
    }

    free(line);
    fclose(fp);
    return NULL;
}

/* Get file size */
static long get_file_size(char *filename) {
    struct stat st;

    if (stat(filename, &st) != 0) {
        return -1;
    }

    return (long)st.st_size;
}

/* Initialize worker arguments */
static void init_args(struct worker_args *args,
                      char *filename,
                      char *target,
                      long file_size,
                      int i) {
    long chunk = file_size / NUM_THREADS;

    args->filename = filename;
    args->target = target;
    args->start = i * chunk;
    args->end = (i == NUM_THREADS - 1)
                    ? file_size
                    : (i + 1) * chunk;

    args->count = 0;
    args->instances = NULL;
    args->error = 0;
}

/* Free worker-owned strings */
static void free_worker_instances(struct worker_args *args) {
    for (int i = 0; i < args->count; i++) {
        free(args->instances[i]);
    }

    free(args->instances);
}

/* Parallel count */
int search_count(char *filename, char *target) {
    long file_size = get_file_size(filename);

    if (file_size < 0) {
        perror("stat");
        return -1;
    }

    pthread_t threads[NUM_THREADS];
    struct worker_args args[NUM_THREADS];

    int created = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        init_args(&args[i], filename, target, file_size, i);

        if (pthread_create(&threads[i], NULL,
                           count_worker, &args[i]) != 0) {
            break;
        }

        created++;
    }

    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }

    if (created != NUM_THREADS) return -1;

    int total = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        if (args[i].error) return -1;
        total += args[i].count;
    }

    return total;
}

/* Parallel instance search */
struct count_result search_instance(char *filename, char *target) {
    struct count_result result = {0, NULL};

    long file_size = get_file_size(filename);

    if (file_size < 0) {
        result.count = -1;
        return result;
    }

    pthread_t threads[NUM_THREADS];
    struct worker_args args[NUM_THREADS];

    int created = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        init_args(&args[i], filename, target, file_size, i);

        if (pthread_create(&threads[i], NULL,
                           instance_worker, &args[i]) != 0) {
            break;
        }

        created++;
    }

    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }

    int total = 0;
    bool failed = created != NUM_THREADS;

    for (int i = 0; i < created; i++) {
        if (args[i].error) failed = true;
        total += args[i].count;
    }

    if (failed) {
        for (int i = 0; i < created; i++) {
            free_worker_instances(&args[i]);
        }

        result.count = -1;
        return result;
    }

    if (total > 0) {
        result.instances = malloc((size_t)total * sizeof(char *));

        if (result.instances == NULL) {
            for (int i = 0; i < created; i++) {
                free_worker_instances(&args[i]);
            }

            result.count = -1;
            return result;
        }
    }

    /* Combine results in file order */
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = 0; j < args[i].count; j++) {
            result.instances[result.count++] =
                args[i].instances[j];
        }

        /* Free only the pointer array; strings now belong to result */
        free(args[i].instances);
    }

    return result;
}