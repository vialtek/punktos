# Punkt OS

Punkt is a microkernel based OS built on top of LK. We are currently in the early inception stage of the project.

Aim of the first milestone is to create userland and syscalls infrastructure.


See https://github.com/littlekernel/lk for more information about LK.

### High Level Overview

- Microkernel based OS
- Built on top of LK, inspired by Zircon
- Fully-reentrant multi-threaded preemptive kernel
- Portable to 32 and 64 bit architectures

### Supported architectures

- ARM32
  - ARMv7+ Cortex-A class cores
- ARM64
  - ARMv8 and ARMv9 cores
- x86-64
