# Question 3: Perfect Numbers with POSIX Threads

`perfect_num.c` searches 1 to 100,000 for perfect numbers twice:
- **Single-threaded:** one loop from 1 to 100,000.
- **Multi-threaded:** 8 threads, each given a `Range` struct with 12,500 numbers: 1–12500, 12501–25000, ..., 87501–100000.

`is_perfect(n)` loops `i` from 2 while `i * i <= n` (up to √n). Each divisor `i` found adds both `i` and `n / i` to the sum, adding `i` only once when `i == n / i`.

## Compile and Run

```bash
cd question_3
gcc -Wall -o perfect perfect_num.c -pthread
./perfect
```

Check how many CPU cores your machine has (needed for the speedup discussion):

```bash
nproc
```

## Output

```
<paste output of ./perfect here>
```

Expected first two lines:

```
Perfect numbers found: 6 28 496 8128
The synchronized total number of perfect numbers between 1 and 100000 is 4
```

## Timing (run `./perfect` 3 times)

| Run | Single-threaded (s) | Multi-threaded (s) | Speedup (single / multi) |
|-----|---------------------|--------------------|--------------------------|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| Average | | | |

CPU cores (`nproc`): `<fill in>`

## 1. Why a Mutex Is Needed (Race Condition)

All 8 threads share `total_count`, `perfect[]` and `perfect_size`.

- `total_count += local_count` is not one step. The CPU does it in three: **load** the value, **add**, **store** it back. If two threads load the same old value (say 1) at the same time, both store 2, and one thread's count is lost. This is a **race condition**: the result depends on the timing of the threads.
- `perfect[perfect_size] = n; perfect_size++;` has the same problem. Two threads can read the same `perfect_size`, write their numbers into the **same slot**, and then both increment. One perfect number is overwritten, and a slot is left as 0, or the size comes out wrong.

`pthread_mutex_lock()` lets only one thread at a time into the code between lock and unlock (the **critical section**), so these updates cannot interleave. The mutex is created once with `pthread_mutex_init()` before the threads start, and released with `pthread_mutex_destroy()` after all threads are joined.

## 2. Why a Local Count Plus One Lock Is More Efficient

- Each thread counts in `local_count`, a variable on its own stack that no other thread touches, so it needs no lock.
- Each thread locks **once** at the end to add its count. That is 8 lock/unlock pairs in total.
- Locking for every number checked would mean 100,000 lock/unlock pairs. Each one costs time, and when the lock is held, the other threads must **wait**. The threads would spend much of their time queuing for the lock instead of working in parallel, which wipes out most of the speedup.
- The shared array is still locked per perfect number found, but there are only 4 perfect numbers, so that is only 4 extra locks.

## 3. Single vs Multi-threaded Time

`<Fill in: average single time, average multi time, speedup = single / multi, and the number of cores.>`

What to expect and why:
- With 8 threads on a machine with 8 or more cores, the threads run truly in parallel, so the multi-threaded version should be several times faster.
- The speedup is **less than 8** for these reasons:
  - **Uneven work (see section 4):** the total time is set by the slowest thread, which has about 18% of the work. That limits the speedup to about **5.5×**, even with perfect hardware.
  - **Overhead:** creating and joining 8 threads, plus locking, takes time.
  - **Short job:** the whole search takes only milliseconds, so this overhead is a noticeable share of the total.
  - **Fewer cores:** if `nproc` is less than 8, the threads take turns on the cores, which lowers the speedup further.
- Times change slightly between runs because of other processes and CPU frequency changes. That is why the average of 3 runs is used.

## 4. Workload Balance

Equal-sized segments do **not** give equal work. Checking `n` takes about √n loop steps, so bigger numbers cost more. The work in a segment is roughly the sum of √n over it, which is proportional to `k^1.5 − (k−1)^1.5` for segment `k`:

| Thread | Range | Relative work | Share of total |
|--------|-------|---------------|----------------|
| 1 | 1 – 12,500 | 1.00 | 4.4% |
| 2 | 12,501 – 25,000 | 1.83 | 8.1% |
| 3 | 25,001 – 37,500 | 2.37 | 10.5% |
| 4 | 37,501 – 50,000 | 2.80 | 12.4% |
| 5 | 50,001 – 62,500 | 3.18 | 14.1% |
| 6 | 62,501 – 75,000 | 3.52 | 15.5% |
| 7 | 75,001 – 87,500 | 3.82 | 16.9% |
| 8 | 87,501 – 100,000 | 4.11 | 18.2% |

- Thread 8 does about **4 times** the work of thread 1.
- Thread 1 finishes early and its core sits idle while thread 8 is still working.
- The program ends only when the last thread ends, so the best possible speedup is 100% / 18.2% ≈ **5.5×** instead of 8×.

To balance the work better:
- Make the segments smaller for larger numbers, or
- Give numbers out in turns: thread `t` checks `t+1, t+9, t+17, ...`. Each thread then gets a mix of small and large numbers.
