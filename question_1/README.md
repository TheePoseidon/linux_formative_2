# Question 1: Three-Stage Pipeline with strace

Pipeline used (strict three-child design):

```
cat /etc/passwd | cut -d: -f7 | sort -u
```

## Compile and Run

```bash
cd question1
gcc -Wall -o pipeline pipeline.c
./pipeline
```

strace:

```bash
strace -f -tt -o trace.log ./pipeline
strace -f -e trace=clone,fork,execve,pipe,pipe2,dup2,open,openat,read,write,close,wait4 -o filtered.log ./pipeline
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
Child 1 (cat, PID 53008) exited with status 0
Child 2 (cut, PID 53009) exited with status 0
Child 3 (sort, PID 53010) exited with status 0
```

## 1. Sequence of System Calls

Excerpts are from `filtered.log`. The parent is PID 53075. The children are 53076 (cat), 53077 (cut) and 53078 (sort). Dynamic-loader lines (libraries, locale files) are left out.

### a) Pipe Setup (parent)

```
53075 pipe2([3, 4], 0)                  = 0     pipe A: cat -> cut
53075 pipe2([5, 6], 0)                  = 0     pipe B: cut -> sort
53075 pipe2([7, 8], 0)                  = 0     pipe C: sort -> parent
```

### b) Process Creation (parent)

```
53075 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 53076
53075 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 53077
53075 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 53078
```

### c) Redirection and Closing Unused Ends (each child), then execve

```
53076 dup2(4, 1)          = 1          cat:  stdout -> pipe A write
53076 close(3) ... close(8)
53076 execve("/usr/bin/cat", ["cat", "/etc/passwd"], ...) = 0

53077 dup2(3, 0)          = 0          cut:  stdin  <- pipe A read
53077 dup2(6, 1)          = 1          cut:  stdout -> pipe B write
53077 close(3) ... close(8)
53077 execve("/usr/bin/cut", ["cut", "-d:", "-f7"], ...) = 0

53078 dup2(5, 0)          = 0          sort: stdin  <- pipe B read
53078 dup2(8, 1)          = 1          sort: stdout -> pipe C write
53078 close(3) ... close(8)
53078 execve("/usr/bin/sort", ["sort", "-u"], ...) = 0
```

Before each successful `execve`, there are failed attempts such as `execve("/usr/local/bin/cut", ...) = -1 ENOENT`. This is `execvp()` trying each directory in `PATH` until it finds the program.

Parent closes the pipe ends it does not use:

```
53075 close(3)  close(4)  close(5)  close(6)  close(8)      keeps only 7 (pipe C read)
```

### d) Data Flow and File Operations

```
53076 openat(AT_FDCWD, "/etc/passwd", O_RDONLY|O_CLOEXEC) = 3
53077 read(0, "root:x:0:0:root:/root:/bin/bash\n"..., 8192) = 2824
53077 read(0, "", 8192)                 = 0                       EOF from cat
53077 write(1, "/bin/bash\n/usr/sbin/nologin\n/usr"..., 818) = 818
53078 read(0, "/bin/bash\n/usr/sbin/nologin\n/usr"..., 8192) = 818
53078 read(0, "", 8192)                 = 0                       EOF from cut
53078 write(1, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 62) = 62

53075 openat(AT_FDCWD, "result.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
53075 read(7, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 1024) = 62
53075 write(3, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 62) = 62
53075 read(7, "", 1024)                 = 0                       EOF from sort
53075 close(7)                          = 0
53075 close(3)                          = 0
53075 openat(AT_FDCWD, "result.txt", O_RDONLY) = 3
53075 read(3, "/bin/bash\n/bin/false\n/bin/sync\n/"..., 1024) = 62
53075 read(3, "", 1024)                 = 0
53075 close(3)                          = 0
```

On this system `cat` is the Rust coreutils version. It copies the file to the pipe with `splice()` (visible in `trace.log`) instead of `read()`/`write()`.

### e) Process Termination (with timestamps from `trace.log`)

