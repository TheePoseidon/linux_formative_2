#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <source> <destination> <buffer_size>\n", argv[0]);
        return 1;
    }

    int size = atoi(argv[3]);
    if (size <= 0) {
        fprintf(stderr, "Buffer size must be a positive number\n");
        return 1;
    }

    char *buf = malloc(size);
    if (buf == NULL) {
        perror("malloc");
        return 1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    FILE *in = fopen(argv[1], "rb");
    if (in == NULL) {
        perror(argv[1]);
        return 1;
    }

    FILE *out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror(argv[2]);
        return 1;
    }

    size_t n;
    while ((n = fread(buf, 1, size, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            perror("fwrite");
            return 1;
        }
    }
    if (ferror(in)) {
        perror("fread");
        return 1;
    }

    fclose(in);
    if (fclose(out) != 0) {
        perror("fclose");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("copy_stdio: buffer %d bytes, elapsed %.6f s\n", size, elapsed);

    free(buf);
    return 0;
}
