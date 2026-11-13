# Question 4: Log Scanner with a Thread Pool

- `logscan.c` creates a fixed pool of worker threads. The file names on the command line form the shared work queue. Each worker takes the next file, counts lines with `fgets()` and `strstr()`, writes one line to the report, and repeats until the queue is empty.
- There are three separate mutexes, one per shared resource:
  - `queue_lock` protects the work queue (`next_file`).
  - `report_lock` protects the report file (`report`).
  - `total_lock` protects the grand totals (`grand_total`, `files_processed`).
- `gen_logs.sh` creates 12 log files (1, 2, 3, 5, 8, 10, 15, 20, 25, 30, 40 and 50 MB, 209 MB in total) in `logs/`. Each line is `INFO`, `WARN`, `ERROR` or `DEBUG`, picked at random.
- The pattern used is `ERROR`.
- The single-threaded baseline is the same program run with 1 thread.

## Compile and Run

```bash
cd question_4
gcc -Wall -o logscan logscan.c -pthread
bash gen_logs.sh
./logscan ERROR report.txt 4 logs/*.log
cat report.txt
```

Error test (a missing file is reported and the rest are still processed):

```bash
./logscan ERROR report.txt 4 logs/*.log missing.log
```

Verify the total against grep:

```bash
grep -c ERROR logs/*.log | awk -F: '{sum += $2} END {print "grep total:", sum}'
```

Experiment (1, 2, 4 and 8 threads, 3 runs each):

```bash
./logscan ERROR report.txt 1 logs/*.log > /dev/null
for t in 1 2 4 8; do for r in 1 2 3; do ./logscan ERROR report.txt $t logs/*.log | grep elapsed; done; done
```

The first command is a warm-up run that loads the files into the page cache, so all timed runs start from the same state.

Number of CPU cores:

```bash
nproc
```

Do not commit the `logs/` folder (209 MB). It can be recreated with `bash gen_logs.sh`.

## Output

`report.txt` (4 threads):

```
<paste report.txt here>
```

Screen:

```
<paste screen output here>
```

Error test:

```
<paste output of the error test>
```

Verification:

```
<paste grep total>
```

`<State whether the grep total equals the program's total.>`

## Results

CPU cores (`nproc`): `<fill in>`

| Threads | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average Tn (s) | Speedup T1 / Tn |
|---------|-----------|-----------|-----------|----------------|-----------------|
| 1 | | | | | 1.00 |
| 2 | | | | | |
| 4 | | | | | |
| 8 | | | | | |

## 1. Why the Mutexes Are Needed

**Work queue (`queue_lock`):** Taking a file is two steps: read `files[next_file]`, then do `next_file++`. Without the lock:
- Two workers can read the same `next_file`, so both process the **same file**. It is counted twice, and a file is skipped because `next_file` ends up past it.
- Or both increment at the same time, one increment is lost, and the same file is given out again.

**Report file (`report_lock`):** `fprintf()` to one shared `FILE *` changes the stdio buffer and its position. Without the lock, two workers writing at once could **interleave** their text into one broken line, or overwrite each other's data in the buffer so that a result line is **lost**. The lock makes each result line one complete write. (glibc does lock inside a single `fprintf`, but the program should not rely on that, and the task requires a mutex.)

**Grand total (`total_lock`):** `grand_total += matching` is load, add, store. If two workers load the same old value, both store their own sum and one file's matches are **lost**. The final total would be too small and would not match `grep -c`. This is a **race condition**: the result depends on thread timing.

**Why separate mutexes:** A worker updating the total does not block another worker that is writing to the report or taking the next file. Each lock is held only for the short time its own resource is used, so there is less waiting.

All three mutexes are created with `pthread_mutex_init()` before the threads start and released with `pthread_mutex_destroy()` after `pthread_join()`.

## 2. Why a Thread Pool with a Shared Queue Balances Load Better

- **One file per thread (static):** with 12 files of 1–50 MB, the thread given the 50 MB file works about 50 times longer than the thread given the 1 MB file. Threads with small files finish and sit idle. Also, one thread per file means 12 threads even if there are only 4 cores.
- **Thread pool with a shared queue (dynamic):** a worker that finishes a small file immediately takes the next file. Fast workers end up processing more files, and slow workers fewer. All threads stay busy until the queue is empty, so they finish at nearly the same time.
- The report shows this. The `(thread <id>)` column shows some threads processing several small files while another processes one big file.
- **Limit:** a single file is never split between threads. The run can never be shorter than the time to scan the largest file (50 MB of 209 MB), so the best possible speedup is about 209 / 50 ≈ **4.2×**. Because `logs/*.log` lists the files from smallest to largest, the 50 MB file is taken last, which leaves one thread working alone at the end. Putting the largest files first in the queue would reduce this.

## 3. Why the Speedup Stops Growing

`<Fill in what your table shows, e.g. "speedup rises from 1 to 2 to 4 threads, then stays flat or drops at 8".>`

Reasons:

1. **Uneven file sizes:** as explained above, the 50 MB file caps the speedup at about 4.2×. With 8 threads, most threads have no work left while the last big file is still being scanned, so going from 4 to 8 threads adds little.
2. **Disk I/O:** if the files are not in memory, all threads read from the same disk. The disk has a fixed speed. Once it is the bottleneck, more threads just wait in the disk queue, and on a hard disk, jumping between files can make it slower.
3. **Page cache:** after the warm-up run, the files are in RAM, so reading costs only a memory copy and the work becomes CPU-bound (`fgets` + `strstr`). This is the best case for threads. If the cache is cold (first run, or after `sync; echo 3 | sudo tee /proc/sys/vm/drop_caches`), times are much higher and the speedup is much lower, because the disk is the limit.
4. **Mutex contention:** each file takes the locks only 3 times (queue, report, total), so contention is very low. It is not the main limit here. It would matter only with many tiny files, or if a lock were taken for every line.
5. **CPU cores and overhead:** if there are fewer cores than threads (`nproc`), extra threads only take turns on the same cores. Thread creation and context switching add a small cost. Threads also share memory bandwidth, and reading 209 MB at once can fill it.

## Notes

- Lines longer than 4095 characters would be counted as more than one line by `fgets()`. The generated log lines are about 70 characters, so this does not affect the results.
- The order of lines in `report.txt` changes from run to run, because workers finish their files at different times.
- The summary counts the files that were processed successfully, so a missing file is not included in `<N>`.
