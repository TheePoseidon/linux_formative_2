#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

void close_pipes(int p[3][2])
{
    for (int i = 0; i < 3; i++) {
        close(p[i][0]);
        close(p[i][1]);
    }
}

pid_t start_stage(char *args[], int in_fd, int out_fd, int p[3][2])
{
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        if (in_fd != -1)
            dup2(in_fd, STDIN_FILENO);
        dup2(out_fd, STDOUT_FILENO);
        close_pipes(p);
        execvp(args[0], args);
        perror("execvp");
        exit(1);
    }
    return pid;
}

int main(void)
{
    int p[3][2];
    pid_t pids[3];
    char *names[3] = {"cat", "cut", "sort"};
    char *cat_args[] = {"cat", "/etc/passwd", NULL};
    char *cut_args[] = {"cut", "-d:", "-f7", NULL};
    char *sort_args[] = {"sort", "-u", NULL};
    char buf[1024];
    ssize_t n;
    int fd;

    for (int i = 0; i < 3; i++) {
        if (pipe(p[i]) == -1) {
            perror("pipe");
            exit(1);
        }
    }

    pids[0] = start_stage(cat_args, -1, p[0][1], p);
    pids[1] = start_stage(cut_args, p[0][0], p[1][1], p);
    pids[2] = start_stage(sort_args, p[1][0], p[2][1], p);

    close(p[0][0]);
    close(p[0][1]);
    close(p[1][0]);
    close(p[1][1]);
    close(p[2][1]);

    fd = open("result.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }
    while ((n = read(p[2][0], buf, sizeof(buf))) > 0)
        write(fd, buf, n);
    close(p[2][0]);
    close(fd);

    fd = open("result.txt", O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }
    int lines = 0;
    printf("First 5 lines of result.txt:\n");
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (lines < 5)
                putchar(buf[i]);
            if (buf[i] == '\n')
                lines++;
        }
    }
    close(fd);
    printf("Total lines: %d\n", lines);

    for (int i = 0; i < 3; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status))
            printf("Child %d (%s, PID %d) exited with status %d\n",
                   i + 1, names[i], pids[i], WEXITSTATUS(status));
        else
            printf("Child %d (%s, PID %d) terminated abnormally\n",
                   i + 1, names[i], pids[i]);
    }

    return 0;
}
