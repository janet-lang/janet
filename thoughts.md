# Thoughts

A collection of thoughts and todo tasks for the project.

- Allow entrances into the VM to track the size of the stack when they entered, and return
  when the stack is less that. This would make calling dst functions from C feasible (
  The programmer would still have to ensure no GC violations).

  Instead, we can just keep allocating new Fibers when we call a dst function from C. A pool
  of fibers would mostly mitigate the overhead of allocation. (going with this).

  We can now call into dst from C without suspending the entire garbage collector. A separate
  function does exactly that.

- Make unknown instruction in vm trap and put current fiber in a new state, 'debug'.
  This could allow implementation of a debugger. Since opcodes are encoded in one byte,
  we can use the most significant bit (0x80) to set breakpoints in code, assuming all valid
  opcodes are in the range [0, 127]. The debugger could simply set the MSB of the opcode for each
  instruction that was marked. This would allow debugging with 0 overhead.
 
  We could also add a debugger instruction, much like JavaScripts debugger; statement very easily.

  Lastly, to make continuation after a breakpoint easier, stopping on the first instruction
  could be optional. This could be as simple as selecting the first 7 bits of the instructions
  instead of the usual 8 for the very instruction executed after entering the vm loop.

  What exactly should happen on a trapped instruction is another issue. It would be preferable
  for the runtime to be able to handle a trap in dst, but allow nested fibers to not capture
  debugging signals unless needed.

  Fiber's currently propagate all states to their direct parent, but perhaps each fiber
  could have a mask for different signals - error, debug, return. So a single fiber could
  say capture returns, error, but not debug. Possibly like try - catch in other languages, where
  we only catch certain kinds of errors. The default fiber would be to only mask debug, so a single fiber
  could wrap an entire running application for debugging.

- Remove the concept of 'Ast node'. While providing fine-grained source mapping is
  is reasonably useful, it complicates the implementation of macros and other source
  transforming operations. Instead, we can key collection types (which have the unique
  pointer property) to source mapping information in a external data structure, which is specifically
  for source mapping. This data structure would be a hash table that used pointer equality
  instead of value equality for entries.

  The compiler would then need to keep track of 'most specific collection' node when compiling,
  as the value itself would not always be a collection and contain source mapping information.

  As a result, we could remove special forms like 'ast-quote', as well as ast-unwrapping logic
  which potentially duplicates a fair amount of data. Macros would be easier to write without
  needing to either unwrap ast values or sacrifice all source mapping.

- Keep track of source file information in the compiler. The compiler could simply accept
  and extra argument, sourcefile, which woud append the appropriate metadata to all function
  definitions generated with this one form.

- Serialization and deserialization of all datatypes. This would allow loading of bytecode
  without needing the compiler present. However, loading C functions is currently problamatic.
  C functions could perhaps be wrapped in data structures that contain some meta information
  about them, say home module and types. This could also allow some automated type checking for
  C functions rather than writing it manually. Some slight overhead could perhaps be compensated
  for by adding optional ommission of typechecking later for C functions if it can be statically
  shown the types are sound.

- Better support for custom user datatypes. Tables and structs do work well for creating
  custom 'objects' and records, but lack ability to differentiate between object style
  values and data structure style values. For example, simply adding special keys as fields
  would make plain a table or struct possibly become object-like if the write keys are added.

  A Lua like solution would be a metatables. It would perhaps make sense to only allow
  metatables on tables, as object like behavior seems to makes most sense on mutable data (?).
  For example, metatables on a struct could allow non-pure behavior unless they were extremely
  limited, or purity was enforced somehow. This could break expectations of a struct to behave
  as immutable data.

  Also, it might make sense that the metatable of a value would actually be a struct, so
  a metastruct. Implementations of Lua (LuaJIT) do not allow (or do not acknowledge) 
  changing certain values of a metatables after it is set, such as __gc, for performance
  reasons.

- Actually make a debugger. While the VM changes to enable debugging are relatively
  simple, make  a useful debugger would be another project. At first, simply inspection
  of the generated bytecode assembly would be a good start. Single stepping, continuation, 
  and inspecting the stack and current fiber would be a start.

  Preferably this would be programmed in dst. The needed functionality could be exposed
  in the fiber API.

  Later, with information from the compiler, we could inspect variables and upvalues
  by symbol. The current compiler does not do full SSA optimization, so named values
  are always accessible in the stack when in scope.

- Create a pool for fibers. Whlie the general purpose allocator and GC can be made more efficient,
  Fiber's can be well pooled because the allocated stack is large and can be reused. The stack
  size parameter on dst_fiber could simply become the minimum memory allocated for the stack. (Do
  a linear search throught the pool).

- Implement multi-methods. 
