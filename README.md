# Multithreaded-File-Hasher

A C program that computes a single hash value for a file using a **binary tree of POSIX threads**. The file is split into equal chunks, each thread hashes its own chunk, and the results are combined up the tree into one final hash. It also reports how long the hashing took, so you can compare performance across thread counts.

## How it works

1. The whole file is read into memory and divided into `num_threads` chunks (the last thread also takes any leftover bytes).
2. Thread `i` hashes chunk `i` with the **Jenkins one-at-a-time** hash.
3. Thread `i` creates its children, threads `2i + 1` and `2i + 2` (if they exist), and waits for their results.
4. Each thread concatenates its hash with its children's hashes as a string and hashes that string again.
5. The root thread (thread 0) returns the final hash, which `main` prints along with the elapsed time.

## Build

```bash
gcc -Wall -pthread -o htree htree.c
```

## Run

```bash
./htree <filename> <num_threads>
```

Example:

```
$ ./htree bigfile.bin 4
hash value = 3687639898
time taken = 0.000603
```

## Notes

- `num_threads` must be greater than 0 and no larger than the file size in bytes.
- An empty file prints `hash value = 0`.
- The hash depends on the number of threads, so the same file hashed with different thread counts produces different values.
