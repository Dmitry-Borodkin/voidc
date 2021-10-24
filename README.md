
# The Void Programming Language

*<p align='right'>"Programming language is not a templum, it's a workshop." (c)...</p>*

Void is an open-source, highly extensible programming language, minimalistic and low-level in its base.
It uses LLVM for code generation and can be seen as a thin (well, in some sense) shell around it.
Extendability of Void is practically limitless (any syntax/semantics),
restricted only by decidability and ~your money~ imagination.


#### Main architecture features:

  - Starting from minimalistic language "kernel", which is:
    - Compatible with "C" by key types and calling conventions;
    - Able to call (almost) arbitrary C-functions (from libc, LLVM, whatever);
    - Has some built-in "module importing" system (with pre-*compilation* support);
    - Has some predefined tools for further "bootstrapping".

  - Continuing with exposing compiler's internal API(s):
    - Extendable parser based on PEG-machinery;
    - Extendable AST;
    - Extendable compiler based on "visitor pattern";
    - Exposing essential parts of LLVM-C API (Core, TargetMachine, etc.).

  - Developing further language features:
    - Ability to create (C-compatible) functions;
    - Basic set of control flow constructions: "if then else", "block", "loop", "switch", "defer";
    - Comfortable "syntactic sugar" for grammar development;
    - C-like expressions (with some additional "twists");
    - Finally, some "normal" syntax for function's and variable's definitions/declarations.


For the moment (Oct 2021), language seems to be ready just for some early experiments...


**Note:** Void is *not* intended to be seen as a complete/finished/whatever programming language in usual sense...
In fact, it is merely a toolkit/workshop/boilerplate/etc. for development of programming languages *per se*,
and for development of *itself* particularly.


#### Nearest TODO(s):

  - Restructuring source tree (by language "levels");

  - Documenting everything written so far;

  - Preparing for next developments:

    - Standard library (strings, smart pointers, containers, etc.);
    - *Protocols*:
      - "Named fields/methods",
      - "Variants and matching",
      - "Coercions",
      - ...
    - "Bootstrapping"...

  - ...



## Installation

The `voidc` is "tested" (well, in some sense) under Linux and Windows (MSYS2/Mingw64) environments.

#### Dependencies:

  - LLVM/Clang (v.12 or later);
  - C++ library [immer](https://github.com/arximboldi/immer) (should be "visible" for include...).

#### Build:

Try scripts...

```bash
$ ./mk_config

$ ./mk_build
```


## Usage

Currently (Oct 2021) `voidc` works mainly from it's source directory.

You can try some of:

```bash
$ build/voidc hello.void

$ build/voidc hello_jit.void
```

You should see the sacramental "Hello, world!" messages with some LLVM's printouts...

BTW, first time it can take a while due to long (idk, 10-30 sec) compilation of huge `llvm-c/Core.void`.

After that, try:

```bash
$ build/voidc level-01/definitions_test.void
```

This "script" (with imports) uses almost all language features developed so far.
And yes, first-time compilation of it can take even longer...


## Contributing
Of course, contributions are **welcome**!
But, for the moment, I'm absolute newbie on GitHub, project/community maintaining etc...

So, for any friendly help (on these topics especially) I'll be greatly appreciated!

## License
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.txt)

