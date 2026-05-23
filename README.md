# xbks

`xbks` is a 64-bit freestanding kernel intended to live as an independent Git
submodule under XiaobaiOS and future distributions.

Current stage:

- Limine Boot Protocol entry path.
- LLVM/Clang C23 kernel build.
- GNU Make incremental build.
- Hybrid BIOS/UEFI ISO generation through the in-tree Limine submodule.
- Serial diagnostics, framebuffer terminal output, and an early static shell.
- Linux-like x86_64 syscall ABI scaffolding for early userland bring-up.

The kernel is intentionally built in small verifiable stages. Architecture
tables, interrupt routing, physical memory management, virtual memory, ELF64
loading, and user mode will be expanded behind the existing subsystem
interfaces instead of being rushed into temporary code.

## Syscall ABI

The public syscall ABI lives in `include/xbks/uapi/` and uses Linux x86_64
register conventions and common syscall numbers. Currently implemented calls
are enough for early libc and init bring-up: `read`, `write`, `close`, `brk`,
`getpid`, `gettid`, `getppid`, uid/gid queries, `uname`, `getcwd`, `exit`, and
`exit_group`.

Filesystem, VM, process, signal, time, futex, and networking calls are already
assigned Linux-compatible numbers but return Linux-style negative errno until
the matching kernel subsystems exist.

## Build

Initialize submodules first:

```sh
git submodule update --init --recursive
```

Build a bootable ISO:

```sh
make iso
```

Run it in QEMU:

```sh
make run
```

The build uses `third_party/limine` first. If Limine has not been built yet,
`make` bootstraps and compiles only the x86 BIOS CD, x86_64 UEFI, and UEFI CD
targets. If the required Limine products already exist, that step is skipped.
