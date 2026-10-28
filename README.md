# featherweight

A non-preemptive, user-level thread library for x86-64 Linux, written in C.

LWP stands for *lightweight process*. Every "thread" here runs in the same
address space on a single kernel thread and cooperatively hands off the CPU,
so they are lighter than lightweight — hence *featherweight*.

This is an implementation of the LWP library for Cal Poly's CPE/CSC 453
(Operating Systems) Programming Assignment 3. It implements thread creation,
cooperative context switching, round-robin scheduling, termination, and
joining, all on top of a single assembly primitive (`swap_rfiles`) that
atomically swaps register files.

## What it does

The library exposes this API (declared in `lwp.h`):

| Function | Purpose |
|---|---|
| `lwp_create(fn, arg)` | Create a thread that runs `fn(arg)` on its own stack |
| `lwp_start()` | Turn the calling (system) thread into an LWP and start scheduling |
| `lwp_yield()` | Cooperatively hand the CPU to the next scheduled thread |
| `lwp_exit(status)` | Terminate the calling thread |
| `lwp_wait(&status)` | Reap a terminated thread (FIFO), blocking if needed |
| `lwp_gettid()` | Thread id of the caller |
| `tid2thread(tid)` | Look up a thread by id |
| `lwp_set_scheduler(s)` / `lwp_get_scheduler()` | Swap the scheduling policy at runtime |

Key design points:

- **Stacks** are allocated with `mmap(2)` (not `malloc`), sized from the soft
  `RLIMIT_STACK` or 8 MB, rounded up to a page. Each stack is its own mapping,
  so an overflow faults instead of corrupting a neighbor.
- **Context switches** go through `swap_rfiles()` (`magic64.S`), which saves and
  restores all 16 integer registers plus the FPU state. New threads get a
  hand-built stack frame so that the first switch "returns" into a wrapper that
  calls the thread function and then `lwp_exit()`.
- **Scheduling** defaults to round robin (`rr.c`) but can be replaced at runtime
  with any struct of `init`/`shutdown`/`admit`/`remove`/`next`/`qlen` function
  pointers.
- **Joining** returns terminated threads in FIFO order and blocks waiters on a
  separate queue, waking the oldest waiter when a thread exits.

The implementation details are in `lwp.c`, which is the file I wrote.

## Building

Requires `gcc` and GNU `make` on x86-64 Linux. (The library uses `mmap`,
`getrlimit`, and x86-64 assembly — it is Linux/x86-64 only.)

```bash
make            # builds util64.o and liblwp.so
```

## Testing

Two self-checking test programs. Each prints `PASS`/`FAIL` per check and exits
non-zero if anything fails.

```bash
make test       # lwptest:  core behavior (creation, switching, FIFO wait, FP, ...)
make edgetest   # lwpedge:  edge cases (stack sizing, mmap failure, scheduler swap, ...)
```

`lwpedge` deliberately manipulates `RLIMIT_STACK`; on locked-down systems some
of those checks print `SKIP` instead of running.

The instructor's number demo (needs the prebuilt `libsnakes.so`, see below):

```bash
make nums && LD_LIBRARY_PATH=. ./nums
```

## What's in this repo (and what isn't)

**Mine** (MIT licensed, see `LICENSE`):

- `lwp.c` — the LWP library implementation
- `lwptest.c`, `lwpedge.c` — test programs
- `README.md`, build/`Makefile` changes

**Instructor-supplied starter code and spec** (included for context, not my work):

- `PA3.md`, `PA3.pdf` — the assignment specification
- `lwp.h`, `fp.h` — API and FPU-state headers
- `rr.c`, `rr.h`, `schedulers.h`, `AlwaysZero.c` — scheduler scaffolding
- `magic64.S` — the `swap_rfiles` context-switch primitive
- `util.c`, `util.h`, `snakes.h` — demo support (`util.c` has two minimal
  declaration fixes from me for GCC 14)
- `numbersmain.c`, `randomsnakes.c`, `hungrysnakes.c` — demo programs
- `README.orig` — the original directory-contents note
- `demos/` — a copy of the demo programs

**Not in this repo:** the prebuilt binaries `libsnakes.so`, `libPLN.so`, and the
`lib64/` directory. Those are the instructor's compiled libraries and are not
mine to redistribute; they're excluded in `.gitignore`. To run the snake/number
demos you need `libsnakes.so` from the course materials.

## Credits

This assignment is from Cal Poly San Luis Obispo's CPE/CSC 453. Per the handout:

> This assignment is reproduced from Professor Foaad Khosmood. The assignment,
> originally by Dr. Nico, is adopted from versions by Dr. Bellardo and Nico,
> Cal Poly Computer Science.

The specification, starter code, headers, assembly primitive, and demo programs
are the work of those authors. This repository's contribution is the LWP library
implementation and tests.

## License

The original work here is under the [MIT License](LICENSE). That license covers
only my code; it does not cover the instructor-supplied material listed above,
which remains the property of its original authors.
