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
- `build/basiccw` defaults to a Windows x86-64 COFF object file.

Both drivers accept the same options. The `basiccw` name is a convenience
wrapper around the same compiler binary; it selects `--target=win64 --emit=obj`
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

## Generate Object Files

Ask `basicc` to generate a relocatable Linux ELF object:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting.o --emit=obj -O2
```

Ask `basiccw` to generate a Windows x86-64 COFF object:

```sh
./build/basiccw examples/greeting.bas -o /tmp/greeting.obj -O2
```

The generated object contains the BASIC program and the BASIC runtime helpers.
It still leaves standard C library symbols such as `printf`, `malloc`, `scanf`,
and `getchar` unresolved for the final link step.

On a Debian/Ubuntu x86-64 system with static glibc and GCC runtime objects
installed, link the object into a static Linux executable with LLD:

```sh
ld.lld-20 -static -o /tmp/greeting-static \
  /usr/lib/x86_64-linux-gnu/crt1.o \
  /usr/lib/x86_64-linux-gnu/crti.o \
  /usr/lib/gcc/x86_64-linux-gnu/13/crtbeginT.o \
  /tmp/greeting.o \
  --start-group \
  /usr/lib/x86_64-linux-gnu/libc.a \
  /usr/lib/gcc/x86_64-linux-gnu/13/libgcc.a \
  /usr/lib/gcc/x86_64-linux-gnu/13/libgcc_eh.a \
  --end-group \
  /usr/lib/gcc/x86_64-linux-gnu/13/crtend.o \
  /usr/lib/x86_64-linux-gnu/crtn.o
```

## Generate Windows Object

Install the MinGW-w64 x86-64 cross toolchain on Ubuntu:

```sh
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
```

Then generate a Windows COFF object:

```sh
./build/basiccw examples/greeting.bas -o /tmp/greeting.obj -O2
```

Equivalent explicit form:

```sh
./build/basicc examples/greeting.bas -o /tmp/greeting.obj \
  --emit=obj --target=win64 -O2
```

The generated file should be a COFF x86-64 object:

```sh
file /tmp/greeting.obj
```

`build/basiccw` is a Linux-hosted cross-compiler driver. Link the generated
object with a Windows x86-64 C runtime to produce the final PE executable.

Using the MinGW-w64 POSIX runtime installed by Debian/Ubuntu packages, link the
COFF object into a Windows console executable with LLD:

```sh
ld.lld-20 -m i386pep -o /tmp/greeting-win.exe \
  /usr/x86_64-w64-mingw32/lib/crt2.o \
  /usr/lib/gcc/x86_64-w64-mingw32/13-posix/crtbegin.o \
  /tmp/greeting.obj \
  -L/usr/lib/gcc/x86_64-w64-mingw32/13-posix \
  -L/usr/x86_64-w64-mingw32/lib \
  --start-group \
  -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt \
  -lkernel32 -luser32 -ladvapi32 -lshell32 \
  --end-group \
  /usr/lib/gcc/x86_64-w64-mingw32/13-posix/crtend.o
```

The generated file should be a PE32+ x86-64 console executable:

```sh
file /tmp/greeting-win.exe
```

## Manual LLVM IR Linking

When using `--emit=llvm`, the output is a standalone LLVM IR file containing
the generated BASIC program and the BASIC runtime helpers. To run it, link it
with libc:

```sh
clang-20 /tmp/greeting.ll -o /tmp/greeting
```

## Compiler Options

```text
basicc input.bas -o output [--emit=llvm|obj|exe] [--target=linux64|win64] [-O0|-O1|-O2|-O3]
```

- `--emit=llvm` writes LLVM IR. This is the default.
- `--emit=obj` writes a relocatable object file directly through LLVM.
- `--emit=exe` writes a native executable by linking the generated IR with
  `clang-20`.
- `--target=linux64` targets the host x86-64 Linux toolchain.
- `--target=win64` targets `x86_64-w64-windows-gnu` and requires MinGW-w64.
- `-O0` disables LLVM optimization passes. This is the default.
- `-O1`, `-O2`, and `-O3` run LLVM's standard optimization pipelines before
  writing IR or linking an executable.

Driver defaults:

```text
basicc   input.bas -o output   # same as --emit=llvm --target=linux64 -O0
basiccw  input.bas -o output   # same as --emit=obj  --target=win64   -O0
```
