# Question 4: Log Scanner with a Thread Pool

- `logscan.c` starts a fixed pool of worker threads. The file names from the command line form the shared work queue. Each worker takes the next file, counts lines with `fgets()` + `strstr()`, writes one report line, and repeats until the queue is empty.
- There are three separate mutexes, one per shared resource:
  - `queue_lock` protects the queue (`next_file`).
  - `report_lock` protects `report.txt`.
  - `total_lock` protects the grand totals.
- `gen_logs.sh` creates 12 logs in `logs/`: 1, 2, 3, 5, 8, 10, 15, 20, 25, 30, 40 and 50 MB (209 MB total).
- The pattern is `ERROR`. The single-threaded baseline is the same program run with 1 thread.

## Compile and Run

```bash
cd question_4
gcc -Wall -o logscan logscan.c -pthread
bash gen_logs.sh
./logscan ERROR report.txt 4 logs/*.log
```

Error test:

```bash
./logscan ERROR report_error.txt 4 logs/*.log missing.log
```

Verify with grep:

```bash
grep -c ERROR logs/*.log | awk -F: '{sum += $2} END {print "grep total:", sum}'
```

Experiment (a warm-up run, then 3 runs for each thread count):

```bash
./logscan ERROR timing_report.txt 1 logs/*.log > /dev/null
for t in 1 2 4 8; do for r in 1 2 3; do ./logscan ERROR timing_report.txt $t logs/*.log | grep elapsed; done; done
nproc
```

Do not commit `logs/` (209 MB). `bash gen_logs.sh` recreates it.

## Output

`report.txt` (4 threads):

```
logs/file01.log: 2153 matching lines out of 17301 (thread 3)
logs/file03.log: 6375 matching lines out of 51544 (thread 1)
logs/file02.log: 4302 matching lines out of 34421 (thread 2)
logs/file05.log: 17225 matching lines out of 136539 (thread 3)
logs/file06.log: 21146 matching lines out of 170233 (thread 1)
logs/file04.log: 10797 matching lines out of 85779 (thread 4)
logs/file08.log: 42153 matching lines out of 338682 (thread 3)
logs/file09.log: 52961 matching lines out of 422895 (thread 1)
logs/file07.log: 31818 matching lines out of 254454 (thread 2)
logs/file10.log: 63284 matching lines out of 507128 (thread 4)
logs/file11.log: 84564 matching lines out of 675572 (thread 3)
logs/file12.log: 105534 matching lines out of 844011 (thread 1)
Total matching lines across 12 files: 442312
```

Screen:

```
Total matching lines across 12 files: 442312
Threads: 4, elapsed time: 0.167452 seconds
```

Error test (the missing file is reported and the other 12 are still processed):

```
missing.log: No such file or directory
Total matching lines across 12 files: 442312
Threads: 4, elapsed time: 0.210563 seconds
```

## Verification

```
grep total: 442312
```

- The program's total of **442,312** matching lines equals the `grep -c` total.
- The total line count (3,538,559) also equals `wc -l logs/*.log`.

## Results

CPU cores (`nproc`): **4**

| Threads | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average Tn (s) | Speedup T1 / Tn |
|---------|-----------|-----------|-----------|----------------|-----------------|
| 1 | 0.2843 | 0.2735 | 0.2943 | **0.2840** | 1.00× |
| 2 | 0.2013 | 0.1786 | 0.1700 | **0.1833** | 1.55× |
| 4 | 0.1596 | 0.1431 | 0.1424 | **0.1484** | 1.91× |
| 8 | 0.1651 | 0.1438 | 0.1361 | **0.1483** | 1.91× |

## 1. Why the Mutexes Are Needed

- **Work queue:** taking a file means reading `files[next_file]` and then doing `next_file++`. Without the lock, two workers can read the same index. One file is then **processed twice** and another is **skipped**, and the totals come out wrong.
- **Report file:** all workers write to one shared `FILE *`. Without the lock, two `fprintf()` calls at once could **interleave** into one broken line, or overwrite each other in the buffer so that a line is **lost**. The lock makes each result line one complete write.
- **Grand total:** `grand_total += matching` is load, add, store. Two workers can load the same old value, and then one file's matches are **lost**. The total would no longer match `grep`.

These are **race conditions**: the result depends on thread timing. Using a **separate mutex** for each resource means a worker updating the total does not block a worker that is writing the report or taking a file.

## 2. Why a Thread Pool with a Shared Queue Balances Load Better

- **One file per thread:** the thread given the 50 MB file works about 50 times longer than the thread given the 1 MB file, while the others sit idle. It would also need 12 threads on 4 cores.
- **Shared queue:** a worker that finishes takes the next file straight away. In `report.txt`, threads 2 and 4 each processed 2 files while threads 1 and 3 processed 4 each. Every thread stayed busy until the queue was empty.
- **Limit:** a single file is never split, and the files are queued from smallest to largest. In this run thread 1 ended up with file03 + file06 + file09 + file12 ≈ 88 MB of the 209 MB. This caps the 4-thread speedup at about 209 / 88 ≈ **2.4×**. Putting the largest files first in the queue would spread the work more evenly.

## 3. Why the Speedup Stops Growing

The speedup rises from 1.55× at 2 threads to 1.91× at 4 threads, then **stays flat** at 1.91× with 8 threads.

- **CPU cores:** there are only 4 cores. Threads 5 to 8 cannot run at the same time as the others, so they just take turns on the same 4 cores and add context-switch cost.
- **Uneven file sizes:** the large files come last in the queue, so near the end one or two threads are still scanning `file11` / `file12` while the rest have nothing to do (section 2).
- **Page cache and disk I/O:** after the warm-up run, the 209 MB of logs are in RAM (page cache), so the disk is not the limit here. The work is copying memory plus `fgets`/`strstr`, and all threads share the same memory bandwidth, which limits the speedup. With a cold cache, all threads would wait on the same disk and the speedup would be even lower.
- **Mutex contention:** each file takes only 3 locks (36 in total), so contention is negligible here. It would only matter with very many small files or a lock per line.
