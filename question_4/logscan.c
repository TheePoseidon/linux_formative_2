#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

char **files;
int file_count;
int next_file = 0;
char *pattern;
FILE *report;
long grand_total = 0;
int files_processed = 0;

pthread_mutex_t queue_lock;
pthread_mutex_t report_lock;
pthread_mutex_t total_lock;

char *get_next_file(void)
{
    char *file = NULL;

    pthread_mutex_lock(&queue_lock);
    if (next_file < file_count) {
        file = files[next_file];
        next_file++;
    }
    pthread_mutex_unlock(&queue_lock);

    return file;
}

void *worker(void *arg)
{
    int id = *(int *)arg;
    char *filename;
    char line[4096];

    while ((filename = get_next_file()) != NULL) {
        FILE *fp = fopen(filename, "r");
        if (fp == NULL) {
            perror(filename);
            continue;
        }

        long matching = 0;
        long total = 0;
        while (fgets(line, sizeof(line), fp) != NULL) {
            total++;
            if (strstr(line, pattern) != NULL)
                matching++;
        }
        fclose(fp);

        pthread_mutex_lock(&report_lock);
        fprintf(report, "%s: %ld matching lines out of %ld (thread %d)\n",
                filename, matching, total, id);
        pthread_mutex_unlock(&report_lock);

        pthread_mutex_lock(&total_lock);
        grand_total += matching;
        files_processed++;
        pthread_mutex_unlock(&total_lock);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s pattern report.txt <number_of_threads> file1.log file2.log ...\n", argv[0]);
        return 1;
    }

    pattern = argv[1];
    int num_threads = atoi(argv[3]);
    files = &argv[4];
    file_count = argc - 4;

    if (num_threads < 1) {
        fprintf(stderr, "Number of threads must be at least 1\n");
        return 1;
    }

    report = fopen(argv[2], "w");
    if (report == NULL) {
        perror(argv[2]);
        return 1;
    }

    pthread_t threads[num_threads];
    int ids[num_threads];

    pthread_mutex_init(&queue_lock, NULL);
    pthread_mutex_init(&report_lock, NULL);
    pthread_mutex_init(&total_lock, NULL);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_threads; i++) {
        ids[i] = i + 1;
        if (pthread_create(&threads[i], NULL, worker, &ids[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    fprintf(report, "Total matching lines across %d files: %ld\n", files_processed, grand_total);
    fclose(report);

    printf("Total matching lines across %d files: %ld\n", files_processed, grand_total);
    printf("Threads: %d, elapsed time: %.6f seconds\n", num_threads, elapsed);

    pthread_mutex_destroy(&queue_lock);
    pthread_mutex_destroy(&report_lock);
    pthread_mutex_destroy(&total_lock);

    return 0;
}
