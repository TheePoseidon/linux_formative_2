#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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

    int in = open(argv[1], O_RDONLY);
    if (in == -1) {
        perror(argv[1]);
        return 1;
    }

    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out == -1) {
        perror(argv[2]);
        return 1;
    }

    ssize_t n;
    while ((n = read(in, buf, size)) > 0) {
        if (write(out, buf, n) != n) {
            perror("write");
            return 1;
        }
    }
    if (n == -1) {
        perror("read");
        return 1;
    }

    close(in);
    close(out);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("copy_sys: buffer %d bytes, elapsed %.6f s\n", size, elapsed);

    free(buf);
    return 0;
}
