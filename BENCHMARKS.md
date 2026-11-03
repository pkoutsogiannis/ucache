# UCache benchmarks

How fast each version of ucache reads and writes, when one process or several call the cache back to back, against the
version before it (1.0.0, 1.0.1, 1.1.0, 1.1.1), and what `ucache.lockedcopy=0` changes in 1.1.1. Measured on 2026-11-02,
on PHP 7.2 and 8.4 (1.0.0 on PHP 7.2 only, the one version it builds for).

**Reading the tables:** each figure compares a version with the one before it. `+12%` means 12% more calls a second,
`×2.4` means 2.4 times as many, `−68%` means 68% fewer, and drops of 20% or more are in bold. The summary tables give
the geometric mean over the six workloads; the tables folded under each summary give the calls a second of each
workload. A difference of less than about 10% can be noise, and of less than about 15% on PHP 7.2 (see
[How it was measured](#how-it-was-measured)).

## At a glance

PHP 7.2, the six workloads together. One process:

| Load | 1.0.0 → 1.0.1 | 1.0.1 → 1.1.0 | 1.1.0 → 1.1.1 |
|---|--:|--:|--:|
| Gets only | +89% | +32% | +2% |
| 1% sets | ×4.0 | +39% | +5% |
| 5% sets | ×5.8 | +82% | +11% |
| 50% sets | ×7.8 | ×3.7 | +67% |
| 80% sets | ×8.5 | ×4.2 | +87% |
| Sets only | ×7.2 | ×4.4 | ×2.0 |

4 and 8 processes:

| Load | 1.0.0 → 1.0.1 | 1.0.1 → 1.1.0 | 1.1.0 → 1.1.1 |
|---|--:|--:|--:|
| Gets only | +77% | +33% | +4% |
| 1% sets | ×3.2 | +30% | +4% |
| 5% sets | ×3.9 | +68% | +7% |
| One writer beside readers: the readers' gets | ×12 | +21% | +2% |
| One writer beside readers: the writer's sets | ×4.5 | +64% | ×2.0 |
| 50% sets | ×3.1 | ×6.6 | +57% |
| 80% sets | ×2.8 | ×7.0 | ×2.4 |
| Sets only | ×2.8 | ×7.0 | ×2.8 |
| One reader beside writers: the reader's gets | ×340 | **−52%** | **−68%** |
| One reader beside writers: the writers' sets | ×2.8 | ×8.0 | ×2.2 |

- **Reads:** 1.0.1 reads integers and strings as fast as 1.0.0, and arrays several times as fast, as 1.0.0 leaks a copy
  of every array it reads. 1.1.0 reads about a third faster on PHP 7.2 (60% on PHP 8.4), with 82 times as many hash
  buckets. 1.1.1 reads as fast as 1.1.0.
- **Writes:** on average, each version writes several times as fast as the one before it: with one process, 7.2 times as
  many sets with 1.0.1, 4.4 times with 1.1.0 and 2.0 times with 1.1.1.
- **A reader beside writers** that write back to back is the one load that gets slower, twice: from 1.0.1 to 1.1.0
  (−52%) and from 1.1.0 to 1.1.1 (−68%), as the writers make 8 and 2.2 times as many sets, each of which takes the
  cache lock. 1.0.0 was far slower there, as its writers held the cache lock while they replaced entries (6.6 ms for a
  row among 20,000), and `ucache.lockedcopy=0` takes most of 1.1.1's added wait away.

## 1.1.0 → 1.1.1

- **Reads:** as fast as 1.1.0, within the noise (+2% to +4% with gets only): a get takes the same path, and finds its
  entry in one block of shared memory.
- **Writes:** an entry is one block, allocated in one hold of the allocator's lock, where 1.1.0 took that lock again for
  each block of the entry, and a value whose entry takes at most `ucache.lockedcopy` bytes (2048 by default) is copied
  in under the cache lock, with its insert. One process sets small values 2.3 to 2.9 times as fast, and large ones 13 to
  32% faster. With 1% or 5% sets, 1.1.1 made 4 to 16% more calls, with 50% sets 1.6 to 2 times as many, and with 80%
  sets or sets only 1.9 to 2.9 times as many.
- **Large values side by side:** a value larger than `ucache.lockedcopy` is copied in before the cache lock is taken, so
  processes that store such values back to back make more sets together than one process alone: 22,000 sets a second of
  arrays of 100 rows with one process, 77,000 with 4 and 100,000 with 8 (1.1.0: 19,400, 38,700 and 28,800).
- **The one slower load, a reader beside writers:** a process that reads back to back beside 3 or 7 processes that set
  back to back made 68% fewer gets (79% fewer on PHP 8.4). Beside 7 writers of values that 1.1.1 copies under the cache
  lock (integers, rows, strings of 1 KB, the mix), it made 87 to 94% fewer, its median get taking 33 to 52 µs instead
  of 1 to 3: the writers hold the cache lock while they copy, so the reader waits behind each of them. With
  `ucache.lockedcopy=0`, it waits 2 to 8 µs (below).

The changes from 1.1.0 to 1.1.1:

| Load | 1 process | 4 and 8 processes | PHP 8.4: 1 process | PHP 8.4: 4 and 8 processes |
|---|--:|--:|--:|--:|
| Gets only | +2% | +4% | +2% | +2% |
| 1% sets | +5% | +4% | +8% | +6% |
| 5% sets | +11% | +7% | +16% | +8% |
| One writer beside readers: the readers' gets | – | +2% | – | −4% |
| One writer beside readers: the writer's sets | – | ×2.0 | – | +92% |
| 50% sets | +67% | +57% | +96% | +66% |
| 80% sets | +87% | ×2.4 | ×2.3 | ×2.3 |
| Sets only | ×2.0 | ×2.8 | ×2.5 | ×2.7 |
| One reader beside writers: the reader's gets | – | **−68%** | – | **−79%** |
| One reader beside writers: the writers' sets | – | ×2.2 | – | ×2.0 |

<details>
<summary>Gets only (gets a second)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 1.02 M | 1.01 M | −1% | +3% |
| integers | 4 | 332 k | 312 k | −6% | −1% |
| integers | 8 | 276 k | 275 k | 0% | −2% |
| rows of 14 strings | 1 | 634 k | 655 k | +3% | −1% |
| rows of 14 strings | 4 | 204 k | 251 k | +23% | 0% |
| rows of 14 strings | 8 | 183 k | 192 k | +5% | +8% |
| strings of 1 KB | 1 | 824 k | 852 k | +3% | – |
| strings of 1 KB | 4 | 235 k | 263 k | +12% | – |
| strings of 1 KB | 8 | 210 k | 219 k | +4% | – |
| lists of 1000 integers | 1 | 109 k | 114 k | +4% | – |
| lists of 1000 integers | 4 | 78.1 k | 74.7 k | −4% | – |
| lists of 1000 integers | 8 | 69.0 k | 70.8 k | +3% | – |
| arrays of 100 rows | 1 | 25.7 k | 25.5 k | −1% | – |
| arrays of 100 rows | 4 | 26.9 k | 27.3 k | +1% | – |
| arrays of 100 rows | 8 | 26.8 k | 26.0 k | −3% | – |
| mix | 1 | 917 k | 946 k | +3% | +3% |
| mix | 4 | 253 k | 280 k | +11% | +1% |
| mix | 8 | 238 k | 254 k | +7% | +4% |

</details>
<details>
<summary>1% sets (calls a second)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 945 k | 970 k | +3% | +7% |
| integers | 4 | 284 k | 305 k | +7% | +8% |
| integers | 8 | 304 k | 273 k | −10% | +4% |
| rows of 14 strings | 1 | 589 k | 654 k | +11% | +11% |
| rows of 14 strings | 4 | 182 k | 216 k | +19% | +10% |
| rows of 14 strings | 8 | 180 k | 212 k | +18% | +3% |
| strings of 1 KB | 1 | 771 k | 832 k | +8% | – |
| strings of 1 KB | 4 | 232 k | 259 k | +12% | – |
| strings of 1 KB | 8 | 182 k | 203 k | +12% | – |
| lists of 1000 integers | 1 | 108 k | 110 k | +2% | – |
| lists of 1000 integers | 4 | 76.1 k | 73.6 k | −3% | – |
| lists of 1000 integers | 8 | 73.3 k | 72.6 k | −1% | – |
| arrays of 100 rows | 1 | 25.6 k | 24.4 k | −5% | – |
| arrays of 100 rows | 4 | 25.6 k | 26.9 k | +5% | – |
| arrays of 100 rows | 8 | 25.3 k | 24.7 k | −3% | – |
| mix | 1 | 884 k | 971 k | +10% | +6% |
| mix | 4 | 273 k | 277 k | +1% | +5% |
| mix | 8 | 286 k | 269 k | −6% | +3% |

</details>
<details>
<summary>5% sets (calls a second)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 835 k | 990 k | +19% | +16% |
| integers | 4 | 295 k | 295 k | 0% | +6% |
| integers | 8 | 259 k | 268 k | +4% | +6% |
| rows of 14 strings | 1 | 536 k | 593 k | +11% | +17% |
| rows of 14 strings | 4 | 182 k | 216 k | +19% | +6% |
| rows of 14 strings | 8 | 153 k | 199 k | +30% | +3% |
| strings of 1 KB | 1 | 639 k | 801 k | +25% | – |
| strings of 1 KB | 4 | 196 k | 206 k | +5% | – |
| strings of 1 KB | 8 | 163 k | 179 k | +10% | – |
| lists of 1000 integers | 1 | 109 k | 110 k | +1% | – |
| lists of 1000 integers | 4 | 75.3 k | 78.3 k | +4% | – |
| lists of 1000 integers | 8 | 72.3 k | 73.8 k | +2% | – |
| arrays of 100 rows | 1 | 25.1 k | 25.4 k | +1% | – |
| arrays of 100 rows | 4 | 26.2 k | 26.9 k | +2% | – |
| arrays of 100 rows | 8 | 24.5 k | 26.0 k | +6% | – |
| mix | 1 | 786 k | 897 k | +14% | +16% |
| mix | 4 | 256 k | 303 k | +18% | +8% |
| mix | 8 | 249 k | 216 k | −13% | +19% |

</details>
<details>
<summary>One writer beside readers: the readers' gets (gets a second of the 3 or 7 readers together)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 188 k | 193 k | +3% | −12% |
| integers | 8 | 226 k | 239 k | +6% | +2% |
| rows of 14 strings | 4 | 128 k | 134 k | +5% | −8% |
| rows of 14 strings | 8 | 130 k | 156 k | +20% | −5% |
| strings of 1 KB | 4 | 159 k | 172 k | +8% | – |
| strings of 1 KB | 8 | 171 k | 175 k | +2% | – |
| lists of 1000 integers | 4 | 66.7 k | 67.2 k | +1% | – |
| lists of 1000 integers | 8 | 68.6 k | 67.2 k | −2% | – |
| arrays of 100 rows | 4 | 24.3 k | 23.3 k | −4% | – |
| arrays of 100 rows | 8 | 24.0 k | 23.0 k | −4% | – |
| mix | 4 | 180 k | 159 k | −12% | 0% |
| mix | 8 | 216 k | 223 k | +3% | +1% |

</details>
<details>
<summary>One writer beside readers: the writer's sets (sets a second of the writer)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 31.3 k | 64.3 k | ×2.1 | +75% |
| integers | 8 | 16.2 k | 34.2 k | ×2.1 | ×2.0 |
| rows of 14 strings | 4 | 21.2 k | 44.6 k | ×2.1 | +85% |
| rows of 14 strings | 8 | 9.21 k | 22.3 k | ×2.4 | +89% |
| strings of 1 KB | 4 | 26.3 k | 57.2 k | ×2.2 | – |
| strings of 1 KB | 8 | 12.2 k | 24.9 k | ×2.0 | – |
| lists of 1000 integers | 4 | 11.0 k | 22.2 k | ×2.0 | – |
| lists of 1000 integers | 8 | 4.84 k | 9.48 k | +96% | – |
| arrays of 100 rows | 4 | 4.14 k | 6.75 k | +63% | – |
| arrays of 100 rows | 8 | 1.71 k | 3.03 k | +77% | – |
| mix | 4 | 29.9 k | 52.8 k | +77% | ×2.0 |
| mix | 8 | 15.4 k | 31.7 k | ×2.1 | ×2.0 |

</details>
<details>
<summary>50% sets (calls a second)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 389 k | 773 k | +99% | +78% |
| integers | 4 | 177 k | 280 k | +58% | +53% |
| integers | 8 | 167 k | 263 k | +58% | +56% |
| rows of 14 strings | 1 | 210 k | 423 k | ×2.0 | ×2.1 |
| rows of 14 strings | 4 | 93.9 k | 163 k | +74% | +64% |
| rows of 14 strings | 8 | 76.6 k | 155 k | ×2.0 | ×2.1 |
| strings of 1 KB | 1 | 287 k | 602 k | ×2.1 | – |
| strings of 1 KB | 4 | 119 k | 222 k | +87% | – |
| strings of 1 KB | 8 | 103 k | 183 k | +78% | – |
| lists of 1000 integers | 1 | 83.3 k | 100 k | +20% | – |
| lists of 1000 integers | 4 | 78.9 k | 110 k | +39% | – |
| lists of 1000 integers | 8 | 73.0 k | 105 k | +44% | – |
| arrays of 100 rows | 1 | 21.6 k | 23.5 k | +9% | – |
| arrays of 100 rows | 4 | 39.0 k | 44.4 k | +14% | – |
| arrays of 100 rows | 8 | 38.9 k | 43.4 k | +12% | – |
| mix | 1 | 328 k | 655 k | ×2.0 | +98% |
| mix | 4 | 143 k | 245 k | +71% | +63% |
| mix | 8 | 128 k | 219 k | +72% | +62% |

</details>
<details>
<summary>80% sets (calls a second)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 294 k | 627 k | ×2.1 | ×2.2 |
| integers | 4 | 112 k | 240 k | ×2.1 | ×2.0 |
| integers | 8 | 107 k | 228 k | ×2.1 | ×2.0 |
| rows of 14 strings | 1 | 151 k | 346 k | ×2.3 | ×2.4 |
| rows of 14 strings | 4 | 55.1 k | 158 k | ×2.9 | ×2.9 |
| rows of 14 strings | 8 | 51.9 k | 148 k | ×2.9 | ×2.7 |
| strings of 1 KB | 1 | 209 k | 532 k | ×2.6 | – |
| strings of 1 KB | 4 | 70.7 k | 179 k | ×2.5 | – |
| strings of 1 KB | 8 | 67.0 k | 171 k | ×2.6 | – |
| lists of 1000 integers | 1 | 71.1 k | 94.2 k | +32% | – |
| lists of 1000 integers | 4 | 53.6 k | 150 k | ×2.8 | – |
| lists of 1000 integers | 8 | 45.7 k | 142 k | ×3.1 | – |
| arrays of 100 rows | 1 | 20.4 k | 22.6 k | +10% | – |
| arrays of 100 rows | 4 | 41.6 k | 63.4 k | +53% | – |
| arrays of 100 rows | 8 | 35.6 k | 74.1 k | ×2.1 | – |
| mix | 1 | 239 k | 550 k | ×2.3 | ×2.3 |
| mix | 4 | 87.3 k | 211 k | ×2.4 | ×2.2 |
| mix | 8 | 82.1 k | 191 k | ×2.3 | ×2.2 |

</details>
<details>
<summary>Sets only (sets a second)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 263 k | 593 k | ×2.3 | ×2.3 |
| integers | 4 | 90.2 k | 233 k | ×2.6 | ×2.4 |
| integers | 8 | 87.0 k | 223 k | ×2.6 | ×2.2 |
| rows of 14 strings | 1 | 126 k | 327 k | ×2.6 | ×2.6 |
| rows of 14 strings | 4 | 44.2 k | 147 k | ×3.3 | ×3.4 |
| rows of 14 strings | 8 | 42.7 k | 144 k | ×3.4 | ×3.4 |
| strings of 1 KB | 1 | 177 k | 512 k | ×2.9 | – |
| strings of 1 KB | 4 | 58.7 k | 182 k | ×3.1 | – |
| strings of 1 KB | 8 | 55.1 k | 167 k | ×3.0 | – |
| lists of 1000 integers | 1 | 66.6 k | 88.0 k | +32% | – |
| lists of 1000 integers | 4 | 39.1 k | 111 k | ×2.8 | – |
| lists of 1000 integers | 8 | 37.7 k | 107 k | ×2.8 | – |
| arrays of 100 rows | 1 | 19.4 k | 22.0 k | +13% | – |
| arrays of 100 rows | 4 | 38.7 k | 77.0 k | +99% | – |
| arrays of 100 rows | 8 | 28.8 k | 100 k | ×3.5 | – |
| mix | 1 | 206 k | 524 k | ×2.5 | ×2.5 |
| mix | 4 | 70.4 k | 192 k | ×2.7 | ×2.6 |
| mix | 8 | 66.3 k | 178 k | ×2.7 | ×2.6 |

</details>
<details>
<summary>One reader beside writers: the reader's gets (gets a second of the reader)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 70.4 k | 62.5 k | −11% | −20% |
| integers | 8 | 211 k | 27.3 k | −87% | −89% |
| rows of 14 strings | 4 | 112 k | 38.8 k | −65% | −59% |
| rows of 14 strings | 8 | 321 k | 18.9 k | −94% | −94% |
| strings of 1 KB | 4 | 72.7 k | 44.2 k | −39% | – |
| strings of 1 KB | 8 | 339 k | 19.9 k | −94% | – |
| lists of 1000 integers | 4 | 36.3 k | 35.6 k | −2% | – |
| lists of 1000 integers | 8 | 56.3 k | 22.0 k | −61% | – |
| arrays of 100 rows | 4 | 20.6 k | 19.3 k | −6% | – |
| arrays of 100 rows | 8 | 18.4 k | 12.8 k | −30% | – |
| mix | 4 | 72.6 k | 50.0 k | −31% | −37% |
| mix | 8 | 305 k | 22.0 k | −93% | −93% |

The reader's get, median / 99th percentile:

| Workload | Processes | 1.1.0 | 1.1.1 | PHP 8.4: 1.1.0 | PHP 8.4: 1.1.1 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 15 µs / 24 µs | 15 µs / 32 µs | 15 µs / 24 µs | 17 µs / 32 µs |
| integers | 8 | 3 µs / 32 µs | 33 µs / 71 µs | 2.7 µs / 28 µs | 40 µs / 80 µs |
| rows of 14 strings | 4 | 6 µs / 25 µs | 25 µs / 42 µs | 10 µs / 25 µs | 25 µs / 45 µs |
| rows of 14 strings | 8 | 2 µs / 12 µs | 50 µs / 99 µs | 1.9 µs / 13 µs | 52 µs / 106 µs |
| strings of 1 KB | 4 | 17 µs / 25 µs | 22 µs / 38 µs | – | – |
| strings of 1 KB | 8 | 2 µs / 12 µs | 47 µs / 87 µs | – | – |
| lists of 1000 integers | 4 | 27 µs / 41 µs | 27 µs / 46 µs | – | – |
| lists of 1000 integers | 8 | 17 µs / 33 µs | 46 µs / 77 µs | – | – |
| arrays of 100 rows | 4 | 42 µs / 98 µs | 44 µs / 116 µs | – | – |
| arrays of 100 rows | 8 | 51 µs / 122 µs | 74 µs / 131 µs | – | – |
| mix | 4 | 16 µs / 24 µs | 19 µs / 34 µs | 16 µs / 27 µs | 21 µs / 39 µs |
| mix | 8 | 1 µs / 14 µs | 44 µs / 78 µs | 1.4 µs / 14 µs | 45 µs / 89 µs |

</details>
<details>
<summary>One reader beside writers: the writers' sets (sets a second of the 3 or 7 writers together)</summary>

| Workload | Processes | 1.1.0 | 1.1.1 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 101 k | 187 k | +86% | +67% |
| integers | 8 | 86.4 k | 191 k | ×2.2 | +94% |
| rows of 14 strings | 4 | 72.1 k | 116 k | +61% | +66% |
| rows of 14 strings | 8 | 38.8 k | 133 k | ×3.4 | ×3.2 |
| strings of 1 KB | 4 | 79.5 k | 133 k | +67% | – |
| strings of 1 KB | 8 | 51.1 k | 139 k | ×2.7 | – |
| lists of 1000 integers | 4 | 52.0 k | 106 k | ×2.0 | – |
| lists of 1000 integers | 8 | 37.0 k | 143 k | ×3.9 | – |
| arrays of 100 rows | 4 | 23.5 k | 39.6 k | +69% | – |
| arrays of 100 rows | 8 | 30.0 k | 77.7 k | ×2.6 | – |
| mix | 4 | 87.4 k | 150 k | +72% | +65% |
| mix | 8 | 64.1 k | 154 k | ×2.4 | ×2.3 |

</details>

### ucache.lockedcopy=0 in 1.1.1

- **What it changes:** `ucache.lockedcopy` is the largest entry, in bytes, whose value a set copies in under the cache
  lock (2048 by default; see [Tuning](README.md#tuning) in the README). With 0, every value is copied before the cache
  lock is taken, as in 1.1.0, and the set holds the cache lock only for its insert.
- **Reads and read-heavy loads:** no change beyond the noise.
- **Writes:** copying under the lock is faster, so 0 made 10 to 14% fewer calls with 50% sets in one process (5 to 7%
  more with 4 or 8), and 6 to 19% fewer with 80% sets or sets only (up to 27% fewer). Even with 0, 1.1.1 made 1.7 times
  as many sets of integers as 1.1.0 with one process: most of 1.1.1's gain on writes comes from the one block per entry.
- **A reader beside writers:** 2.2 times as many gets on PHP 7.2 and 3.3 times on 8.4; beside 7 writers of small values,
  6.8 to 9.8 times as many, its median get taking 2 to 8 µs instead of 33 to 52 (1.1.0: 1 to 3 µs).
- **Values larger than 2048 bytes** (lists of 1000 integers, arrays of 100 rows): no change, as they are copied before
  the lock either way.
- **When to use it:** keep the default, unless processes that only read run beside processes that write small values
  back to back, and the reads matter more than the writes. In a web server, requests that do other work between their
  calls rarely find the cache lock taken, and the setting changes little either way.

The changes from 2048 to 0:

| Load | 1 process | 4 and 8 processes | PHP 8.4: 1 process | PHP 8.4: 4 and 8 processes |
|---|--:|--:|--:|--:|
| Gets only | +1% | −1% | 0% | +4% |
| 1% sets | −1% | 0% | +1% | −3% |
| 5% sets | −1% | +3% | −3% | +1% |
| One writer beside readers: the readers' gets | – | −1% | – | +3% |
| One writer beside readers: the writer's sets | – | −1% | – | +3% |
| 50% sets | −10% | +5% | −14% | +7% |
| 80% sets | −12% | −6% | −18% | −6% |
| Sets only | −14% | −14% | −19% | −18% |
| One reader beside writers: the reader's gets | – | ×2.2 | – | ×3.3 |
| One reader beside writers: the writers' sets | – | +3% | – | +8% |

<details>
<summary>Gets only (gets a second)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 1.01 M | 1.02 M | +1% | +3% |
| integers | 4 | 312 k | 317 k | +2% | +1% |
| integers | 8 | 275 k | 276 k | 0% | +2% |
| rows of 14 strings | 1 | 655 k | 652 k | 0% | 0% |
| rows of 14 strings | 4 | 251 k | 246 k | −2% | +6% |
| rows of 14 strings | 8 | 192 k | 183 k | −5% | +8% |
| strings of 1 KB | 1 | 852 k | 884 k | +4% | – |
| strings of 1 KB | 4 | 263 k | 253 k | −4% | – |
| strings of 1 KB | 8 | 219 k | 208 k | −5% | – |
| lists of 1000 integers | 1 | 114 k | 112 k | −1% | – |
| lists of 1000 integers | 4 | 74.7 k | 74.4 k | 0% | – |
| lists of 1000 integers | 8 | 70.8 k | 73.6 k | +4% | – |
| arrays of 100 rows | 1 | 25.5 k | 24.7 k | −3% | – |
| arrays of 100 rows | 4 | 27.3 k | 26.8 k | −2% | – |
| arrays of 100 rows | 8 | 26.0 k | 25.2 k | −3% | – |
| mix | 1 | 946 k | 1.01 M | +7% | −3% |
| mix | 4 | 280 k | 266 k | −5% | +1% |
| mix | 8 | 254 k | 267 k | +5% | +3% |

</details>
<details>
<summary>1% sets (calls a second)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 970 k | 977 k | +1% | 0% |
| integers | 4 | 305 k | 301 k | −1% | −1% |
| integers | 8 | 273 k | 291 k | +6% | −5% |
| rows of 14 strings | 1 | 654 k | 634 k | −3% | 0% |
| rows of 14 strings | 4 | 216 k | 208 k | −4% | −6% |
| rows of 14 strings | 8 | 212 k | 210 k | −1% | −1% |
| strings of 1 KB | 1 | 832 k | 812 k | −2% | – |
| strings of 1 KB | 4 | 259 k | 249 k | −4% | – |
| strings of 1 KB | 8 | 203 k | 211 k | +4% | – |
| lists of 1000 integers | 1 | 110 k | 116 k | +5% | – |
| lists of 1000 integers | 4 | 73.6 k | 73.6 k | 0% | – |
| lists of 1000 integers | 8 | 72.6 k | 71.0 k | −2% | – |
| arrays of 100 rows | 1 | 24.4 k | 25.2 k | +3% | – |
| arrays of 100 rows | 4 | 26.9 k | 27.1 k | +1% | – |
| arrays of 100 rows | 8 | 24.7 k | 25.0 k | +1% | – |
| mix | 1 | 971 k | 897 k | −8% | +2% |
| mix | 4 | 277 k | 257 k | −7% | −4% |
| mix | 8 | 269 k | 283 k | +5% | +1% |

</details>
<details>
<summary>5% sets (calls a second)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 990 k | 947 k | −4% | −4% |
| integers | 4 | 295 k | 307 k | +4% | +2% |
| integers | 8 | 268 k | 282 k | +5% | −1% |
| rows of 14 strings | 1 | 593 k | 606 k | +2% | −1% |
| rows of 14 strings | 4 | 216 k | 228 k | +5% | +3% |
| rows of 14 strings | 8 | 199 k | 192 k | −3% | +7% |
| strings of 1 KB | 1 | 801 k | 778 k | −3% | – |
| strings of 1 KB | 4 | 206 k | 232 k | +13% | – |
| strings of 1 KB | 8 | 179 k | 218 k | +22% | – |
| lists of 1000 integers | 1 | 110 k | 111 k | +1% | – |
| lists of 1000 integers | 4 | 78.3 k | 78.5 k | 0% | – |
| lists of 1000 integers | 8 | 73.8 k | 74.1 k | 0% | – |
| arrays of 100 rows | 1 | 25.4 k | 24.8 k | −3% | – |
| arrays of 100 rows | 4 | 26.9 k | 26.0 k | −3% | – |
| arrays of 100 rows | 8 | 26.0 k | 25.9 k | 0% | – |
| mix | 1 | 897 k | 894 k | 0% | −3% |
| mix | 4 | 303 k | 300 k | −1% | +1% |
| mix | 8 | 216 k | 218 k | +1% | −2% |

</details>
<details>
<summary>One writer beside readers: the readers' gets (gets a second of the 3 or 7 readers together)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 193 k | 190 k | −2% | −3% |
| integers | 8 | 239 k | 234 k | −2% | −1% |
| rows of 14 strings | 4 | 134 k | 161 k | +20% | +14% |
| rows of 14 strings | 8 | 156 k | 141 k | −10% | +8% |
| strings of 1 KB | 4 | 172 k | 156 k | −9% | – |
| strings of 1 KB | 8 | 175 k | 158 k | −10% | – |
| lists of 1000 integers | 4 | 67.2 k | 66.5 k | −1% | – |
| lists of 1000 integers | 8 | 67.2 k | 67.4 k | 0% | – |
| arrays of 100 rows | 4 | 23.3 k | 21.8 k | −6% | – |
| arrays of 100 rows | 8 | 23.0 k | 23.1 k | +1% | – |
| mix | 4 | 159 k | 178 k | +12% | −1% |
| mix | 8 | 223 k | 223 k | 0% | 0% |

</details>
<details>
<summary>One writer beside readers: the writer's sets (sets a second of the writer)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 64.3 k | 63.1 k | −2% | −3% |
| integers | 8 | 34.2 k | 33.4 k | −2% | −1% |
| rows of 14 strings | 4 | 44.6 k | 53.1 k | +19% | +14% |
| rows of 14 strings | 8 | 22.3 k | 20.0 k | −10% | +8% |
| strings of 1 KB | 4 | 57.2 k | 51.7 k | −10% | – |
| strings of 1 KB | 8 | 24.9 k | 22.4 k | −10% | – |
| lists of 1000 integers | 4 | 22.2 k | 21.9 k | −1% | – |
| lists of 1000 integers | 8 | 9.48 k | 9.50 k | 0% | – |
| arrays of 100 rows | 4 | 6.75 k | 6.57 k | −3% | – |
| arrays of 100 rows | 8 | 3.03 k | 3.11 k | +3% | – |
| mix | 4 | 52.8 k | 58.7 k | +11% | −1% |
| mix | 8 | 31.7 k | 31.8 k | 0% | 0% |

</details>
<details>
<summary>50% sets (calls a second)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 773 k | 627 k | −19% | −19% |
| integers | 4 | 280 k | 275 k | −2% | −1% |
| integers | 8 | 263 k | 254 k | −3% | −2% |
| rows of 14 strings | 1 | 423 k | 367 k | −13% | −10% |
| rows of 14 strings | 4 | 163 k | 204 k | +25% | +17% |
| rows of 14 strings | 8 | 155 k | 197 k | +27% | +19% |
| strings of 1 KB | 1 | 602 k | 521 k | −13% | – |
| strings of 1 KB | 4 | 222 k | 225 k | +1% | – |
| strings of 1 KB | 8 | 183 k | 204 k | +11% | – |
| lists of 1000 integers | 1 | 100 k | 99.0 k | −1% | – |
| lists of 1000 integers | 4 | 110 k | 109 k | −1% | – |
| lists of 1000 integers | 8 | 105 k | 104 k | −1% | – |
| arrays of 100 rows | 1 | 23.5 k | 23.3 k | −1% | – |
| arrays of 100 rows | 4 | 44.4 k | 44.3 k | 0% | – |
| arrays of 100 rows | 8 | 43.4 k | 43.0 k | −1% | – |
| mix | 1 | 655 k | 566 k | −14% | −13% |
| mix | 4 | 245 k | 246 k | 0% | +2% |
| mix | 8 | 219 k | 229 k | +4% | +10% |

</details>
<details>
<summary>80% sets (calls a second)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 627 k | 507 k | −19% | −22% |
| integers | 4 | 240 k | 202 k | −16% | −9% |
| integers | 8 | 228 k | 195 k | −14% | −12% |
| rows of 14 strings | 1 | 346 k | 301 k | −13% | −13% |
| rows of 14 strings | 4 | 158 k | 148 k | −7% | −9% |
| rows of 14 strings | 8 | 148 k | 138 k | −7% | −4% |
| strings of 1 KB | 1 | 532 k | 427 k | −20% | – |
| strings of 1 KB | 4 | 179 k | 178 k | −1% | – |
| strings of 1 KB | 8 | 171 k | 157 k | −8% | – |
| lists of 1000 integers | 1 | 94.2 k | 92.4 k | −2% | – |
| lists of 1000 integers | 4 | 150 k | 148 k | −1% | – |
| lists of 1000 integers | 8 | 142 k | 142 k | 0% | – |
| arrays of 100 rows | 1 | 22.6 k | 23.2 k | +3% | – |
| arrays of 100 rows | 4 | 63.4 k | 64.1 k | +1% | – |
| arrays of 100 rows | 8 | 74.1 k | 74.1 k | 0% | – |
| mix | 1 | 550 k | 452 k | −18% | −19% |
| mix | 4 | 211 k | 193 k | −9% | +1% |
| mix | 8 | 191 k | 176 k | −8% | −2% |

</details>
<details>
<summary>Sets only (sets a second)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 593 k | 456 k | −23% | −24% |
| integers | 4 | 233 k | 170 k | −27% | −16% |
| integers | 8 | 223 k | 167 k | −25% | −17% |
| rows of 14 strings | 1 | 327 k | 276 k | −16% | −15% |
| rows of 14 strings | 4 | 147 k | 110 k | −25% | −25% |
| rows of 14 strings | 8 | 144 k | 108 k | −25% | −25% |
| strings of 1 KB | 1 | 512 k | 396 k | −23% | – |
| strings of 1 KB | 4 | 182 k | 152 k | −16% | – |
| strings of 1 KB | 8 | 167 k | 140 k | −16% | – |
| lists of 1000 integers | 1 | 88.0 k | 89.5 k | +2% | – |
| lists of 1000 integers | 4 | 111 k | 112 k | 0% | – |
| lists of 1000 integers | 8 | 107 k | 108 k | +1% | – |
| arrays of 100 rows | 1 | 22.0 k | 22.3 k | +2% | – |
| arrays of 100 rows | 4 | 77.0 k | 77.9 k | +1% | – |
| arrays of 100 rows | 8 | 100 k | 104 k | +4% | – |
| mix | 1 | 524 k | 419 k | −20% | −19% |
| mix | 4 | 192 k | 164 k | −15% | −8% |
| mix | 8 | 178 k | 152 k | −14% | −16% |

</details>
<details>
<summary>One reader beside writers: the reader's gets (gets a second of the reader)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 62.5 k | 55.9 k | −11% | +1% |
| integers | 8 | 27.3 k | 230 k | ×8.4 | ×9.5 |
| rows of 14 strings | 4 | 38.8 k | 68.3 k | +76% | +45% |
| rows of 14 strings | 8 | 18.9 k | 129 k | ×6.8 | ×7.5 |
| strings of 1 KB | 4 | 44.2 k | 55.0 k | +25% | – |
| strings of 1 KB | 8 | 19.9 k | 195 k | ×9.8 | – |
| lists of 1000 integers | 4 | 35.6 k | 36.8 k | +3% | – |
| lists of 1000 integers | 8 | 22.0 k | 21.7 k | −1% | – |
| arrays of 100 rows | 4 | 19.3 k | 19.8 k | +2% | – |
| arrays of 100 rows | 8 | 12.8 k | 12.9 k | +1% | – |
| mix | 4 | 50.0 k | 64.5 k | +29% | +39% |
| mix | 8 | 22.0 k | 205 k | ×9.3 | ×9.3 |

The reader's get, median / 99th percentile:

| Workload | Processes | 2048 | 0 | PHP 8.4: 2048 | PHP 8.4: 0 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 15 µs / 32 µs | 18 µs / 30 µs | 17 µs / 32 µs | 18 µs / 28 µs |
| integers | 8 | 33 µs / 71 µs | 2 µs / 20 µs | 40 µs / 80 µs | 2.4 µs / 28 µs |
| rows of 14 strings | 4 | 25 µs / 42 µs | 15 µs / 26 µs | 25 µs / 45 µs | 18 µs / 28 µs |
| rows of 14 strings | 8 | 50 µs / 99 µs | 8 µs / 27 µs | 52 µs / 106 µs | 7.4 µs / 22 µs |
| strings of 1 KB | 4 | 22 µs / 38 µs | 19 µs / 30 µs | – | – |
| strings of 1 KB | 8 | 47 µs / 87 µs | 4 µs / 20 µs | – | – |
| lists of 1000 integers | 4 | 27 µs / 46 µs | 26 µs / 44 µs | – | – |
| lists of 1000 integers | 8 | 46 µs / 77 µs | 46 µs / 73 µs | – | – |
| arrays of 100 rows | 4 | 44 µs / 116 µs | 44 µs / 111 µs | – | – |
| arrays of 100 rows | 8 | 74 µs / 131 µs | 72 µs / 138 µs | – | – |
| mix | 4 | 19 µs / 34 µs | 17 µs / 27 µs | 21 µs / 39 µs | 18 µs / 30 µs |
| mix | 8 | 44 µs / 78 µs | 4 µs / 19 µs | 45 µs / 89 µs | 4.2 µs / 19 µs |

</details>
<details>
<summary>One reader beside writers: the writers' sets (sets a second of the 3 or 7 writers together)</summary>

| Workload | Processes | 2048 | 0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 187 k | 159 k | −15% | −4% |
| integers | 8 | 191 k | 165 k | −14% | −2% |
| rows of 14 strings | 4 | 116 k | 167 k | +43% | +31% |
| rows of 14 strings | 8 | 133 k | 127 k | −4% | +5% |
| strings of 1 KB | 4 | 133 k | 157 k | +19% | – |
| strings of 1 KB | 8 | 139 k | 144 k | +3% | – |
| lists of 1000 integers | 4 | 106 k | 109 k | +3% | – |
| lists of 1000 integers | 8 | 143 k | 141 k | −1% | – |
| arrays of 100 rows | 4 | 39.6 k | 39.9 k | +1% | – |
| arrays of 100 rows | 8 | 77.7 k | 81.7 k | +5% | – |
| mix | 4 | 150 k | 166 k | +11% | +19% |
| mix | 8 | 154 k | 157 k | +1% | +4% |

</details>

## 1.0.1 → 1.1.0

- **Reads:** one hash bucket per 512 bytes of the cache instead of one per 32 to 64 KB: 262,139 buckets instead of 3,203
  in these caches of 128 MB, so that with 100,000 keys a bucket holds 31 entries on average instead of less than one.
  Gets were 53 to 82% faster with the 100,000 integers and the mix, 27 to 56% with the 20,000 rows and the 50,000
  strings, and about as fast (−9% to +10%) with the 2,000 lists and the 200 arrays of 100 rows, whose chains were
  already short. On PHP 8.4, gets were 60 to 63% faster on average.
- **Writes:** arrays take fewer blocks of shared memory (4 instead of 12 for a row, 4 instead of 148 for an array of 100
  rows) and less memory (a row 1,948 bytes instead of 6,396). One process made 4.4 times as many sets (geometric mean),
  4 and 8 processes 7 times as many. With arrays of 100 rows, 4 or 8 processes made 53 to 77 times as many sets: in
  1.0.1, each of the 148 blocks of such an array takes the allocator's lock, and processes that store them at once queue
  on it (4,380 sets a second with one process, 475 with 4).
- **A reader beside writers:** a process that reads back to back beside 3 or 7 processes that set back to back made 52%
  fewer gets (53% on PHP 8.4), its median get taking 2 to 58 µs instead of 1 to 47, while the writers made 8 times as
  many sets (5.7 times on PHP 8.4), each of which takes the cache lock.
- **Memory:** an entry of the mix took 464 bytes on average instead of 765.

The changes from 1.0.1 to 1.1.0:

| Load | 1 process | 4 and 8 processes | PHP 8.4: 1 process | PHP 8.4: 4 and 8 processes |
|---|--:|--:|--:|--:|
| Gets only | +32% | +33% | +60% | +63% |
| 1% sets | +39% | +30% | +67% | +59% |
| 5% sets | +82% | +68% | ×2.2 | +67% |
| One writer beside readers: the readers' gets | – | +21% | – | +43% |
| One writer beside readers: the writer's sets | – | +64% | – | +82% |
| 50% sets | ×3.7 | ×6.6 | ×4.6 | ×5.2 |
| 80% sets | ×4.2 | ×7.0 | ×5.1 | ×5.3 |
| Sets only | ×4.4 | ×7.0 | ×5.5 | ×5.1 |
| One reader beside writers: the reader's gets | – | **−52%** | – | **−53%** |
| One reader beside writers: the writers' sets | – | ×8.0 | – | ×5.7 |

<details>
<summary>Gets only (gets a second)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 701 k | 1.11 M | +58% | +72% |
| integers | 4 | 215 k | 328 k | +53% | +89% |
| integers | 8 | 198 k | 314 k | +59% | +90% |
| rows of 14 strings | 1 | 466 k | 613 k | +32% | +31% |
| rows of 14 strings | 4 | 153 k | 195 k | +27% | +25% |
| rows of 14 strings | 8 | 146 k | 217 k | +48% | +25% |
| strings of 1 KB | 1 | 635 k | 871 k | +37% | – |
| strings of 1 KB | 4 | 212 k | 290 k | +37% | – |
| strings of 1 KB | 8 | 155 k | 242 k | +56% | – |
| lists of 1000 integers | 1 | 119 k | 119 k | 0% | – |
| lists of 1000 integers | 4 | 79.3 k | 72.1 k | −9% | – |
| lists of 1000 integers | 8 | 71.1 k | 70.2 k | −1% | – |
| arrays of 100 rows | 1 | 22.3 k | 23.9 k | +7% | – |
| arrays of 100 rows | 4 | 23.2 k | 25.5 k | +10% | – |
| arrays of 100 rows | 8 | 22.3 k | 24.1 k | +8% | – |
| mix | 1 | 495 k | 866 k | +75% | +83% |
| mix | 4 | 148 k | 270 k | +82% | +83% |
| mix | 8 | 141 k | 233 k | +65% | +80% |

</details>
<details>
<summary>1% sets (calls a second)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 635 k | 1.01 M | +59% | +85% |
| integers | 4 | 186 k | 310 k | +66% | +90% |
| integers | 8 | 187 k | 322 k | +72% | +78% |
| rows of 14 strings | 1 | 408 k | 545 k | +34% | +32% |
| rows of 14 strings | 4 | 148 k | 173 k | +17% | +28% |
| rows of 14 strings | 8 | 133 k | 160 k | +20% | +29% |
| strings of 1 KB | 1 | 519 k | 799 k | +54% | – |
| strings of 1 KB | 4 | 224 k | 288 k | +29% | – |
| strings of 1 KB | 8 | 188 k | 245 k | +30% | – |
| lists of 1000 integers | 1 | 110 k | 113 k | +2% | – |
| lists of 1000 integers | 4 | 73.3 k | 68.4 k | −7% | – |
| lists of 1000 integers | 8 | 70.4 k | 70.1 k | 0% | – |
| arrays of 100 rows | 1 | 20.2 k | 23.5 k | +17% | – |
| arrays of 100 rows | 4 | 21.8 k | 24.0 k | +10% | – |
| arrays of 100 rows | 8 | 19.5 k | 23.4 k | +20% | – |
| mix | 1 | 439 k | 822 k | +87% | +90% |
| mix | 4 | 138 k | 244 k | +76% | +76% |
| mix | 8 | 131 k | 217 k | +65% | +65% |

</details>
<details>
<summary>5% sets (calls a second)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 459 k | 910 k | +98% | ×2.3 |
| integers | 4 | 152 k | 308 k | ×2.0 | +85% |
| integers | 8 | 139 k | 285 k | ×2.1 | +72% |
| rows of 14 strings | 1 | 268 k | 448 k | +67% | +65% |
| rows of 14 strings | 4 | 94.0 k | 152 k | +62% | +53% |
| rows of 14 strings | 8 | 86.2 k | 162 k | +88% | +63% |
| strings of 1 KB | 1 | 236 k | 694 k | ×2.9 | – |
| strings of 1 KB | 4 | 164 k | 254 k | +55% | – |
| strings of 1 KB | 8 | 156 k | 204 k | +31% | – |
| lists of 1000 integers | 1 | 101 k | 105 k | +4% | – |
| lists of 1000 integers | 4 | 69.6 k | 70.5 k | +1% | – |
| lists of 1000 integers | 8 | 66.0 k | 67.9 k | +3% | – |
| arrays of 100 rows | 1 | 16.2 k | 22.9 k | +41% | – |
| arrays of 100 rows | 4 | 9.12 k | 24.2 k | ×2.7 | – |
| arrays of 100 rows | 8 | 8.25 k | 22.5 k | ×2.7 | – |
| mix | 1 | 279 k | 709 k | ×2.5 | ×2.7 |
| mix | 4 | 136 k | 206 k | +52% | +66% |
| mix | 8 | 119 k | 200 k | +67% | +64% |

</details>
<details>
<summary>One writer beside readers: the readers' gets (gets a second of the 3 or 7 readers together)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 151 k | 220 k | +46% | +63% |
| integers | 8 | 155 k | 230 k | +49% | +76% |
| rows of 14 strings | 4 | 137 k | 156 k | +14% | −1% |
| rows of 14 strings | 8 | 135 k | 172 k | +27% | +24% |
| strings of 1 KB | 4 | 176 k | 184 k | +4% | – |
| strings of 1 KB | 8 | 164 k | 198 k | +21% | – |
| lists of 1000 integers | 4 | 62.3 k | 62.7 k | +1% | – |
| lists of 1000 integers | 8 | 65.0 k | 62.6 k | −4% | – |
| arrays of 100 rows | 4 | 17.6 k | 19.9 k | +13% | – |
| arrays of 100 rows | 8 | 19.5 k | 21.1 k | +8% | – |
| mix | 4 | 120 k | 171 k | +42% | +44% |
| mix | 8 | 127 k | 192 k | +51% | +70% |

</details>
<details>
<summary>One writer beside readers: the writer's sets (sets a second of the writer)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 18.8 k | 36.6 k | +95% | ×2.0 |
| integers | 8 | 10.1 k | 16.4 k | +63% | +87% |
| rows of 14 strings | 4 | 14.1 k | 25.8 k | +84% | +51% |
| rows of 14 strings | 8 | 8.03 k | 12.2 k | +52% | +42% |
| strings of 1 KB | 4 | 11.1 k | 30.5 k | ×2.8 | – |
| strings of 1 KB | 8 | 7.80 k | 14.1 k | +81% | – |
| lists of 1000 integers | 4 | 10.1 k | 10.3 k | +2% | – |
| lists of 1000 integers | 8 | 4.53 k | 4.43 k | −2% | – |
| arrays of 100 rows | 4 | 1.93 k | 3.13 k | +62% | – |
| arrays of 100 rows | 8 | 1.11 k | 1.45 k | +31% | – |
| mix | 4 | 12.8 k | 28.3 k | ×2.2 | ×2.2 |
| mix | 8 | 7.66 k | 13.7 k | +79% | ×2.0 |

</details>
<details>
<summary>50% sets (calls a second)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 88.0 k | 396 k | ×4.5 | ×4.5 |
| integers | 4 | 48.0 k | 160 k | ×3.3 | ×3.8 |
| integers | 8 | 44.6 k | 163 k | ×3.6 | ×3.6 |
| rows of 14 strings | 1 | 51.7 k | 193 k | ×3.7 | ×3.6 |
| rows of 14 strings | 4 | 9.47 k | 93.2 k | ×9.8 | ×11 |
| rows of 14 strings | 8 | 8.30 k | 79.8 k | ×9.6 | ×8.4 |
| strings of 1 KB | 1 | 30.9 k | 257 k | ×8.3 | – |
| strings of 1 KB | 4 | 19.8 k | 117 k | ×5.9 | – |
| strings of 1 KB | 8 | 19.8 k | 94.5 k | ×4.8 | – |
| lists of 1000 integers | 1 | 68.4 k | 78.4 k | +15% | – |
| lists of 1000 integers | 4 | 23.0 k | 68.2 k | ×3.0 | – |
| lists of 1000 integers | 8 | 20.7 k | 66.0 k | ×3.2 | – |
| arrays of 100 rows | 1 | 7.01 k | 19.4 k | ×2.8 | – |
| arrays of 100 rows | 4 | 851 | 31.2 k | ×37 | – |
| arrays of 100 rows | 8 | 827 | 31.7 k | ×38 | – |
| mix | 1 | 57.1 k | 338 k | ×5.9 | ×5.9 |
| mix | 4 | 30.7 k | 126 k | ×4.1 | ×4.4 |
| mix | 8 | 29.5 k | 116 k | ×3.9 | ×3.7 |

</details>
<details>
<summary>80% sets (calls a second)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 57.4 k | 266 k | ×4.6 | ×4.8 |
| integers | 4 | 29.3 k | 97.3 k | ×3.3 | ×4.1 |
| integers | 8 | 27.6 k | 95.5 k | ×3.5 | ×3.8 |
| rows of 14 strings | 1 | 35.0 k | 134 k | ×3.8 | ×4.3 |
| rows of 14 strings | 4 | 5.99 k | 51.0 k | ×8.5 | ×8.7 |
| rows of 14 strings | 8 | 5.05 k | 42.8 k | ×8.5 | ×8.9 |
| strings of 1 KB | 1 | 20.6 k | 209 k | ×10 | – |
| strings of 1 KB | 4 | 13.2 k | 71.5 k | ×5.4 | – |
| strings of 1 KB | 8 | 12.4 k | 65.7 k | ×5.3 | – |
| lists of 1000 integers | 1 | 58.4 k | 68.8 k | +18% | – |
| lists of 1000 integers | 4 | 14.9 k | 46.5 k | ×3.1 | – |
| lists of 1000 integers | 8 | 13.8 k | 39.9 k | ×2.9 | – |
| arrays of 100 rows | 1 | 4.77 k | 18.7 k | ×3.9 | – |
| arrays of 100 rows | 4 | 533 | 34.8 k | ×65 | – |
| arrays of 100 rows | 8 | 535 | 31.3 k | ×59 | – |
| mix | 1 | 34.4 k | 222 k | ×6.5 | ×6.5 |
| mix | 4 | 19.0 k | 79.8 k | ×4.2 | ×4.4 |
| mix | 8 | 17.2 k | 73.6 k | ×4.3 | ×4.0 |

</details>
<details>
<summary>Sets only (sets a second)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 1 | 47.1 k | 239 k | ×5.1 | ×5.2 |
| integers | 4 | 24.5 k | 80.6 k | ×3.3 | ×3.8 |
| integers | 8 | 24.6 k | 84.5 k | ×3.4 | ×3.7 |
| rows of 14 strings | 1 | 29.2 k | 127 k | ×4.4 | ×4.5 |
| rows of 14 strings | 4 | 4.86 k | 45.1 k | ×9.3 | ×8.7 |
| rows of 14 strings | 8 | 4.52 k | 42.4 k | ×9.4 | ×8.7 |
| strings of 1 KB | 1 | 16.6 k | 180 k | ×11 | – |
| strings of 1 KB | 4 | 10.4 k | 58.4 k | ×5.6 | – |
| strings of 1 KB | 8 | 10.1 k | 51.7 k | ×5.1 | – |
| lists of 1000 integers | 1 | 51.6 k | 64.2 k | +24% | – |
| lists of 1000 integers | 4 | 11.9 k | 33.9 k | ×2.9 | – |
| lists of 1000 integers | 8 | 11.3 k | 31.9 k | ×2.8 | – |
| arrays of 100 rows | 1 | 4.38 k | 17.4 k | ×4.0 | – |
| arrays of 100 rows | 4 | 475 | 36.5 k | ×77 | – |
| arrays of 100 rows | 8 | 529 | 28.1 k | ×53 | – |
| mix | 1 | 29.7 k | 194 k | ×6.5 | ×7.0 |
| mix | 4 | 16.1 k | 62.5 k | ×3.9 | ×4.3 |
| mix | 8 | 14.2 k | 58.3 k | ×4.1 | ×4.1 |

</details>
<details>
<summary>One reader beside writers: the reader's gets (gets a second of the reader)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 287 k | 62.0 k | −78% | −77% |
| integers | 8 | 269 k | 150 k | −44% | −32% |
| rows of 14 strings | 4 | 449 k | 116 k | −74% | −71% |
| rows of 14 strings | 8 | 401 k | 317 k | −21% | −24% |
| strings of 1 KB | 4 | 534 k | 76.0 k | −86% | – |
| strings of 1 KB | 8 | 498 k | 328 k | −34% | – |
| lists of 1000 integers | 4 | 84.4 k | 34.2 k | −59% | – |
| lists of 1000 integers | 8 | 83.3 k | 52.3 k | −37% | – |
| arrays of 100 rows | 4 | 18.8 k | 19.2 k | +2% | – |
| arrays of 100 rows | 8 | 18.3 k | 15.0 k | −18% | – |
| mix | 4 | 251 k | 70.2 k | −72% | −71% |
| mix | 8 | 253 k | 291 k | +15% | +2% |

The reader's get, median / 99th percentile:

| Workload | Processes | 1.0.1 | 1.1.0 | PHP 8.4: 1.0.1 | PHP 8.4: 1.1.0 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 2 µs / 21 µs | 17 µs / 29 µs | 2.3 µs / 24 µs | 18 µs / 32 µs |
| integers | 8 | 2 µs / 19 µs | 5 µs / 42 µs | 2.5 µs / 21 µs | 5.2 µs / 39 µs |
| rows of 14 strings | 4 | 2 µs / 11 µs | 5 µs / 24 µs | 2.6 µs / 15 µs | 9.6 µs / 28 µs |
| rows of 14 strings | 8 | 2 µs / 13 µs | 2 µs / 12 µs | 2.5 µs / 15 µs | 2.6 µs / 15 µs |
| strings of 1 KB | 4 | 1 µs / 12 µs | 17 µs / 29 µs | – | – |
| strings of 1 KB | 8 | 1 µs / 13 µs | 2 µs / 12 µs | – | – |
| lists of 1000 integers | 4 | 10 µs / 27 µs | 29 µs / 48 µs | – | – |
| lists of 1000 integers | 8 | 11 µs / 27 µs | 19 µs / 38 µs | – | – |
| arrays of 100 rows | 4 | 47 µs / 119 µs | 44 µs / 131 µs | – | – |
| arrays of 100 rows | 8 | 46 µs / 111 µs | 58 µs / 183 µs | – | – |
| mix | 4 | 3 µs / 21 µs | 16 µs / 27 µs | 2.9 µs / 23 µs | 18 µs / 29 µs |
| mix | 8 | 3 µs / 21 µs | 2 µs / 15 µs | 2.8 µs / 23 µs | 1.8 µs / 17 µs |

</details>
<details>
<summary>One reader beside writers: the writers' sets (sets a second of the 3 or 7 writers together)</summary>

| Workload | Processes | 1.0.1 | 1.1.0 | Change | Change on PHP 8.4 |
|---|--:|--:|--:|--:|--:|
| integers | 4 | 22.1 k | 89.1 k | ×4.0 | ×3.8 |
| integers | 8 | 20.4 k | 80.4 k | ×3.9 | ×3.9 |
| rows of 14 strings | 4 | 4.97 k | 72.9 k | ×15 | ×13 |
| rows of 14 strings | 8 | 4.00 k | 38.0 k | ×9.5 | ×8.9 |
| strings of 1 KB | 4 | 10.6 k | 72.9 k | ×6.9 | – |
| strings of 1 KB | 8 | 9.85 k | 50.2 k | ×5.1 | – |
| lists of 1000 integers | 4 | 11.0 k | 48.0 k | ×4.4 | – |
| lists of 1000 integers | 8 | 9.59 k | 32.8 k | ×3.4 | – |
| arrays of 100 rows | 4 | 467 | 22.1 k | ×47 | – |
| arrays of 100 rows | 8 | 482 | 27.2 k | ×56 | – |
| mix | 4 | 15.7 k | 84.6 k | ×5.4 | ×4.9 |
| mix | 8 | 15.5 k | 63.7 k | ×4.1 | ×4.0 |

</details>

## 1.0.0 → 1.0.1

- **Replacing a value:** 1.0.0 kept a bug of WinCache 2.0.0.8: a set that replaces an entry (and a delete) takes time in
  proportion to the number of entries stored, while it holds the cache lock. 1.0.1 takes the same time with any number
  of entries:

  | Entries stored | 1.0.0: a row | 1.0.1: a row | 1.0.0: an integer | 1.0.1: an integer |
  |--:|--:|--:|--:|--:|
  | 100 | 23 µs | 14 µs | 3.0 µs | 3.6 µs |
  | 1,000 | 159 µs | 15 µs | 4.6 µs | 3.4 µs |
  | 5,000 | 1.2 ms | 16 µs | 14 µs | 3.7 µs |
  | 20,000 | 6.6 ms | 17 µs | 13 µs | 3.9 µs |

  So one process made 7.2 times as many sets of these workloads with 1.0.1 (geometric mean), and 76 times as many rows:
  the factors depend on the number of entries stored, and grow with it.
- **A reader beside writers:** in 1.0.0, a process that reads beside 3 or 7 processes that set back to back waits for
  each of those replaces: with 1.0.1, it made 36 to 19,000 times as many gets (340 times on average), its median get
  taking 1 to 47 µs instead of 108 µs to 38 ms.
- **Reads:** 1.0.0 leaks a copy of every array it reads, until the end of the request:

  | Workload | Left behind per get, 1.0.0 | 1.0.1 |
  |---|--:|--:|
  | integers | none | none |
  | rows of 14 strings | 1,600 bytes | none |
  | strings of 1 KB | none | none |
  | lists of 1000 integers | 32,824 bytes | none |
  | arrays of 100 rows | 164,152 bytes | none |
  | mix | 109 bytes | none |

  1.0.1 read integers and strings as fast as 1.0.0 (−11% to −1%), rows 11 to 30% faster, lists of 1000 integers 5.4
  to 8 times as fast and arrays of 100 rows 4.7 to 5.2 times as fast: in these runs, 1.0.0 took new memory for every
  array it read.
- **Writes with several processes:** 1.0.1 copies values before it takes the cache lock, so that its writers no longer
  hold it for their copies, but each block of a value takes the allocator's lock. With 4 or 8 processes setting strings
  of 1 KB or the mix back to back, 1.0.1 made 8 to 26% fewer sets than 1.0.0, and its sets of arrays of 100 rows (148
  blocks each) fell from 4,380 a second with one process to 475 with 4, though still 4.4 to 4.6 times as many as 1.0.0.
- **Memory:** unchanged: 1.0.1 kept the memory layout of 1.0.0.

The changes from 1.0.0 to 1.0.1 (PHP 7.2 only):

| Load | 1 process | 4 and 8 processes |
|---|--:|--:|
| Gets only | +89% | +77% |
| 1% sets | ×4.0 | ×3.2 |
| 5% sets | ×5.8 | ×3.9 |
| One writer beside readers: the readers' gets | – | ×12 |
| One writer beside readers: the writer's sets | – | ×4.5 |
| 50% sets | ×7.8 | ×3.1 |
| 80% sets | ×8.5 | ×2.8 |
| Sets only | ×7.2 | ×2.8 |
| One reader beside writers: the reader's gets | – | ×340 |
| One reader beside writers: the writers' sets | – | ×2.8 |

<details>
<summary>Gets only (gets a second)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 1 | 719 k | 701 k | −2% |
| integers | 4 | 216 k | 215 k | −1% |
| integers | 8 | 208 k | 198 k | −5% |
| rows of 14 strings | 1 | 360 k | 466 k | +30% |
| rows of 14 strings | 4 | 139 k | 153 k | +11% |
| rows of 14 strings | 8 | 129 k | 146 k | +13% |
| strings of 1 KB | 1 | 698 k | 635 k | −9% |
| strings of 1 KB | 4 | 239 k | 212 k | −11% |
| strings of 1 KB | 8 | 162 k | 155 k | −4% |
| lists of 1000 integers | 1 | 14.9 k | 119 k | ×8.0 |
| lists of 1000 integers | 4 | 14.1 k | 79.3 k | ×5.6 |
| lists of 1000 integers | 8 | 13.2 k | 71.1 k | ×5.4 |
| arrays of 100 rows | 1 | 4.77 k | 22.3 k | ×4.7 |
| arrays of 100 rows | 4 | 4.53 k | 23.2 k | ×5.1 |
| arrays of 100 rows | 8 | 4.33 k | 22.3 k | ×5.2 |
| mix | 1 | 467 k | 495 k | +6% |
| mix | 4 | 153 k | 148 k | −3% |
| mix | 8 | 119 k | 141 k | +19% |

</details>
<details>
<summary>1% sets (calls a second)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 1 | 346 k | 635 k | +83% |
| integers | 4 | 161 k | 186 k | +16% |
| integers | 8 | 134 k | 187 k | +40% |
| rows of 14 strings | 1 | 11.3 k | 408 k | ×36 |
| rows of 14 strings | 4 | 9.43 k | 148 k | ×16 |
| rows of 14 strings | 8 | 10.4 k | 133 k | ×13 |
| strings of 1 KB | 1 | 483 k | 519 k | +8% |
| strings of 1 KB | 4 | 211 k | 224 k | +6% |
| strings of 1 KB | 8 | 166 k | 188 k | +14% |
| lists of 1000 integers | 1 | 14.1 k | 110 k | ×7.8 |
| lists of 1000 integers | 4 | 12.2 k | 73.3 k | ×6.0 |
| lists of 1000 integers | 8 | 12.1 k | 70.4 k | ×5.8 |
| arrays of 100 rows | 1 | 3.19 k | 20.2 k | ×6.3 |
| arrays of 100 rows | 4 | 3.39 k | 21.8 k | ×6.4 |
| arrays of 100 rows | 8 | 3.06 k | 19.5 k | ×6.4 |
| mix | 1 | 358 k | 439 k | +22% |
| mix | 4 | 99.5 k | 138 k | +39% |
| mix | 8 | 100 k | 131 k | +31% |

</details>
<details>
<summary>5% sets (calls a second)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 1 | 124 k | 459 k | ×3.7 |
| integers | 4 | 79.3 k | 152 k | +92% |
| integers | 8 | 65.2 k | 139 k | ×2.1 |
| rows of 14 strings | 1 | 2.66 k | 268 k | ×100 |
| rows of 14 strings | 4 | 3.41 k | 94.0 k | ×28 |
| rows of 14 strings | 8 | 2.96 k | 86.2 k | ×29 |
| strings of 1 KB | 1 | 231 k | 236 k | +2% |
| strings of 1 KB | 4 | 136 k | 164 k | +20% |
| strings of 1 KB | 8 | 97.9 k | 156 k | +60% |
| lists of 1000 integers | 1 | 12.9 k | 101 k | ×7.8 |
| lists of 1000 integers | 4 | 11.9 k | 69.6 k | ×5.8 |
| lists of 1000 integers | 8 | 11.4 k | 66.0 k | ×5.8 |
| arrays of 100 rows | 1 | 1.45 k | 16.2 k | ×11 |
| arrays of 100 rows | 4 | 1.53 k | 9.12 k | ×6.0 |
| arrays of 100 rows | 8 | 1.49 k | 8.25 k | ×5.5 |
| mix | 1 | 252 k | 279 k | +11% |
| mix | 4 | 93.3 k | 136 k | +46% |
| mix | 8 | 94.6 k | 119 k | +26% |

</details>
<details>
<summary>One writer beside readers: the readers' gets (gets a second of the 3 or 7 readers together)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 4 | 11.7 k | 151 k | ×13 |
| integers | 8 | 35.7 k | 155 k | ×4.3 |
| rows of 14 strings | 4 | 542 | 137 k | ×250 |
| rows of 14 strings | 8 | 1.17 k | 135 k | ×110 |
| strings of 1 KB | 4 | 34.6 k | 176 k | ×5.1 |
| strings of 1 KB | 8 | 61.6 k | 164 k | ×2.7 |
| lists of 1000 integers | 4 | 7.31 k | 62.3 k | ×8.5 |
| lists of 1000 integers | 8 | 8.96 k | 65.0 k | ×7.3 |
| arrays of 100 rows | 4 | 300 | 17.6 k | ×59 |
| arrays of 100 rows | 8 | 687 | 19.5 k | ×28 |
| mix | 4 | 42.2 k | 120 k | ×2.8 |
| mix | 8 | 61.0 k | 127 k | ×2.1 |

</details>
<details>
<summary>One writer beside readers: the writer's sets (sets a second of the writer)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 4 | 3.90 k | 18.8 k | ×4.8 |
| integers | 8 | 5.10 k | 10.1 k | +98% |
| rows of 14 strings | 4 | 181 | 14.1 k | ×78 |
| rows of 14 strings | 8 | 168 | 8.03 k | ×48 |
| strings of 1 KB | 4 | 11.5 k | 11.1 k | −4% |
| strings of 1 KB | 8 | 8.80 k | 7.80 k | −11% |
| lists of 1000 integers | 4 | 2.44 k | 10.1 k | ×4.2 |
| lists of 1000 integers | 8 | 1.28 k | 4.53 k | ×3.5 |
| arrays of 100 rows | 4 | 100 | 1.93 k | ×19 |
| arrays of 100 rows | 8 | 98 | 1.11 k | ×11 |
| mix | 4 | 14.1 k | 12.8 k | −9% |
| mix | 8 | 8.72 k | 7.66 k | −12% |

</details>
<details>
<summary>50% sets (calls a second)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 1 | 14.2 k | 88.0 k | ×6.2 |
| integers | 4 | 12.7 k | 48.0 k | ×3.8 |
| integers | 8 | 10.5 k | 44.6 k | ×4.2 |
| rows of 14 strings | 1 | 341 | 51.7 k | ×150 |
| rows of 14 strings | 4 | 277 | 9.47 k | ×34 |
| rows of 14 strings | 8 | 344 | 8.30 k | ×24 |
| strings of 1 KB | 1 | 29.8 k | 30.9 k | +4% |
| strings of 1 KB | 4 | 23.4 k | 19.8 k | −15% |
| strings of 1 KB | 8 | 22.2 k | 19.8 k | −11% |
| lists of 1000 integers | 1 | 9.91 k | 68.4 k | ×6.9 |
| lists of 1000 integers | 4 | 8.43 k | 23.0 k | ×2.7 |
| lists of 1000 integers | 8 | 8.37 k | 20.7 k | ×2.5 |
| arrays of 100 rows | 1 | 230 | 7.01 k | ×30 |
| arrays of 100 rows | 4 | 219 | 851 | ×3.9 |
| arrays of 100 rows | 8 | 229 | 827 | ×3.6 |
| mix | 1 | 53.7 k | 57.1 k | +6% |
| mix | 4 | 35.2 k | 30.7 k | −13% |
| mix | 8 | 33.3 k | 29.5 k | −11% |

</details>
<details>
<summary>80% sets (calls a second)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 1 | 8.21 k | 57.4 k | ×7.0 |
| integers | 4 | 6.76 k | 29.3 k | ×4.3 |
| integers | 8 | 6.62 k | 27.6 k | ×4.2 |
| rows of 14 strings | 1 | 159 | 35.0 k | ×220 |
| rows of 14 strings | 4 | 158 | 5.99 k | ×38 |
| rows of 14 strings | 8 | 361 | 5.05 k | ×14 |
| strings of 1 KB | 1 | 19.4 k | 20.6 k | +6% |
| strings of 1 KB | 4 | 16.9 k | 13.2 k | −22% |
| strings of 1 KB | 8 | 16.3 k | 12.4 k | −24% |
| lists of 1000 integers | 1 | 8.93 k | 58.4 k | ×6.5 |
| lists of 1000 integers | 4 | 7.46 k | 14.9 k | ×2.0 |
| lists of 1000 integers | 8 | 7.25 k | 13.8 k | +91% |
| arrays of 100 rows | 1 | 137 | 4.77 k | ×35 |
| arrays of 100 rows | 4 | 144 | 533 | ×3.7 |
| arrays of 100 rows | 8 | 134 | 535 | ×4.0 |
| mix | 1 | 33.2 k | 34.4 k | +4% |
| mix | 4 | 23.0 k | 19.0 k | −17% |
| mix | 8 | 21.4 k | 17.2 k | −19% |

</details>
<details>
<summary>Sets only (sets a second)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 1 | 6.97 k | 47.1 k | ×6.8 |
| integers | 4 | 6.34 k | 24.5 k | ×3.9 |
| integers | 8 | 6.47 k | 24.6 k | ×3.8 |
| rows of 14 strings | 1 | 382 | 29.2 k | ×76 |
| rows of 14 strings | 4 | 174 | 4.86 k | ×28 |
| rows of 14 strings | 8 | 231 | 4.52 k | ×20 |
| strings of 1 KB | 1 | 15.6 k | 16.6 k | +6% |
| strings of 1 KB | 4 | 13.5 k | 10.4 k | −23% |
| strings of 1 KB | 8 | 12.0 k | 10.1 k | −16% |
| lists of 1000 integers | 1 | 8.48 k | 51.6 k | ×6.1 |
| lists of 1000 integers | 4 | 6.76 k | 11.9 k | +75% |
| lists of 1000 integers | 8 | 6.56 k | 11.3 k | +72% |
| arrays of 100 rows | 1 | 120 | 4.38 k | ×37 |
| arrays of 100 rows | 4 | 108 | 475 | ×4.4 |
| arrays of 100 rows | 8 | 114 | 529 | ×4.6 |
| mix | 1 | 27.0 k | 29.7 k | +10% |
| mix | 4 | 17.5 k | 16.1 k | −8% |
| mix | 8 | 19.2 k | 14.2 k | −26% |

</details>
<details>
<summary>One reader beside writers: the reader's gets (gets a second of the reader)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 4 | 1.78 k | 287 k | ×160 |
| integers | 8 | 726 | 269 k | ×370 |
| rows of 14 strings | 4 | 61 | 449 k | ×7,400 |
| rows of 14 strings | 8 | 21 | 401 k | ×19,000 |
| strings of 1 KB | 4 | 4.48 k | 534 k | ×120 |
| strings of 1 KB | 8 | 1.71 k | 498 k | ×290 |
| lists of 1000 integers | 4 | 1.90 k | 84.4 k | ×44 |
| lists of 1000 integers | 8 | 875 | 83.3 k | ×95 |
| arrays of 100 rows | 4 | 38 | 18.8 k | ×490 |
| arrays of 100 rows | 8 | 16 | 18.3 k | ×1,100 |
| mix | 4 | 6.91 k | 251 k | ×36 |
| mix | 8 | 2.90 k | 253 k | ×87 |

The reader's get, median / 99th percentile:

| Workload | Processes | 1.0.0 | 1.0.1 |
|---|--:|--:|--:|
| integers | 4 | 448 µs / 3.1 ms | 2 µs / 21 µs |
| integers | 8 | 1.1 ms / 6.0 ms | 2 µs / 19 µs |
| rows of 14 strings | 4 | 6.7 ms / 48.9 ms | 2 µs / 11 µs |
| rows of 14 strings | 8 | 38.3 ms / 155.7 ms | 2 µs / 13 µs |
| strings of 1 KB | 4 | 201 µs / 553 µs | 1 µs / 12 µs |
| strings of 1 KB | 8 | 499 µs / 1.6 ms | 1 µs / 13 µs |
| lists of 1000 integers | 4 | 510 µs / 943 µs | 10 µs / 27 µs |
| lists of 1000 integers | 8 | 1.1 ms / 1.9 ms | 11 µs / 27 µs |
| arrays of 100 rows | 4 | 23.8 ms / 41.2 ms | 47 µs / 119 µs |
| arrays of 100 rows | 8 | 46.7 ms / 76.1 ms | 46 µs / 111 µs |
| mix | 4 | 108 µs / 505 µs | 3 µs / 21 µs |
| mix | 8 | 275 µs / 1.2 ms | 3 µs / 21 µs |

</details>
<details>
<summary>One reader beside writers: the writers' sets (sets a second of the 3 or 7 writers together)</summary>

| Workload | Processes | 1.0.0 | 1.0.1 | Change |
|---|--:|--:|--:|--:|
| integers | 4 | 5.35 k | 22.1 k | ×4.1 |
| integers | 8 | 5.08 k | 20.4 k | ×4.0 |
| rows of 14 strings | 4 | 182 | 4.97 k | ×27 |
| rows of 14 strings | 8 | 146 | 4.00 k | ×27 |
| strings of 1 KB | 4 | 13.4 k | 10.6 k | −21% |
| strings of 1 KB | 8 | 11.9 k | 9.85 k | −18% |
| lists of 1000 integers | 4 | 5.69 k | 11.0 k | +94% |
| lists of 1000 integers | 8 | 6.13 k | 9.59 k | +57% |
| arrays of 100 rows | 4 | 114 | 467 | ×4.1 |
| arrays of 100 rows | 8 | 113 | 482 | ×4.3 |
| mix | 4 | 20.7 k | 15.7 k | −24% |
| mix | 8 | 20.3 k | 15.5 k | −24% |

</details>

## How it was measured

- **Machine:** AMD Ryzen 7 7840HS (8 cores, 16 threads), Windows 11 Pro 10.0.26200.
- **PHP:** 7.2.34 and 8.4.14, NTS x64, from the command line (`ucache.enablecli=1`). 1.0.0 builds for PHP 7.2 only, and
  PHP 8.4 ran three of the six workloads (integers, rows of 14 strings, the mix): the tables give a dash for the others.
- **Versions:** the DLLs were built from the sources of each version, with the toolsets and devel packs that build.bat
  uses (v141 and the 7.2.34 devel pack for PHP 7.2, v143 and the 8.4.27 devel pack for PHP 8.4).
- **Two series of runs:** 1.1.0, 1.1.1 and 1.1.1 with `ucache.lockedcopy=0` in one, 1.0.0, 1.0.1 and 1.1.0 in the other,
  on the same day. Each step compares two versions of the same series, whose runs alternated.
- **Cache:** `ucache.size=128`, every other setting at its default. 1.0.0 ran without a memory_limit
  (`memory_limit=-1`), as it leaks a copy of every array it reads (see [1.0.0 → 1.0.1](#100--101)). Each run has a
  cache of its own (its own `ucache.apppoolid`), which a first process fills with the keys of the workload before the
  run, so that every get finds its key; a set replaces an entry with a value of the same shape.
- **Calls:** 1, 4 or 8 processes start together and call the cache back to back for 2 seconds, each with keys picked at
  random among those of the workload, and nothing else between the calls. This measures what the cache itself can take,
  not what a web server sees: requests that do other work between their calls rarely find the cache lock taken.
- **Loads:** in the mixed loads, each process makes a set or a get at random for each call, in the given proportion.

  | Load | What the processes do |
  |---|---|
  | Gets only | every process gets |
  | 1% sets, 5% sets | every process makes 1 or 5 sets in 100 calls, and gets otherwise |
  | One writer beside readers | one process sets back to back, the 3 or 7 others get |
  | 50% sets | every process makes a set or a get with equal odds |
  | 80% sets | every process makes 4 sets in 5 calls, and gets otherwise |
  | Sets only | every process sets |
  | One reader beside writers | one process gets back to back, the 3 or 7 others set |

- **Workloads:** keys of 12 to 16 characters.

  | Workload | Value | Keys |
  |---|---|--:|
  | integers | an integer | 100,000 |
  | rows of 14 strings | an array of 14 strings of 1 to 42 characters, with string keys (446 bytes serialized) | 20,000 |
  | strings of 1 KB | a string of 1024 bytes | 50,000 |
  | lists of 1000 integers | `range(1, 1000)` | 2,000 |
  | arrays of 100 rows | a list of 100 rows of 14 strings (45 KB serialized) | 200 |
  | mix | 60% integers, 20% strings of 29 bytes, 8% `[1, 2, 3]`, 6% rows of 14 strings, 5% rows of 14 strings serialized (strings of 446 bytes), 1% strings of 10 KB | 100,000 |

  The shared memory that one entry takes, with its key (on average for the mix). 1.1.1 copies the values of the first
  three workloads under the cache lock by default, as their entries take less than 2048 bytes:

  | Workload | 1.0.0 | 1.0.1 | 1.1.0 | 1.1.1 |
  |---|--:|--:|--:|--:|
  | integers | 183 bytes | 183 bytes | 183 bytes | 143 bytes |
  | rows of 14 strings | 6,396 bytes | 6,396 bytes | 1,948 bytes | 1,748 bytes |
  | strings of 1 KB | 1,278 bytes | 1,278 bytes | 1,278 bytes | 1,198 bytes |
  | lists of 1000 integers | 33,056 bytes | 33,056 bytes | 32,416 bytes | 32,216 bytes |
  | arrays of 100 rows | 94,528 bytes | 94,528 bytes | 83,632 bytes | 83,432 bytes |
  | mix | 765 bytes | 765 bytes | 464 bytes | 391 bytes |

  The blocks of shared memory that one entry takes, each allocated under the allocator's lock:

  | Workload | 1.0.0 | 1.0.1 | 1.1.0 | 1.1.1 |
  |---|--:|--:|--:|--:|
  | integers | 2 | 2 | 2 | 1 |
  | rows of 14 strings | 12 | 12 | 4 | 1 |
  | strings of 1 KB | 3 | 3 | 3 | 1 |
  | lists of 1000 integers | 8 | 8 | 4 | 1 |
  | arrays of 100 rows | 148 | 148 | 4 | 1 |
  | mix | 3.18 | 3.18 | 2.54 | 1 |

- **Rounds:** every case (load, workload, processes and version) ran 3 times on PHP 7.2 and twice on PHP 8.4, the
  versions of a series one after the other, in an order that changed from one case to the next; the tables give the
  medians of the rounds. One get in 8 is timed, and one set in 8 (every set with 1% and 5% sets): with `microtime()` on
  PHP 7.2, to the microsecond, and with `hrtime()` on PHP 8.4.
- **Noise:** the throughput of a version varied between the rounds of a case by 2 to 7% at the median on PHP 8.4, and in
  the 50/50 and write-heavy loads of the first series on PHP 7.2; it varied by 11 to 12% with one process and 16 to 19%
  with 4 or 8 in the read-heavy loads of the first series and in every load of the second series on PHP 7.2 (up to 35%
  in 9 cases out of 10). A difference of less than about 15% in one row of a table of PHP 7.2 can be noise: the
  geometric means of the summaries, and the runs of PHP 8.4, are steadier.
