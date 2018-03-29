# Dst Bytecode Interpreter

This document outlines the Dst bytecode format, and core ideas in the runtime.
the are closely related to the bytecode. It should enable the reader
to write dst assembly code and hopefully understand the dst internals better.
It will also talk about the C abstractions used to implement some of these ideas.
Some experience with basic computer organization is helpful for understanding
the model of computation.

## The Stack = The Fiber

A Dst Fiber is the type used to represent multiple concurrent processes
in dst. It is basically a wrapper around the idea of a stack. The stack is
divided into a number of stack frames (`DstStackFrame *` in C), each of which
contains information such as the function that created the stack frame, 
the program counter for the stack frame, a pointer to the previous frame,
and the size of the frame. Each stack frame also is paired with a number
registers.

```
X: Slot

X
X - Stack Top, for next function call.
-----
Frame next
-----
X
X
X
X
X
X
X - Stack 0
-----
Frame 0
-----
X
X
X - Stack -1
-----
Frame -1
-----
X
X
X
X
X - Stack -2
-----
Frame -2
-----
...
...
...
----- 
Bottom of stack
```

Fibers also have an incomplete stack frame for the next function call on top
of their stacks. Making a function call involves pushing arguments to this
temporary stack, and then invoking either the CALL or TCALL instructions.
Arguments for the next function call are pushed via the PUSH, PUSH2, PUSH3, and
PUSHA instructions. The stack of a fiber will grow as large as needed, so
recursive algorithms can be used without fear of stack overflow.

The slots in the stack are exposed as virtual registers to instructions. They
can hold any Dst value.

## Closures

All functions in dst are closures; they combine some bytecode instructions
with 0 or more environments. In the C source, a closure (hereby the same as
a function) is represented by the type `DstFunc *`. The bytecode instruction
part of the function is represented by `DstFuncDef *`, and a function environment
is represented with `DstFuncEnv *`.

The function definition part of a function (the 'bytecode' part, `DstFuncDef *`),
we also store various metadata about the function which is useful for debugging,
as well as constants referenced by the function.

## C Functions

Dst uses c functions to bridge to native code. A C function
(`DstCFunction *` in C) is a C function pointer that can be called like
a normal dst closure. From the perspective of the bytecode instruction set, there is no difference
in invoking a c function and invoking a normal dst function.

## Bytecode Format

Dst bytecode presents an interface to virtual machine with a large number
of identical registers that can hold any Dst value (`Dst *` in C). Most instructions
have a destination register, and 1 or 2 source register. Registers are simply
named with positive integers.

Each instruction is a 32 bit integer, meaning that the instruction set is a constant
width instruction set like MIPS. The opcode of each instruction is the least significant
byte of the instruction. This means there are 256 possible opcodes, but half of those
are reserved, so 128 possible opcodes. The current implementation uses about half of these.

```
X - Payload bits
O - Opcode bits

   4    3    2    1
+----+----+----+----+
| XX | XX | XX | OO |
+----+----+----+----+
```

8 bits for the opcode leaves 24 bits for the payload, which may or may not be utilized.
There are a few instruction variants that divide these payload bits.

* 0 arg - Used for noops, returning nil, or other instructions that take no
  arguments. The payload is essentially ignored.
* 1 arg - All payload bits correspond to a single value, usually a signed or a signed integer/
  Used for instructions of 1 argument, like returning a value, yielding a value to the parent fiber,
  or doing a jump.
* 2 arg - Payload is split into byte 2 and bytes 3 and 4.
  The first argument is the 8 bit value from byte 2, and the second argument is the 16 bit value
  from bytes 3 and 4 (`instruction >> 16`). Used for instructions of two arguments, like move, normal
  function calls, conditionals, etc.
* 3 arg - Bytes 2, 3, and 4 each correspond to an 8 bit argument.
  Used for arithmetic operations.

These instruction variants can be further refined based on the semantics of the arguments.
Some instructions may treat an argument as a slot index, while other instructions
will treat the argument as a signed integer literal, and index for a constant, an index
for an environment, or an unsigned integer.

## Instruction Reference

A listing of all opcode values can be found in src/include/dst/dstopcodes.h. The dst assembly
short names can be found src/assembler/asm.c. In this document, we will refer to the instructions
by their short names as presented to the assembler rather than their numerical values.

* `add`:
* `addi`:
* `addim`:
* `addr`:
* `band`:
* `bnot`:
* `bor`:
* `bxor`:
* `call`:
* `clo`:
* `cmp`:
* `debug`:
* `div`:
* `divi`:
* `divim`:
* `divr`:
* `eq`:
* `err`:
* `get`:
* `geti`:
* `gt`:
* `jmp`:
* `jmpi`:
* `jmpn`:
* `ldc`:
* `ldf`:
* `ldi`:
* `ldn`:
* `lds`:
* `ldt`:
* `ldu`:
* `lt`:
* `movf`:
* `movn`:
* `mul`:
* `muli`:
* `mulim`:
* `mulr`:
* `noop`:
* `push`:
* `push2`:
* `push3`:
* `pusha`:
* `put`:
* `puti`:
* `res`:
* `ret`:
* `retn`:
* `setu`:
* `sl`:
* `slim`:
* `sr`:
* `srim`:
* `sru`:
* `sruim`:
* `sub`:
* `tcall`:
* `tchck`:
* `yield`:

