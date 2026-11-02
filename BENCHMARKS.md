# UCache benchmarks

How fast ucache 1.0.1 reads and writes, when one process or several call the cache back to back, against 1.0.0. Measured
on 2026-11-02, on PHP 7.2, the one version 1.0.0 builds for.

**Reading the tables:** each figure compares 1.0.1 with 1.0.0. `+12%` means 12% more calls a second, `×2.4` means 2.4
times as many, `−68%` means 68% fewer, and drops of 20% or more are in bold. The summary tables give the geometric
mean over the six workloads; the tables folded under the summary give the calls a second of each workload. A difference
of less than about 15% can be noise (see [How it was measured](#how-it-was-measured)).

## At a glance

PHP 7.2, the six workloads together. One process:

| Load | 1.0.0 → 1.0.1 |
|---|--:|
| Gets only | +89% |
| 1% sets | ×4.0 |
| 5% sets | ×5.8 |
| 50% sets | ×7.8 |
| 80% sets | ×8.5 |
| Sets only | ×7.2 |

4 and 8 processes:

| Load | 1.0.0 → 1.0.1 |
|---|--:|
| Gets only | +77% |
| 1% sets | ×3.2 |
| 5% sets | ×3.9 |
| One writer beside readers: the readers' gets | ×12 |
| One writer beside readers: the writer's sets | ×4.5 |
| 50% sets | ×3.1 |
| 80% sets | ×2.8 |
| Sets only | ×2.8 |
| One reader beside writers: the reader's gets | ×340 |
| One reader beside writers: the writers' sets | ×2.8 |

- **Reads:** 1.0.1 reads integers and strings as fast as 1.0.0, and arrays several times as fast, as 1.0.0 leaks a copy
  of every array it reads.
- **Writes:** with one process, 1.0.1 makes 7.2 times as many sets as 1.0.0 (the geometric mean over the workloads).
- **A reader beside writers** that write back to back makes 340 times as many gets with 1.0.1: 1.0.0's writers held the
  cache lock while they replaced entries (6.6 ms for a row among 20,000).

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
- **PHP:** 7.2.34, NTS x64, from the command line (`ucache.enablecli=1`), the one version 1.0.0 builds for.
- **Versions:** the DLLs were built from the sources of each version, with the toolset and devel pack that build.bat
  uses for PHP 7.2 (v141 and the 7.2.34 devel pack).
- **Runs:** 1.0.0 and 1.0.1 ran in one series with 1.1.0, their runs alternating, so that they were measured together.
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

  The shared memory that one entry takes, with its key (on average for the mix):

  | Workload | 1.0.0 | 1.0.1 |
  |---|--:|--:|
  | integers | 183 bytes | 183 bytes |
  | rows of 14 strings | 6,396 bytes | 6,396 bytes |
  | strings of 1 KB | 1,278 bytes | 1,278 bytes |
  | lists of 1000 integers | 33,056 bytes | 33,056 bytes |
  | arrays of 100 rows | 94,528 bytes | 94,528 bytes |
  | mix | 765 bytes | 765 bytes |

  The blocks of shared memory that one entry takes, each allocated under the allocator's lock:

  | Workload | 1.0.0 | 1.0.1 |
  |---|--:|--:|
  | integers | 2 | 2 |
  | rows of 14 strings | 12 | 12 |
  | strings of 1 KB | 3 | 3 |
  | lists of 1000 integers | 8 | 8 |
  | arrays of 100 rows | 148 | 148 |
  | mix | 3.18 | 3.18 |

- **Rounds:** every case (load, workload, processes and version) ran 3 times, the versions one after the other, in an
  order that changed from one case to the next; the tables give the medians of the rounds. One get in 8 is timed, and
  one set in 8 (every set with 1% and 5% sets), with `microtime()`, to the microsecond.
- **Noise:** the throughput of a version varied between the rounds of a case by 12% at the median with one process and
  16% with 4 or 8 (up to 35% in 9 cases out of 10). A difference of less than about 15% in one row of a table can be
  noise: the geometric means of the summaries are steadier.
