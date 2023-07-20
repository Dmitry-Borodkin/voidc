
# The Void Programming Language

*<p align='right'>"Programming language is not a templum, it's a workshop." (c)...</p>*

Void is an open-source, highly extensible programming language, minimalistic and low-level in its base.
It uses LLVM for code generation and can be seen as a thin (well, in some sense) shell around it.
Extensibility of Void is practically limitless (any syntax/semantics),
restricted only by decidability and ~your money~ imagination.


#### Hello, world:

```C
{   v_import("printf.void"); }      // Import declaration of C's "printf"

{   printf("Hello, world\n"); }     // Just call it...
```

See more details in [tutorial](doc/tutorial.md).
(Spoiler: it's all about those *magic curly braces*... :wink:)


#### Main architecture features:

  - Starting from minimalistic language "kernel", which is:
    - Compatible with "C" by key types and calling conventions;
    - Able to call (almost) arbitrary C-functions (from libc, LLVM, whatever);
    - Has some built-in "module importing" system (with pre-*compilation* support);
    - Has some predefined tools for further "bootstrapping".

  - Continuing with exposing compiler's internal API(s):
    - Extensible parser based on PEG-machinery;
    - Extensible AST;
    - Extensible compiler based on "visitor pattern";
    - Exposing essential parts of LLVM-C API (Core, TargetMachine, etc.).

  - Developing "basic" language features:
    - Ability to create (C-compatible) functions;
    - Basic set of control flow constructions: "if then else", "block", "loop", "switch", "defer";
    - Comfortable "syntactic sugar" for grammar development;
    - C-like expressions (with some additional "twists");
    - Finally, some "normal" syntax for function's and variable's definitions/declarations.

  - Extending the language:
    - Comprehensive numeric literals;
    - Sophisticated control flow constuctions: "while", "for", extended "if then else" and "switch";
    - Aggregate "initializations";
    - "Simple" overloading of operators;
    - "Projections" (functions with syntax `a.something`);
    - Simple structures with named fields;
    - "Mangling" (fancy function names to help overloading/projections);
    - Function inlining;
    - Unions (like in C).

  - Kinda, objects:
    - ...

  - ...


For the moment (Jul 2023), language seems to be ready just for some early experiments...


**Note:** Void is *not* intended to be seen as a complete/finished/whatever programming language in usual sense...
In fact, it is merely a toolkit/workshop/boilerplate/etc. for development of programming languages *per se*,
and for development of *itself* particularly.


#### Nearest TODO(s):

  - Documenting everything written so far;

  - Preparing for next developments:

    - Standard library (strings, smart pointers, containers, etc.);
    - *Protocols*:
      - "Named fields/methods",
      - "Variants and matching",
      - "Coercions",
      - "Namespaces",
      - ...
    - "Bootstrapping"...

  - ...



## Installation

The `voidc` is "tested" (well, in some sense) under Linux and Windows (MSYS2/Mingw64) environments.

#### Dependencies:

  - LLVM/Clang (v.15 or later);
  - C++ library [immer](https://github.com/arximboldi/immer) (should be "visible" for include...).

#### Build:

Try scripts (CMake needed)...

```bash
$ ./mk_config

$ ./mk_build
```


## Usage

Currently (Jul 2023) `voidc` works mainly from it's source directory.

You can try some of:

```bash
$ build/voidc hello.void

$ build/voidc hello_jit.void
```

You should see the sacramental "Hello, world!" messages with some LLVM's printouts...

BTW, first time it can take a while due to long (idk, 10-30 sec) compilation of huge `llvm-c/Core.void`.

After that, try:

```bash
$ build/voidc level-03/heap_objects_test.void
```

This "script" (with imports) uses almost all language features developed so far.
And yes, first-time compilation of it can take even longer...


## Contributing
Of course, contributions are **welcome**!
But, for the moment, I'm absolute newbie on GitHub, project/community maintaining etc...

So, for any friendly help (on these topics especially) I'll be greatly appreciated!

## License
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.txt)

