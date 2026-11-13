# Question 1: Three-Stage Pipeline with strace

Pipeline used (strict three-child design):

```
cat /etc/passwd | cut -d: -f7 | sort -u
```

## Compile and Run

```bash
cd question_1
gcc -Wall -o pipeline pipeline.c
./pipeline
```

strace:

```bash
strace -f -tt -o trace.log ./pipeline
strace -f -e trace=clone,fork,execve,pipe,pipe2,dup2,open,openat,read,write,close,wait4 ./pipeline
```

## Program Output

```
First 5 lines of result.txt:
/bin/bash
/bin/false
/bin/sync
/usr/bin/zsh
/usr/sbin/nologin
Total lines: 5
Child 1 (cat, PID 72220) exited with status 0
Child 2 (cut, PID 72221) exited with status 0
Child 3 (sort, PID 72222) exited with status 0
```

## 1. Sequence of System Calls

Excerpts are from `trace.log`. The parent is PID 72218, and the children are 72220 (cat), 72221 (cut) and 72222 (sort). Each `<unfinished ...>` / `<... resumed>` pair is joined into one line, and dynamic-loader lines are left out.

### a) Process Creation

```
72218 19:37:06.749008 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 72220
72218 19:37:06.749678 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 72221
72218 19:37:06.751665 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 72222
72220 19:37:06.757899 execve("/usr/bin/cat", ["cat", "/etc/passwd"], ...) = 0
72221 19:37:06.754912 execve("/usr/bin/cut", ["cut", "-d:", "-f7"], ...) = 0
72222 19:37:06.758357 execve("/usr/bin/sort", ["sort", "-u"], ...) = 0
```

Before each successful `execve` there are failed attempts such as `execve("/usr/local/bin/cat", ...) = -1 ENOENT`. This is `execvp()` searching each directory in `PATH`.

### b) Pipe Setup and Redirection

```
72218 19:37:06.748552 pipe2([3, 4], 0)  = 0        pipe A: cat -> cut
72218 19:37:06.748695 pipe2([5, 6], 0)  = 0        pipe B: cut -> sort
72218 19:37:06.748775 pipe2([7, 8], 0)  = 0        pipe C: sort -> parent

72220 19:37:06.750150 dup2(4, 1) = 1               cat:  stdout -> pipe A
72221 19:37:06.750739 dup2(3, 0) = 0               cut:  stdin  <- pipe A
72221 19:37:06.750875 dup2(6, 1) = 1               cut:  stdout -> pipe B
72222 19:37:06.753704 dup2(5, 0) = 0               sort: stdin  <- pipe B
72222 19:37:06.754159 dup2(8, 1) = 1               sort: stdout -> pipe C

72220 close(3) close(4) close(5) close(6) close(7) close(8)      each child closes all 6 pipe fds
72218 close(3) close(4) close(5) close(6) close(8)              parent keeps only 7
```

### c) File Operations and Data Flow

```
72220 19:37:06.799320 openat(AT_FDCWD, "/etc/passwd", O_RDONLY|O_CLOEXEC) = 3
72221 19:37:06.800223 read(0, "root:x:0:0:root:/root:/bin/bash\n"..., 8192) = 2824
72221 19:37:06.801632 read(0, "", 8192) = 0
72221 19:37:06.801764 write(1, "/bin/bash\n/usr/sbin/nologin\n/usr"..., 818)
72222 19:37:06.842659 read(0, "/bin/bash\n/usr/sbin/nologin\n/usr"..., 8192) = 818
72222 19:37:06.842803 read(0, "", 8192) = 0
72222 19:37:06.846690 write(1, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 62)

72218 19:37:06.754623 openat(AT_FDCWD, "result.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
72218 19:37:06.849573 read(7, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 1024) = 62
72218 19:37:06.850427 write(3, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 62) = 62
72218 19:37:06.851155 read(7, "", 1024) = 0
72218 19:37:06.851447 close(7) = 0
72218 19:37:06.851683 close(3) = 0
72218 19:37:06.851902 openat(AT_FDCWD, "result.txt", O_RDONLY) = 3
72218 19:37:06.852988 read(3, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 1024) = 62
72218 19:37:06.855780 read(3, "", 1024) = 0
72218 19:37:06.855868 close(3) = 0
```

