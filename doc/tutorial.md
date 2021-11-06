
## The Void Programming Language Tutorial

**Disclaimer:** this tutorial assumes the reader has a rather good knowledge of C/C++,
understands basic notions of LLVM-C API, and in general "has an idea of how compilers work"...


#### The "Hello, world" example:

```C
{   v_import("printf.void"); }      // Import declaration of C's "printf"

{   printf("Hello, world\n"); }     // Just call it...
```

Void's source text is a sequence of so-called "units".
In example above we can see two units: first with `v_import`, and second with `printf`.
Each unit (usually) is enclosed in (*magic* :wink:) curly braces and can be seen as a function's body
in the syntax similiar to "C".

Units are processed by the `voidc` compiler sequentially, one by one.
For each unit, the following operations are performed:

  1. Source text is parsed down to the enclosing `}`, resulting in the AST.
  2. The AST is compiled to the object file (in memory) which contains the "unit action" function.
  3. This "unit action" is *executed* in compiler's environment by LLVM's JIT.

In the "Hello, world" example above the first unit calls the compiler's intrinsic function `v_import`.
This function receives the name of the source file "printf.void" (from the Voidc's root directory)
and "imports" it into the "current scope". The "printf.void" file "gives out" only the declaration
of C's `printf` function. But that's enough for the second unit which calls this function with sacramental
greeting message as an argument.














######  To be continued...
