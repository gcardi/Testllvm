# TestLLVM

Minimal LLVM experiment: a small BASIC-style compiler that emits LLVM IR.

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

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 \
  -DCMAKE_CXX_COMPILER=clang++-20 \
  -DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm

cmake --build build
```

The build creates two compiler driver names:

- `build/basicc` defaults to Linux x86-64/native output.
- `build/basiccw` also runs on Linux, but defaults to Windows PE x86-64
  output.

## Generate and Run

Generate LLVM IR:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting.ll
```

Generate optimized LLVM IR:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting.ll --emit=llvm -O2
```

Compile the generated IR with the tiny runtime:

```sh
clang-20 /tmp/greeting.ll runtime/basic_runtime.c -o /tmp/greeting
```

Or ask `basicc` to generate a native executable directly:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting --emit=exe -O2
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

## Windows PE64 Output

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