`cat` on this system (Rust coreutils) copies with `splice(3, NULL, 5, ...)` instead of `read()`/`write()`.

### d) Process Termination

```
72220 19:37:06.801656 +++ exited with 0 +++
72218 19:37:06.801732 --- SIGCHLD {si_code=CLD_EXITED, si_pid=72220, si_status=0} ---
72221 19:37:06.804327 +++ exited with 0 +++
72218 19:37:06.804483 --- SIGCHLD {si_code=CLD_EXITED, si_pid=72221, si_status=0} ---
72222 19:37:06.851033 +++ exited with 0 +++
72218 19:37:06.851304 --- SIGCHLD {si_code=CLD_EXITED, si_pid=72222, si_status=0} ---
72218 19:37:06.856411 wait4(72220, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 72220
72218 19:37:06.857000 wait4(72221, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 72221
72218 19:37:06.857383 wait4(72222, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 72222
72218 19:37:06.861938 +++ exited with 0 +++
```

## 2. Role of Each Key Call

| Call | Role |
|------|------|
| clone / fork | `fork()` is implemented as `clone(... SIGCHLD)`. It creates a child that is a copy of the parent, including its file descriptors. (The `clone3` calls in `sort` create its internal threads, not new processes.) |
| execve | Replaces the child's program with `cat`, `cut` or `sort`. The open fds are kept. |
| pipe / pipe2 | Creates a one-way kernel buffer and returns `[read end, write end]`. |
| dup2 | `dup2(old, new)` makes fd `new` refer to the same pipe as `old`. Used to set stdin (0) and stdout (1). |
| read | Reads from a fd. Returns 0 at EOF. |
| write | Writes to a fd (pipe, file or terminal). |
| close | Releases a fd. When all write ends of a pipe are closed, the reader gets EOF. |
| wait4 | `waitpid()` is implemented as `wait4()`. It waits for a child, returns its exit status and removes the zombie. |

## 3. File Descriptor Changes

After `pipe2`, the parent has A = [3, 4], B = [5, 6], C = [7, 8]. `fork` copies all of them to each child.

| Process | dup2 | Closes | Final fds |
|---------|------|--------|-----------|
| Parent | none | 3, 4, 5, 6, 8 | 7 = pipe C read; then 3 = result.txt |
| cat | `dup2(4,1)` | 3 to 8 | 0 = terminal, 1 = pipe A write |
| cut | `dup2(3,0)`, `dup2(6,1)` | 3 to 8 | 0 = pipe A read, 1 = pipe B write |
| sort | `dup2(5,0)`, `dup2(8,1)` | 3 to 8 | 0 = pipe B read, 1 = pipe C write |

**If unused ends are not closed:** a reader gets EOF only when **every** write end of the pipe is closed, in all processes.
- If the parent kept fd 8 open, `read(7)` would never return 0, and the parent would hang.
- If `sort` kept fd 6 open, it would wait forever for more input on its own pipe. It would never print anything, so the pipeline would deadlock.

## 4. EOF and Exit Status

**EOF:** The parent loops while `read(7, ...) > 0`. When `sort` exits, the last write end of pipe C is closed, so `read(7, "", 1024) = 0`, which is EOF, and the loop ends. The same happens along the chain: `cat` exits, so `cut` gets `read(0, "", 8192) = 0`; `cut` exits, so `sort` gets EOF.

**Exit status:** Each child that exits sends `SIGCHLD` to the parent and stays a zombie until it is collected. The parent calls `waitpid()`, shown as `wait4()`, for each PID. It then uses `WIFEXITED()` and `WEXITSTATUS()` to print the status. All three returned 0.
