# TestLLVM

Minimal LLVM experiment: a small BASIC-style compiler that emits LLVM IR,
Linux ELF executables, or Windows PE64 console executables.

The language uses labels instead of line numbers and supports:

- `LET name = expression`
- `INPUT name`
- `PRINT expression`
- `IF expression <op> expression GOTO LABEL`
- `GOTO LABEL`
- `END`

Variables ending in `$` are strings. Other variables are 32-bit integers.
String expressions support concatenation with `+`; integer expressions support
`+`, `-`, `*`, and `/`.

Comments may be written with `REM` at the start of a line, or with `'` after
code. Labels are written as `NAME:` on their own line.

## Example

```basic
MAIN:
  PRINT "WHAT IS YOUR NAME?"
  INPUT NAME$
  LET GREETING$ = "HELLO, " + NAME$
  PRINT GREETING$
  INPUT N
  LET I = 0

LOOP:
  PRINT I
  LET I = I + 1
  IF I < N GOTO LOOP

DONE:
  PRINT "DONE"
  END
```

## Build

Requirements:

- CMake 3.20 or newer
- Ninja
- LLVM/Clang 20 or newer
- MinGW-w64 x86-64 tools, only when generating Windows PE64 executables

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 \
  -DCMAKE_CXX_COMPILER=clang++-20 \
  -DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm

cmake --build build
```

The build creates two Linux-hosted compiler driver names:

- `build/basicc` defaults to LLVM IR for the host/Linux target.
- `build/basiccw` defaults to a Windows PE64 x86-64 executable.

Both drivers accept the same options. The `basiccw` name is a convenience
wrapper around the same compiler binary; it selects `--target=win64 --emit=exe`
by default.

## Generate LLVM IR

Generate LLVM IR:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting.ll
```

Generate optimized LLVM IR:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting.ll --emit=llvm -O2
```

You can also generate Windows-targeted LLVM IR:

```sh
./build/basiccw examples/greeting.bas -o /tmp/greeting-win.ll --emit=llvm -O2
```

## Generate Linux ELF

Ask `basicc` to generate a native Linux ELF executable directly:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting --emit=exe -O2
```

Equivalent explicit form:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting \
  --emit=exe --target=linux64 -O2
```

The generated file should be an ELF x86-64 executable:

```sh
file /tmp/greeting
```

Run it:

```sh
printf 'Ada\n3\n' | /tmp/greeting
```

Expected output:

```text
WHAT IS YOUR NAME?
HELLO, Ada
0
1
2
DONE
```

## Generate Windows PE64

Install the MinGW-w64 x86-64 cross toolchain on Ubuntu:

```sh
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
```

Then generate a Windows console executable:

```sh
./build/basiccw examples/greeting.bas -o /tmp/greeting-win.exe -O2
```

Equivalent explicit form:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting-win.exe \
  --emit=exe --target=win64 -O2
```

The generated file should be a PE32+ x86-64 executable:

```sh
file /tmp/greeting-win.exe
```

`build/basiccw` is a Linux-hosted cross-compiler driver. Copy the generated
program, such as `/tmp/greeting-win.exe`, to Windows, not `build/basiccw`.

## Manual LLVM IR Linking

When using `--emit=llvm`, the output is a standalone LLVM IR file for the
generated BASIC program. To run it, link it with the tiny runtime:

```sh
clang-20 /tmp/greeting.ll runtime/basic_runtime.c -o /tmp/greeting
```

## Compiler Options

```text
basicc input.bas -o output [--emit=llvm|exe] [--target=linux64|win64] [-O0|-O1|-O2|-O3]
```

- `--emit=llvm` writes LLVM IR. This is the default.
- `--emit=exe` writes a native executable by linking the generated IR with
  `runtime/basic_runtime.c` through `clang-20`.
- `--target=linux64` targets the host x86-64 Linux toolchain.
- `--target=win64` targets `x86_64-w64-windows-gnu` and requires MinGW-w64.
- `-O0` disables LLVM optimization passes. This is the default.
- `-O1`, `-O2`, and `-O3` run LLVM's standard optimization pipelines before
  writing IR or linking an executable.

Driver defaults:

```text
basicc   input.bas -o output   # same as --emit=llvm --target=linux64 -O0
basiccw  input.bas -o output   # same as --emit=exe  --target=win64   -O0
```