```
53065 18:45:53.379862 +++ exited with 0 +++
53064 18:45:53.380135 --- SIGCHLD {si_code=CLD_EXITED, si_pid=53065, si_status=0} ---
53066 18:45:53.381329 +++ exited with 0 +++
53067 18:45:53.398016 +++ exited with 0 +++
53064 18:45:53.400848 wait4(53065, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 53065
53064 18:45:53.402579 wait4(53066, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 53066
53064 18:45:53.402866 wait4(53067, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 53067
53064 18:45:53.403436 write(1, "First 5 lines of result.txt:\n/bi"..., 245) = 245
53064 18:45:53.404448 +++ exited with 0 +++
```

## 2. Role of Each Key Call

| Call | Role |
|------|------|
| clone / fork | `fork()` in glibc is implemented as `clone(... SIGCHLD)`. It creates a copy of the parent, including a copy of its file descriptor table. (The `clone3` calls inside `sort` are `sort` creating its own threads, not new processes.) |
| execve | Replaces the child's program with `cat`, `cut` or `sort`. Open file descriptors (stdin/stdout set by dup2) are kept. |
| pipe / pipe2 | Creates a one-way kernel buffer and returns two fds: `[read end, write end]`. `pipe()` in glibc calls `pipe2(fds, 0)`. |
| dup2 | `dup2(old, new)` makes fd `new` point to the same pipe as `old`. Used to make fd 0 (stdin) or fd 1 (stdout) a pipe end. |
| read | Reads bytes from a fd. Returns 0 at EOF. |
| write | Writes bytes to a fd (pipe, file or terminal). |
| close | Releases a fd. Closing every write end of a pipe is what produces EOF for the reader. |
| wait4 | `waitpid()` is implemented as `wait4()`. It waits for a child to finish, returns its status and removes the zombie. |

## 3. File Descriptor Changes

After the three `pipe2` calls, the parent has: A = [3 read, 4 write], B = [5, 6], C = [7, 8]. Each `fork` copies all of these into the child.

| Process | After dup2 | Closes | Final fds used |
|---------|-----------|--------|----------------|
| Parent | none | 3, 4, 5, 6, 8 | 7 = read pipe C; 3 = result.txt (opened later) |
| cat | `dup2(4,1)` | 3 to 8 | 0 = terminal, 1 = pipe A write |
| cut | `dup2(3,0)`, `dup2(6,1)` | 3 to 8 | 0 = pipe A read, 1 = pipe B write |
| sort | `dup2(5,0)`, `dup2(8,1)` | 3 to 8 | 0 = pipe B read, 1 = pipe C write |

**If the unused pipe ends are not closed:**
- A reader only gets EOF when **all** write ends of the pipe are closed, in every process.
- If the parent keeps fd 8 (pipe C write) open, `read(7)` never returns 0 and the parent hangs forever.
- If, say, `sort` keeps fd 6 (pipe B write) open, `sort` waits forever for more input on its own pipe. Since `sort` outputs only after EOF, nothing reaches the parent and the whole pipeline deadlocks.
- Each process would also keep fds it does not need open.

## 4. EOF Detection and Exit Status Collection

**EOF:** The parent loops `while ((n = read(p[2][0], buf, 1024)) > 0)`. When `sort` exits, the last write end of pipe C (its fd 1) is closed. The parent and the other children have already closed their copies. The kernel then returns 0 from `read()`, which is EOF: `read(7, "", 1024) = 0`. The same happens down the chain: `cat` exits, so `cut` sees EOF; `cut` exits, so `sort` sees EOF. (The `ERESTARTSYS` lines in the trace show a `read()` being interrupted by `SIGCHLD` and restarted automatically. This does not affect the data.)

**Exit status:** When a child exits, the kernel sends `SIGCHLD` to the parent, and the child stays as a zombie holding its exit code. The parent calls `waitpid(pids[i], &status, 0)`, which appears as `wait4(...)`, once per child. It then decodes the status with `WIFEXITED(status)` and `WEXITSTATUS(status)`. All three children returned 0.
