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
strace -f -e trace=clone,fork,execve,pipe,pipe2,dup2,open,openat,read,write,close,wait4 -o filtered.log ./pipeline
```

Helper commands to find the excerpts in the logs:

```bash
grep -E 'pipe2|clone\(|dup2|wait4|exited|SIGCHLD' filtered.log
grep -E 'execve\(' filtered.log | grep -v ENOENT
grep -E 'result.txt|passwd|read\((0|7)|write\((1|3)' filtered.log
grep -E 'exited|SIGCHLD|wait4' trace.log
```

## Program Output

```
<paste output of ./pipeline here>
```

## 1. Sequence of System Calls

Excerpts are from `filtered.log`. Parent PID = `<parent>`, children: `<pid1>` (cat), `<pid2>` (cut), `<pid3>` (sort). Dynamic-loader lines (libraries, locale files) are left out.

### a) Pipe Setup (parent)

```
<paste the 3 pipe2 lines>
```

Expected: `pipe2([3, 4], 0)` (pipe A: cat to cut), `pipe2([5, 6], 0)` (pipe B: cut to sort), `pipe2([7, 8], 0)` (pipe C: sort to parent).

### b) Process Creation (parent)

```
<paste the 3 clone(... SIGCHLD ...) lines>
```

### c) Redirection and Closing Unused Ends (each child), then execve

```
<paste for each child: its dup2 lines, close(3)..close(8), and the successful execve>
```

Before each successful `execve`, there may be failed attempts ending in `= -1 ENOENT`. This is `execvp()` trying each directory in `PATH` until it finds the program.

Parent closing the pipe ends it does not use:

```
<paste the parent's close(3), close(4), close(5), close(6), close(8)>
```

### d) Data Flow and File Operations

```
<paste: cat's openat("/etc/passwd"), cut's and sort's read(0)/write(1) lines,
the parent's openat("result.txt", O_WRONLY|O_CREAT|O_TRUNC), read(7), write(3), read(7, "", ...) = 0,
close(7), close(3), openat("result.txt", O_RDONLY), read(3) lines>
```

Note: if `cat` on your system is the Rust coreutils version (`ls -l /usr/bin/cat`), it copies the file with `splice()` instead of `read()`/`write()`. This shows only in `trace.log`.

### e) Process Termination (from `trace.log`, with timestamps)

```
<paste the "+++ exited with 0 +++" lines of the children, the SIGCHLD lines and the 3 wait4 lines>
```

## 2. Role of Each Key Call

| Call | Role |
|------|------|
| clone / fork | `fork()` in glibc is implemented as `clone(... SIGCHLD)`. It creates a copy of the parent, including a copy of its file descriptor table. (Any `clone3` calls inside `sort` are `sort` creating its own threads, not new processes.) |
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

**EOF:** The parent loops `while ((n = read(p[2][0], buf, 1024)) > 0)`. When `sort` exits, the last write end of pipe C (its fd 1) is closed. The parent and the other children have already closed their copies. The kernel then returns 0 from `read()`, which is EOF: `read(7, "", 1024) = 0`. The same happens down the chain: `cat` exits, so `cut` sees EOF; `cut` exits, so `sort` sees EOF. (If the trace shows `ERESTARTSYS`, a `read()` was interrupted by `SIGCHLD` and restarted automatically. This does not affect the data.)

**Exit status:** When a child exits, the kernel sends `SIGCHLD` to the parent, and the child stays as a zombie holding its exit code. The parent calls `waitpid(pids[i], &status, 0)`, which appears as `wait4(...)`, once per child. It then decodes the status with `WIFEXITED(status)` and `WEXITSTATUS(status)`, and prints it.
