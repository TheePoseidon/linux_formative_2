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

Run every experiment (3 runs per buffer size, each copy verified):

```bash
for prog in copy_sys copy_stdio; do for size in 512 4096 65536; do for run in 1 2 3; do ./$prog big.bin out.bin $size; cmp big.bin out.bin && echo "  cmp OK"; done; done; done
```

Count system calls:

```bash
strace -c ./copy_sys big.bin out.bin 512
strace -c ./copy_sys big.bin out.bin 4096
strace -c ./copy_sys big.bin out.bin 65536
strace -c ./copy_stdio big.bin out.bin 512
strace -c ./copy_stdio big.bin out.bin 4096
strace -c ./copy_stdio big.bin out.bin 65536
```

Do not commit `big.bin`. It is 100 MiB, which is at GitHub's file size limit.

## Verification

Error test output:

```
<paste output of the error test>
```

Checksums:

```
<paste output of sha256sum big.bin out.bin>
```

## Results

### Elapsed Time (program's own timing, 3 runs)

| Version | Buffer | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) |
|---------|--------|-----------|-----------|-----------|-------------|
| copy_sys | 512 B | | | | |
| copy_sys | 4 KB | | | | |
| copy_sys | 64 KB | | | | |
| copy_stdio | 512 B | | | | |
| copy_stdio | 4 KB | | | | |
| copy_stdio | 64 KB | | | | |

### System Call Counts (`strace -c`)

| Version | Buffer | read calls | read s | read % | write calls | write s | write % | Avg time (s) | Time under strace (s) |
|---------|--------|-----------|--------|--------|-------------|---------|---------|--------------|------------------------|
| copy_sys | 512 B | | | | | | | | |
| copy_sys | 4 KB | | | | | | | | |
| copy_sys | 64 KB | | | | | | | | |
| copy_stdio | 512 B | | | | | | | | |
| copy_stdio | 4 KB | | | | | | | | |
| copy_stdio | 64 KB | | | | | | | | |

Expected counts, to check your numbers against:
- `copy_sys` makes 100 MB / buffer size data reads and writes. That is 204,800 at 512 B, 25,600 at 4 KB and 1,600 at 64 KB. There are a few extra calls: 1 read that returns 0 (EOF), 1 read by the loader to load libc, and 1 write for the `printf` of the time.
- `copy_stdio` at 512 B should show about the same counts as `copy_sys` at 4 KB, because stdio's buffer is 4096 bytes.

### Comparison of read/write in Both Versions

- **512 B:** `<copy_sys calls/seconds/% vs copy_stdio calls/seconds/%; how many times faster stdio is>`
- **4 KB:** `<compare>`
- **64 KB:** `<compare>`

### Note: strace Slows Programs Down

strace stops the program at every system call so it can record it. This adds a large cost to each call, so the times under strace are much higher than the program's own times. For example, `copy_sys` with 512 B takes `<avg>` s normally but `<strace time>` s under strace. The slowdown is worst where there are the most calls. So strace is used to **count** calls, and the program's own `clock_gettime()` timing is used to **compare speed**.

## Explanation

### 1. How fread()/fwrite() Buffering Reduces read()/write() Calls

- `FILE *` keeps its own buffer in user space, usually 4096 bytes (the file system block size, see `stat -c %o big.bin`).
- `fread(buf, 1, 512, in)`: the first call does one `read(fd, stdio_buf, 4096)`. The next 7 calls copy from that buffer with `memcpy`, without entering the kernel. So there is 1 `read()` for every 8 `fread()` calls.
- `fwrite(buf, 1, 512, out)` copies into the stdio buffer. Only when 4096 bytes have built up does it call `write()`. `fclose()` flushes the rest.
- So `copy_stdio` at 512 B makes about the same number of calls as `copy_sys` at 4 KB.
- When the request is the buffer size or larger (64 KB), glibc skips its buffer and reads or writes directly from the user's buffer. The write count can be up to double the read count, because `fwrite` may first flush a 4 KB piece and then write the rest of the block directly. You can see this with `strace -e trace=write ./copy_stdio big.bin out.bin 65536`.

### 2. Why Each System Call Adds Overhead

A library call like `fread()` is an ordinary function call in user mode. A system call like `read()` must enter the kernel:

1. The program runs the `syscall` instruction. The CPU switches from **user mode** (ring 3) to **kernel mode** (ring 0).
2. The kernel saves the program's registers, checks the arguments (valid fd, valid buffer address) and finds the file.
3. The kernel copies the data between the page cache and the user buffer.
4. The kernel restores the registers and switches back to user mode.

Steps 1, 2 and 4 cost the same whether you move 512 bytes or 64 KB. Security protections against Spectre and Meltdown make each switch even more expensive, and each switch also disturbs the CPU caches. With 512 B, `copy_sys` pays this fixed cost about 409,600 times, which is why it should be the slowest case.

### 3. Why the Performance Gap Shrinks as the Buffer Grows

Total time is roughly:

```
time = (number of system calls x fixed cost per call) + (bytes copied x copy cost)
```

- The second part is the same for both versions, because both move 100 MB.
- With a small buffer the first part dominates. stdio wins because it turns 8 small requests into 1 system call.
- As the buffer grows, `copy_sys` makes fewer calls (about 409,600, then 51,200, then 3,200). The fixed cost becomes small, so both versions spend most of their time just copying data.
- At 4 KB, stdio's buffer is no bigger than the user's buffer, so it saves no calls and only adds an extra `memcpy`.
- At 64 KB, stdio skips its buffer, so both versions behave almost the same.

The benefit of stdio buffering is that it removes system calls. Once the program's own buffer is large, there are few system calls left to remove, so the gap disappears.

Note: `big.bin` is usually in the page cache (RAM) after `dd`, so the times mostly show system call and copy cost, not disk speed.
