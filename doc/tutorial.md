
## The Void Programming Language Tutorial

**Disclaimer:** this tutorial assumes the reader has a rather good knowledge of C/C++,
understands basic notions of LLVM-C API, and in general "has an idea of how compilers work"...


#### The "Hello, world" example:

```C
{   v_import("printf.void"); }      // Import declaration of C's "printf"

{   printf("Hello, world\n"); }     // Just call it...
```

Void's source text is a sequence of so-called "units".
In example above you can see two of them: the first with `v_import`, and the second with `printf`.
Each unit (usually) is enclosed in (*magic*) curly braces and can be seen as a function's body
in the syntax similiar to "C".

Units are processed by the `voidc` compiler sequentially, one by one.
For each unit, the following operations are performed:

  1. Source text is *parsed* down to the enclosing `}`, resulting in the AST.
  2. The AST is *compiled* to the object file (in memory) which contains the "unit action" function.
  3. This "unit action" is *executed* in compiler's environment by LLVM's JIT.

In the "Hello, world" example above the first unit calls the compiler's intrinsic function `v_import`.
This function receives the name of the source file "printf.void" (from the Voidc's root directory)
and "imports" it into the "current scope". The "printf.void" file "gives out" only the declaration
of C's `printf` function. But that's enough for the second unit which calls this function with sacramental
greeting message as an argument.

**Essential Note:** it is very important to understand the consequences of this
*parse-compile-execute* architecture of the Viodc's "mainloop"...
It's the key to ~enormous~ extensibility of the Void as a language.
This structure intendend to be seen as the ~back~door to the compiler.
You just open the curly brace and enter the compiler as your workshop...
This is why these curly braces are *magic* :wink:.


#### The extended "Hello, world" example

Let's try to dig a bit deeper and see how it's possible to declare `printf`
"from scratch", without any *imports*...

```C
// Unit 1: declaration of the v_pointer_type
{
    typ0 = v_alloca(v_type_ptr, 2);     // Allocate "array" for two types
    typ1 = v_getelementptr(typ0, 1);    // Get pointer to the second element

    v_store(v_type_ptr, typ0);          // Argument 1: element type
    v_store(unsigned,   typ1);          // Argument 2: address space

    ft = v_function_type(v_type_ptr,    // Return type
                         typ0,          // Argument types
                         2,             // Number of arguments
                         false);        // VarArgs? (not)

    v_add_symbol("v_pointer_type",      // Symbol name  (function name)
                 ft,                    // Symbol type  (function type)
                 0);                    // Symbol value (none - just declaration)
}

// Unit 2: declaration of the printf
{
    typ = v_alloca(v_type_ptr);

    char_ptr = v_pointer_type(char, 0);

    v_store(char_ptr, typ);             // Format string

    ft = v_function_type(int, typ, 1, true);
    v_add_symbol("printf", ft, 0);
}

// Unit 3: call the printf
{
    printf("Hello, world\n");
}
```

First of all, it must be mentioned that when `voidc` starts to read the source file,
it begins with "minimal" internal environment. Only very few names are pre-declared.
Everything else, that we wish to use, must be declared "by hands" (or by imports)...

In the case of `printf` we have almost all necessary tools accessible, except one -
the function `v_pointer_type` (to build "pointer to char" type).
So, the first unit makes exactly this thing: it declares the `v_pointer_type` function...

As is often the case in programming, it is best to read it *sdrawkcab*...

The last statement of the unit calls the function `v_add_symbol` to "declare" the `v_pointer_type` function.
The term "symbol" here denotes a symbol in the terminology of (JIT's) linker.

The last but one statement builds the type of the `v_pointer_type` function.





######  To be continued...
