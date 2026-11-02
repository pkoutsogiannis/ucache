# UCache benchmarks

How fast ucache 1.0.0 reads and writes, when one process or several call the cache back to back. Measured on 2026-11-02,
on PHP 7.2, the one version it builds for.

**Reading the tables:** each figure is a number of calls a second, all the processes together, the median of 3 runs:
`719 k` is 719,000, `4.77 k` is 4,770. The tables folded below give the same with 1, 4 and 8 processes. A difference of
less than about 15% can be noise (see [How it was measured](#how-it-was-measured)).

## At a glance

PHP 7.2. One process:

| Load | integers | rows of 14 strings | strings of 1 KB | lists of 1000 integers | arrays of 100 rows | mix |
|---|--:|--:|--:|--:|--:|--:|
| Gets only | 719 k | 360 k | 698 k | 14.9 k | 4.77 k | 467 k |
| 1% sets | 346 k | 11.3 k | 483 k | 14.1 k | 3.19 k | 358 k |
| 5% sets | 124 k | 2.66 k | 231 k | 12.9 k | 1.45 k | 252 k |
| 50% sets | 14.2 k | 341 | 29.8 k | 9.91 k | 230 | 53.7 k |
| 80% sets | 8.21 k | 159 | 19.4 k | 8.93 k | 137 | 33.2 k |
| Sets only | 6.97 k | 382 | 15.6 k | 8.48 k | 120 | 27.0 k |

8 processes:

| Load | integers | rows of 14 strings | strings of 1 KB | lists of 1000 integers | arrays of 100 rows | mix |
|---|--:|--:|--:|--:|--:|--:|
| Gets only | 208 k | 129 k | 162 k | 13.2 k | 4.33 k | 119 k |
| 1% sets | 134 k | 10.4 k | 166 k | 12.1 k | 3.06 k | 100 k |
| 5% sets | 65.2 k | 2.96 k | 97.9 k | 11.4 k | 1.49 k | 94.6 k |
| One writer beside readers: the readers' gets | 35.7 k | 1.17 k | 61.6 k | 8.96 k | 687 | 61.0 k |
| One writer beside readers: the writer's sets | 5.10 k | 168 | 8.80 k | 1.28 k | 98 | 8.72 k |
| 50% sets | 10.5 k | 344 | 22.2 k | 8.37 k | 229 | 33.3 k |
| 80% sets | 6.62 k | 361 | 16.3 k | 7.25 k | 134 | 21.4 k |
| Sets only | 6.47 k | 231 | 12.0 k | 6.56 k | 114 | 19.2 k |
| One reader beside writers: the reader's gets | 726 | 21 | 1.71 k | 875 | 16 | 2.90 k |
| One reader beside writers: the writers' sets | 5.08 k | 146 | 11.9 k | 6.13 k | 113 | 20.3 k |

- **Reads:** one process makes 719,000 gets a second of integers, 698,000 of strings of 1 KB and 360,000 of rows of 14
  strings, but only 14,900 of lists of 1000 integers and 4,770 of arrays of 100 rows: 1.0.0 leaks a copy of every array
  it reads, until the end of the request (see [Notes](#notes)).
- **Writes:** a set that replaces an entry takes time in proportion to the number of entries stored, while it holds the
  cache lock: with 20,000 rows stored, one process replaces 382 of them a second (see [Notes](#notes)).
- **The cache lock:** 4 or 8 processes reading integers back to back make 29 to 30% as many gets together as one process
  alone, as every call takes the cache lock. A process that reads beside processes that set back to back waits for each
  of their replaces: beside 7 processes setting rows, it made 21 gets a second.

## The loads in detail

<details>
<summary>Gets only (gets a second)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | 719 k | 216 k | 208 k |
| rows of 14 strings | 360 k | 139 k | 129 k |
| strings of 1 KB | 698 k | 239 k | 162 k |
| lists of 1000 integers | 14.9 k | 14.1 k | 13.2 k |
| arrays of 100 rows | 4.77 k | 4.53 k | 4.33 k |
| mix | 467 k | 153 k | 119 k |

</details>
<details>
<summary>1% sets (calls a second)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | 346 k | 161 k | 134 k |
| rows of 14 strings | 11.3 k | 9.43 k | 10.4 k |
| strings of 1 KB | 483 k | 211 k | 166 k |
| lists of 1000 integers | 14.1 k | 12.2 k | 12.1 k |
| arrays of 100 rows | 3.19 k | 3.39 k | 3.06 k |
| mix | 358 k | 99.5 k | 100 k |

</details>
<details>
<summary>5% sets (calls a second)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | 124 k | 79.3 k | 65.2 k |
| rows of 14 strings | 2.66 k | 3.41 k | 2.96 k |
| strings of 1 KB | 231 k | 136 k | 97.9 k |
| lists of 1000 integers | 12.9 k | 11.9 k | 11.4 k |
| arrays of 100 rows | 1.45 k | 1.53 k | 1.49 k |
| mix | 252 k | 93.3 k | 94.6 k |

</details>
<details>
<summary>One writer beside readers: the readers' gets (gets a second of the 3 or 7 readers together)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | – | 11.7 k | 35.7 k |
| rows of 14 strings | – | 542 | 1.17 k |
| strings of 1 KB | – | 34.6 k | 61.6 k |
| lists of 1000 integers | – | 7.31 k | 8.96 k |
| arrays of 100 rows | – | 300 | 687 |
| mix | – | 42.2 k | 61.0 k |

</details>
<details>
<summary>One writer beside readers: the writer's sets (sets a second of the writer)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | – | 3.90 k | 5.10 k |
| rows of 14 strings | – | 181 | 168 |
| strings of 1 KB | – | 11.5 k | 8.80 k |
| lists of 1000 integers | – | 2.44 k | 1.28 k |
| arrays of 100 rows | – | 100 | 98 |
| mix | – | 14.1 k | 8.72 k |

</details>
<details>
<summary>50% sets (calls a second)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | 14.2 k | 12.7 k | 10.5 k |
| rows of 14 strings | 341 | 277 | 344 |
| strings of 1 KB | 29.8 k | 23.4 k | 22.2 k |
| lists of 1000 integers | 9.91 k | 8.43 k | 8.37 k |
| arrays of 100 rows | 230 | 219 | 229 |
| mix | 53.7 k | 35.2 k | 33.3 k |

</details>
<details>
<summary>80% sets (calls a second)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | 8.21 k | 6.76 k | 6.62 k |
| rows of 14 strings | 159 | 158 | 361 |
| strings of 1 KB | 19.4 k | 16.9 k | 16.3 k |
| lists of 1000 integers | 8.93 k | 7.46 k | 7.25 k |
| arrays of 100 rows | 137 | 144 | 134 |
| mix | 33.2 k | 23.0 k | 21.4 k |

</details>
<details>
<summary>Sets only (sets a second)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | 6.97 k | 6.34 k | 6.47 k |
| rows of 14 strings | 382 | 174 | 231 |
| strings of 1 KB | 15.6 k | 13.5 k | 12.0 k |
| lists of 1000 integers | 8.48 k | 6.76 k | 6.56 k |
| arrays of 100 rows | 120 | 108 | 114 |
| mix | 27.0 k | 17.5 k | 19.2 k |

</details>
<details>
<summary>One reader beside writers: the reader's gets (gets a second of the reader)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | – | 1.78 k | 726 |
| rows of 14 strings | – | 61 | 21 |
| strings of 1 KB | – | 4.48 k | 1.71 k |
| lists of 1000 integers | – | 1.90 k | 875 |
| arrays of 100 rows | – | 38 | 16 |
| mix | – | 6.91 k | 2.90 k |

The reader's get, median / 99th percentile:

| Workload | 4 processes | 8 processes |
|---|--:|--:|
| integers | 448 µs / 3.1 ms | 1.1 ms / 6.0 ms |
| rows of 14 strings | 6.7 ms / 48.9 ms | 38.3 ms / 155.7 ms |
| strings of 1 KB | 201 µs / 553 µs | 499 µs / 1.6 ms |
| lists of 1000 integers | 510 µs / 943 µs | 1.1 ms / 1.9 ms |
| arrays of 100 rows | 23.8 ms / 41.2 ms | 46.7 ms / 76.1 ms |
| mix | 108 µs / 505 µs | 275 µs / 1.2 ms |

</details>
<details>
<summary>One reader beside writers: the writers' sets (sets a second of the 3 or 7 writers together)</summary>

| Workload | 1 process | 4 processes | 8 processes |
|---|--:|--:|--:|
| integers | – | 5.35 k | 5.08 k |
| rows of 14 strings | – | 182 | 146 |
| strings of 1 KB | – | 13.4 k | 11.9 k |
| lists of 1000 integers | – | 5.69 k | 6.13 k |
| arrays of 100 rows | – | 114 | 113 |
| mix | – | 20.7 k | 20.3 k |

</details>

## Notes

- **Replacing a value:** 1.0.0 kept a bug of WinCache 2.0.0.8: a set that replaces an entry (and a delete) takes time in
  proportion to the number of entries stored, while it holds the cache lock:

  | Entries stored | A row | An integer |
  |--:|--:|--:|
  | 100 | 23 µs | 3.0 µs |
  | 1,000 | 159 µs | 4.6 µs |
  | 5,000 | 1.2 ms | 14 µs |
  | 20,000 | 6.6 ms | 13 µs |

- **Reading an array:** 1.0.0 leaks a copy of every array it reads, until the end of the request:

  | Workload | Left behind per get |
  |---|--:|
  | integers | none |
  | rows of 14 strings | 1,600 bytes |
  | strings of 1 KB | none |
  | lists of 1000 integers | 32,824 bytes |
  | arrays of 100 rows | 164,152 bytes |
  | mix | 109 bytes |

  In these runs, 1.0.0 took new memory for every array it read.

## How it was measured

- **Machine:** AMD Ryzen 7 7840HS (8 cores, 16 threads), Windows 11 Pro 10.0.26200.
- **PHP:** 7.2.34, NTS x64, from the command line (`ucache.enablecli=1`), the one version 1.0.0 builds for.
- **Version:** the DLL was built from the sources of 1.0.0, with the v141 toolset and the 7.2.34 devel pack.
- **Runs:** the runs of 1.0.0 alternated with those of 1.0.1 and 1.1.0, whose documents compare them.
- **Cache:** `ucache.size=128`, every other setting at its default, and no memory_limit (`memory_limit=-1`), as 1.0.0
  leaks a copy of every array it reads (see [Notes](#notes)). Each run has a cache of its own (its own
  `ucache.apppoolid`), which a first process fills with the keys of the workload before the run, so that every get finds
  its key; a set replaces an entry with a value of the same shape.
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

  | Workload | 1.0.0 |
  |---|--:|
  | integers | 183 bytes |
  | rows of 14 strings | 6,396 bytes |
  | strings of 1 KB | 1,278 bytes |
  | lists of 1000 integers | 33,056 bytes |
  | arrays of 100 rows | 94,528 bytes |
  | mix | 765 bytes |

  The blocks of shared memory that one entry takes, each allocated under the allocator's lock:

  | Workload | 1.0.0 |
  |---|--:|
  | integers | 2 |
  | rows of 14 strings | 12 |
  | strings of 1 KB | 3 |
  | lists of 1000 integers | 8 |
  | arrays of 100 rows | 148 |
  | mix | 3.18 |

- **Rounds:** every case (load, workload and processes) ran 3 times; the tables give the medians of the rounds. One get
  in 8 is timed, and one set in 8 (every set with 1% and 5% sets), with `microtime()`, to the microsecond.
- **Noise:** the throughput varied between the rounds of a case by 12% at the median with one process and 16% with 4 or
  8 (up to 35% in 9 cases out of 10).
