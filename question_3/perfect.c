#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define LIMIT 100000
#define NUM_THREADS 8
#define MAX_PERFECT 100

typedef struct {
    int start;
    int end;
} Range;

int total_count = 0;
int perfect[MAX_PERFECT];
int perfect_size = 0;
pthread_mutex_t lock;

int is_perfect(int n)
{
    if (n < 2)
        return 0;

    int sum = 1;
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) {
            sum += i;
            if (i != n / i)
                sum += n / i;
        }
    }
    return sum == n;
}

void *search(void *arg)
{
    Range *r = (Range *)arg;
    int local_count = 0;

    for (int n = r->start; n <= r->end; n++) {
        if (is_perfect(n)) {
            local_count++;
            pthread_mutex_lock(&lock);
            perfect[perfect_size] = n;
            perfect_size++;
            pthread_mutex_unlock(&lock);
        }
    }

    pthread_mutex_lock(&lock);
    total_count += local_count;
    pthread_mutex_unlock(&lock);

    return NULL;
}

int compare(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

double elapsed(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void)
{
    struct timespec start, end;
    pthread_t threads[NUM_THREADS];
    Range ranges[NUM_THREADS];
    int segment = LIMIT / NUM_THREADS;

    clock_gettime(CLOCK_MONOTONIC, &start);
    int single_count = 0;
    for (int n = 1; n <= LIMIT; n++) {
        if (is_perfect(n))
            single_count++;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double single_time = elapsed(start, end);

    pthread_mutex_init(&lock, NULL);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_THREADS; i++) {
        ranges[i].start = i * segment + 1;
        ranges[i].end = (i + 1) * segment;
        if (pthread_create(&threads[i], NULL, search, &ranges[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double multi_time = elapsed(start, end);

    pthread_mutex_destroy(&lock);

    qsort(perfect, perfect_size, sizeof(int), compare);

    printf("Perfect numbers found:");
    for (int i = 0; i < perfect_size; i++)
        printf(" %d", perfect[i]);
    printf("\n");
    printf("The synchronized total number of perfect numbers between 1 and %d is %d\n", LIMIT, total_count);
    printf("Single-threaded time: %.6f seconds\n", single_time);
    printf("Multi-threaded time:  %.6f seconds\n", multi_time);

    if (single_count != total_count)
        printf("Warning: single-threaded count is %d\n", single_count);

    return 0;
}
