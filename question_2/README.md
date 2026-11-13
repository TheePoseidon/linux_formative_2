# Question 2: System Call vs stdio File Copy

- `copy_sys.c` uses only `open()`, `read()`, `write()`, `close()`.
- `copy_stdio.c` uses only `fopen()`, `fread()`, `fwrite()`, `fclose()`.
- Both take `<source> <destination> <buffer_size>`, report errors with `perror()`, and time themselves with `clock_gettime(CLOCK_MONOTONIC)`.

## Compile and Run

```bash
cd question_2
gcc -Wall -o copy_sys copy_sys.c
gcc -Wall -o copy_stdio copy_stdio.c

dd if=/dev/urandom of=big.bin bs=1M count=100

./copy_sys big.bin out.bin 4096
cmp big.bin out.bin && echo "copy OK"

./copy_stdio big.bin out.bin 4096
sha256sum big.bin out.bin
```

Error test:

```bash
./copy_sys missing.bin out.bin 4096
./copy_stdio missing.bin out.bin 4096
```

Experiment (3 runs per buffer size, each copy verified):

```bash
for prog in copy_sys copy_stdio; do for size in 512 4096 65536; do for run in 1 2 3; do ./$prog big.bin out.bin $size; cmp big.bin out.bin && echo "  cmp OK"; done; done; done
```

Count system calls (results saved in `strace_<program>_<size>.txt`):

```bash
for prog in copy_sys copy_stdio; do for size in 512 4096 65536; do strace -c -o strace_${prog}_${size}.txt ./$prog big.bin out.bin $size; done; done
```

Do not commit `big.bin` or `out.bin` (100 MiB each).

## Verification

Error test:

```
missing.bin: No such file or directory
missing.bin: No such file or directory
```

All 18 timed copies printed `cmp OK`. Checksums:

```
a010947919442458d644554eb3af5fb55e0b085d9ae7cc4eb24e83514ee9ab66  big.bin
a010947919442458d644554eb3af5fb55e0b085d9ae7cc4eb24e83514ee9ab66  out.bin
```

## Results

### Elapsed Time (3 runs)

| Version | Buffer | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) |
|---------|--------|-----------|-----------|-----------|-------------|
| copy_sys | 512 B | 1.872 | 1.455 | 1.805 | **1.711** |
| copy_sys | 4 KB | 0.511 | 0.490 | 0.488 | **0.497** |
| copy_sys | 64 KB | 0.242 | 0.245 | 0.176 | **0.221** |
| copy_stdio | 512 B | 0.483 | 0.489 | 0.539 | **0.504** |
| copy_stdio | 4 KB | 0.477 | 0.444 | 0.494 | **0.472** |
| copy_stdio | 64 KB | 0.288 | 0.275 | 0.288 | **0.284** |

### System Calls (`strace -c`) and Times

| Version | Buffer | read calls | read s | read % | write calls | write s | write % | Avg time (s) | Under strace (s) |
|---------|--------|-----------:|-------:|-------:|------------:|--------:|--------:|-------------:|-----------------:|
| copy_sys | 512 B | 204,802 | 0.871 | 31.45 | 204,801 | 1.837 | 66.37 | 1.711 | 14.734 |
| copy_sys | 4 KB | 25,602 | 0.125 | 23.00 | 25,601 | 0.318 | 58.48 | 0.497 | 2.405 |
| copy_sys | 64 KB | 1,602 | 0.018 | 19.01 | 1,601 | 0.040 | 42.35 | 0.221 | 0.285 |
| copy_stdio | 512 B | 25,602 | 0.116 | 23.54 | 25,601 | 0.289 | 58.86 | 0.504 | 2.408 |
| copy_stdio | 4 KB | 25,602 | 0.114 | 21.94 | 25,601 | 0.297 | 57.22 | 0.472 | 2.316 |
| copy_stdio | 64 KB | 1,602 | 0.021 | 12.38 | 3,200 | 0.088 | 48.70 | 0.284 | 0.666 |

- 100 MB / 512 B = 204,800 data reads and writes.
- The extra read calls are 1 EOF read (returns 0) and 1 read by the loader of libc. The extra write call is the `printf`.
- At 64 KB, read + write show a lower % because `openat` takes up to 29% of the time. Opening the existing 100 MB `out.bin` with `O_TRUNC` has to free its blocks.

### Comparison

- **512 B:** `copy_stdio` makes **8× fewer** calls (25,602 vs 204,802 reads). Kernel time in read+write drops from 2.71 s to 0.41 s, and the copy is **3.4× faster** (0.504 s vs 1.711 s).
- **4 KB:** both make the **same** number of calls (25,602 / 25,601), with similar seconds and %. The times are almost equal (0.472 s vs 0.497 s).
- **64 KB:** both make 1,602 reads, but `copy_stdio` makes 3,200 writes (2 per block). `copy_sys` is now slightly faster (0.221 s vs 0.284 s).

### strace Slows Programs Down

strace stops the program at every system call to record it, so times under strace are much higher. For example, `copy_sys` at 512 B takes 1.711 s normally and **14.734 s** under strace, about 9× slower. The slowdown grows with the number of calls. So strace is used only to **count** calls, and `clock_gettime()` is used to **compare speed**.

## Explanation

### 1. How fread()/fwrite() Buffering Reduces read()/write() Calls

- Each `FILE *` has its own 4096-byte buffer in user space.
- `fread(buf, 1, 512, in)` fills that buffer with **one** `read(fd, ..., 4096)`. The next 7 `fread` calls are served from the buffer by `memcpy`, without a system call.
- `fwrite()` copies into the buffer and calls `write()` only when 4096 bytes have built up. `fclose()` flushes the rest.
- So `copy_stdio` at 512 B makes the same 25,601 calls as `copy_sys` at 4 KB.
- For requests of 4096 bytes or more, glibc skips its buffer. At 64 KB, `fread` reads directly (1,602 reads). `fwrite` writes one 4096-byte piece and then the remaining 61,440 bytes, which is why there are 3,200 writes.

### 2. Why Each System Call Adds Overhead

`fread()` is a normal function call in **user mode**. `read()` must enter the **kernel**:

1. The `syscall` instruction switches the CPU from user mode to **kernel mode**.
2. The kernel saves the registers and checks the arguments (fd, buffer address).
3. The kernel copies the data between the page cache and the user buffer.
4. The kernel restores the registers and switches back to user mode.

Steps 1, 2 and 4 are a **fixed cost per call**, whether 512 bytes or 64 KB are moved. Security protections (Spectre/Meltdown mitigations) make each switch more expensive. `copy_sys` at 512 B pays this cost about 409,600 times, so it is the slowest (1.711 s).

### 3. Why the Performance Gap Shrinks as the Buffer Grows

```
time ≈ (number of system calls × fixed cost per call) + (100 MB × copy cost)
```

- The copy cost is the same for both versions.
- **Small buffer:** the call cost dominates. stdio saves 7 of every 8 calls, so it is 3.4× faster.
- **Larger buffer:** `copy_sys` itself makes far fewer calls (409,603, then 51,203, then 3,203). There is little left for stdio to save, and the extra `memcpy` into its own buffer costs time.
- **4 KB:** equal calls, so equal speed.
- **64 KB:** stdio makes extra writes, so `copy_sys` is slightly faster.

Once the program's own buffer is large, the system call overhead is small, so buffering no longer helps and the gap disappears.

Note: `big.bin` was in the page cache (RAM), so these times show system call and copy cost, not disk speed.
