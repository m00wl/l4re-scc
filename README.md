# Scheduling Constraints for L4Re

This repository contains the artifact accompanying the RTAS 2026 paper [**Scheduling Constraints: A Universal OS Mechanism for Managing Shared Resources**](https://l4re.org/research.html).

## Structure

The artifact is split into two parts for the L4Re Microkernel and Runtime Environment:

* `bin/`: Configuration logic for the L4Re snapshot
* `obj/`: Generated object code will be placed here
   * `fiasco/`: L4Re Microkernel build directories
   * `l4/`: L4Re build directories
* `src/`: Contains the source code
   * `fiasco`: L4Re Microkernel source
     (Scheduling Constraints are implemented in `fiasco/src/kern/sched_constraint.cpp`)
   * `l4`: L4Re source

## Requirements

To build and run the artifact, you will need:

- 64-bit Linux installation (we recommend Debian 11/Ubuntu 22.04 or later)
- ~2 GB free disk space

For the purpose of this artifact, the build/run tools are specified in a Nix flake.

Install the Nix package manager:

```bash
sh <(curl --proto '=https' --tlsv1.2 -L https://nixos.org/nix/install) --daemon
```

Clone the repository and enter the development environment:

```bash
git clone https://github.com/l4re/sched-constraints
cd sched-constraints
nix --extra-experimental-features 'nix-command flakes' develop
```

Configure the artifact.
For this showcase, we target QEMU's `aarch64` virtual platform.
The correct configuration options have already been set.
You can just confirm through all of them.
Optionally, you can explore the different platforms and boards that L4Re supports:

```bash
make setup
```

Build the artifact:

```bash
make -j $(nproc)
```

## Minimal Working Example

To run the built system under QEMU, issue:

```bash
make -C obj/l4/arm64/ qemu
```

This will display a dialog menu to let you choose an entry to boot.
For all entries, CPU time scheduling is done with scheduling constraints.
Choose `sc-twindow` or `sc-mutex` to explore higher-level mechanisms realized with SCs.

## L4Re Website

For more information, visit the official [L4Re website](https://l4re.org).
