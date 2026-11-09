# Feudal Process Tree

A C simulation that models a feudal hierarchy as a tree of Unix processes. Each lord runs in a separate process. Commands can grant land, report on a subtree, or exchange territory between lords.

The simulation uses `fork()` and `exec()` to create child processes, anonymous pipes to communicate between parents and children, and named pipes for exchanges between lords.

## Build and run

From the repository root on Linux with GCC and Make installed:

```sh
make
./monarch WHY < sample-testcases/sample-input-1.txt
```
