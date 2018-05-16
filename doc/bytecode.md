# Dst Bytecode Reference

### Dst alpha 0.0.0

This document outlines the Dst bytecode format, and core ideas in the runtime
that are closely related to the bytecode. It should enable the reader
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

Dst uses C functions to bridge to native code. A C function
(`DstCFunction *` in C) is a C function pointer that can be called like
a normal dst closure. From the perspective of the bytecode instruction set, there is no difference
in invoking a C function and invoking a normal dst function.

## Bytecode Format

Dst bytecode presents an interface to a virtual machine with a large number
of identical registers that can hold any Dst value (`Dst *` in C). Most instructions
have a destination register, and 1 or 2 source register. Registers are simply
named with positive integers.

Each instruction is a 32 bit integer, meaning that the instruction set is a constant
width instruction set like MIPS. The opcode of each instruction is the least significant
byte of the instruction. The highest bit of
this leading byte is reserved for debugging purpose, so there are 128 possible opcodes encodable
with this scheme. The current implementation uses about half of these possible opcodes.

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
* 1 arg - All payload bits correspond to a single value, usually a signed or unsigned integer.
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

Each instruction is also listed with a signature, which are the arguments the instruction
expects. There are a handful of instruction signatures, which combine the arity and type
of the instruction. The assembler does not
do any typechecking per closure, but does prevent jumping to invalid instructions and
failure to return or error.

### Notation

* The $ prefix indicates that a instruction parameter is acting as a virtual register (slot).
  If a parameter does not have the $ suffix in the description, it is acting as some kind
  of literal (usually an unsigned integer for indexes, and a signed integer for literal integers).

* Some operators in the description have the suffix 'i' or 'r'. These indicate
  that these operators correspond to integers or real numbers only, respectively. All
  bitwise operators and bit shifts only work with integers. 

* The `>>>` indicates unsigned right shift, as in Java. Because all integers in dst are
  signed, we differentiate the two kinds of right bit shift.

* The 'im' suffix in the instruction name is short for immediate. The 'i' suffix is short for integer,
  and the 'r' suffix is short for real.

### Reference Table

