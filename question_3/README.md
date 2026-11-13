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
nproc
```

## Output (run 1)

```
Perfect numbers found: 6 28 496 8128
The synchronized total number of perfect numbers between 1 and 100000 is 4
Single-threaded time: 0.103560 seconds
Multi-threaded time:  0.033920 seconds
```

All 3 runs found the same 4 numbers. The full output is in `output.txt`.

## Timing

CPU cores (`nproc`): **4**

| Run | Single-threaded (s) | Multi-threaded (s) | Speedup |
|-----|---------------------|--------------------|---------|
| 1 | 0.1036 | 0.0339 | 3.05× |
| 2 | 0.0885 | 0.0369 | 2.40× |
| 3 | 0.0825 | 0.0383 | 2.15× |
| **Average** | **0.0915** | **0.0364** | **2.52×** |

## 1. Why a Mutex Is Needed (Race Condition)

All 8 threads share `total_count`, `perfect[]` and `perfect_size`.

- `total_count += local_count` is three steps: **load**, **add**, **store**. If two threads load the same old value, both store their own result, and one thread's count is **lost**.
- `perfect[perfect_size] = n; perfect_size++;` has the same problem. Two threads can write into the **same slot**, so one number is overwritten and a slot is left as 0, or the size comes out wrong.

This is a **race condition**: the result depends on thread timing. `pthread_mutex_lock()` / `pthread_mutex_unlock()` let only one thread at a time update the shared data. The mutex is set up with `pthread_mutex_init()` before the threads start and released with `pthread_mutex_destroy()` after they are joined.

## 2. Why a Local Count Plus One Lock Is More Efficient

- `local_count` is private to each thread, so it needs no lock.
- Each thread locks **once** at the end: 8 locks in total, plus 4 for the array, one per perfect number found.
- Locking for every number checked would mean **100,000** lock/unlock pairs. Each costs time, and while one thread holds the lock the others must **wait**. The threads would queue for the lock instead of working in parallel, which would remove most of the speedup.

## 3. Single vs Multi-threaded Time

The multi-threaded version is on average **2.52× faster** (0.0915 s vs 0.0364 s) on 4 cores. The maximum possible is 4×, because only 4 threads can run at the same moment. The other 4 threads wait their turn.

Why it is below 4×:
- **Overhead:** creating and joining 8 threads, and switching between them on 4 cores, takes time.
- **Short job:** the search takes only about 0.09 s, so this overhead is a noticeable part of the total.
- **Uneven work:** threads 7 and 8 have the most work (see section 4) and finish last. For part of the time, some cores sit idle.
- **Run-to-run variation:** the single-threaded time dropped from 0.104 s in run 1 to 0.083 s in run 3. This is caused by other processes and changes in CPU speed.

## 4. Workload Balance

Equal-sized segments do **not** give equal work. Checking `n` takes about √n steps, so segments with larger numbers take longer. The relative work of segment `k` is about `k^1.5 − (k−1)^1.5`:

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
- On 8 cores this imbalance alone would cap the speedup at 100 / 18.2 ≈ **5.5×** instead of 8×.
- On this 4-core machine the effect is smaller. With 8 threads on 4 cores, a core that finishes a light thread can run another one, which partly hides the imbalance. Even so, the heavy threads still finish last.
- **Better balance:** give numbers out in turns (thread `t` checks `t+1, t+9, t+17, ...`), so every thread gets a mix of small and large numbers.