| Instruction | Signature                   | Description                       |
| ----------- | --------------------------- | --------------------------------- |
| `add`       | `(add dest lhs rhs)`        | $dest = $lhs + $rhs               | 
| `addi`      | `(addi dest lhs rhs)`       | $dest = $lhs +i $rhs              |
| `addim`     | `(addim dest lhs im)`       | $dest = $lhs +i im                |
| `addr`      | `(addr dest lhs rhs)`       | $dest = $lhs +r $rhs              |
| `band`      | `(band dest lhs rhs)`       | $dest = $lhs & $rhs               |
| `bnot`      | `(bnot dest operand)`       | $dest = ~$operand                 |
| `bor`       | `(bor dest lhs rhs)`        | $dest = $lhs | $rhs               |
| `bxor`      | `(bxor dest lhs rhs)`       | $dest = $lhs ^ $rhs               |
| `call`      | `(call dest callee)`        | $dest = call($callee)             |
| `clo`       | `(clo dest index)`          | $dest = closure(defs[$index])     |
| `cmp`       | `(cmp dest lhs rhs)`        | $dest = dst\_compare($lhs, $rhs)  |
| `debug`     | `(debug)`                   | Suspend current fiber             |
| `div`       | `(div dest lhs rhs)`        | $dest = $lhs / $rhs               |
| `divi`      | `(divi dest lhs rhs)`       | $dest = $lhs /i $rhs              |
| `divim`     | `(divim dest lhs im)`       | $dest = $lhs /i im                |
| `divr`      | `(divr dest lhs rhs)`       | $dest = $lhs /r $rhs              |
| `eq`        | `(eq dest lhs rhs)`         | $dest = $lhs == $rhs              |
| `eqi`       | `(eqi dest lhs rhs)`        | $dest = $lhs ==i $rhs             |
| `eqim`      | `(eqim dest lhs im)`        | $dest = $lhs ==i im               |
| `eqr`       | `(eqr dest lhs rhs)`        | $dest = $lhs ==r $rhs             |
| `err`       | `(err message)`             | Throw error $message.             |
| `get`       | `(get dest ds key)`         | $dest = $ds[$key]                 |
| `geti`      | `(geti dest ds index)`      | $dest = $ds[index]                |
| `gt`        | `(gt dest lhs rhs)`         | $dest = $lhs > $rhs               |
| `gti`       | `(gti dest lhs rhs)`        | $dest = $lhs \>i $rhs             |
| `gtim`      | `(gtim dest lhs im)`        | $dest = $lhs \>i im               |
| `gtr`       | `(gtr dest lhs rhs)`        | $dest = $lhs \>r $rhs             |
| `gter`      | `(gter dest lhs rhs)`       | $dest = $lhs >=r $rhs             |
| `jmp`       | `(jmp label)`               | pc = label, pc += offset          |
| `jmpif`     | `(jmpif cond label)`        | if $cond pc = label else pc++     |
| `jmpno`     | `(jmpno cond label)`        | if $cond pc++ else pc = label     |
| `ldc`       | `(ldc dest index)`          | $dest = constants[index]          |
| `ldf`       | `(ldf dest)`                | $dest = false                     |
| `ldi`       | `(ldi dest integer)`        | $dest = integer                   |
| `ldn`       | `(ldn dest)`                | $dest = nil                       |
| `lds`       | `(lds dest)`                | $dest = current closure (self)    |
| `ldt`       | `(ldt dest)`                | $dest = true                      |
| `ldu`       | `(ldu dest env index)`      | $dest = envs[env][index]          |
| `lt`        | `(lt dest lhs rhs)`         | $dest = $lhs < $rhs               |
| `lti`       | `(lti dest lhs rhs)`        | $dest = $lhs \<i $rhs             |
| `ltim`      | `(ltim dest lhs im)`        | $dest = $lhs \<i im               |
| `ltr`       | `(ltr dest lhs rhs)`        | $dest = $lhs \<r $rhs             |
| `lter`      | `(lter dest lhs rhs)`       | $dest = $lhs <=r $rhs             |
| `movf`      | `(movf src dest)`           | $dest = $src                      |
| `movn`      | `(movn dest src)`           | $dest = $src                      |
| `mul`       | `(mul dest lhs rhs)`        | $dest = $lhs * $rhs               |
| `muli`      | `(muli dest lhs rhs)`       | $dest = $lhs \*i $rhs             |
| `mulim`     | `(mulim dest lhs im)`       | $dest = $lhs \*i im               |
| `mulr`      | `(mulr dest lhs rhs)`       | $dest = $lhs \*r $rhs             |
| `noop`      | `(noop)`                    | Does nothing.                     |
| `push`      | `(push val)`                | Push $val as arg                  |
| `push2`     | `(push2 val1 val3)`         | Push $val1, $val2 as args         |
| `push3`     | `(push3 val1 val2 val3)`    | Push $val1, $val2, $val3, as args |
| `pusha`     | `(pusha array)`             | Push values in $array as args     |
| `put`       | `(put ds key val)`          | $ds[$key] = $val                  |
| `puti`      | `(puti ds index val)`       | $ds[index] = $val                 |
| `res`       | `(res dest fiber val)`      | $dest = resume $fiber with $val   |
| `ret`       | `(ret val)`                 | Return $val                       |
| `retn`      | `(retn)`                    | Return nil                        |
| `setu`      | `(setu env index val)`      | envs[env][index] = $val           |
| `sl`        | `(sl dest lhs rhs)`         | $dest = $lhs << $rhs              |
| `slim`      | `(slim dest lhs shamt)`     | $dest = $lhs << shamt             |
| `sr`        | `(sr dest lhs rhs)`         | $dest = $lhs >> $rhs              |
| `srim`      | `(srim dest lhs shamt)`     | $dest = $lhs >> shamt             |
| `sru`       | `(sru dest lhs rhs)`        | $dest = $lhs >>> $rhs             |
| `sruim`     | `(sruim dest lhs shamt)`    | $dest = $lhs >>> shamt            |
| `sub`       | `(sub dest lhs rhs)`        | $dest = $lhs - $rhs               |
| `tcall`     | `(tcall callee)`            | Return call($callee)              |
| `tchck`     | `(tcheck slot types)`       | Assert $slot does matches types   |
| `yield`     | `(yield dest value)`        | $dest = yield $value to parent    |

