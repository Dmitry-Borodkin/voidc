## Level 0.0 - The "Starter".

Subsystems in some logical order...


### Parsing.

- Parsers - building blocks for Grammars: Parsers, Actions and Arguments.

  - [vpeg_parser.h](vpeg_parser.h) - Declarations of parsers, actions and arguments.
  - [vpeg_parser.cpp](vpeg_parser.cpp) - Implementation.
  - [vpeg_parser.void](vpeg_parser.void) - Void declarations.

- Grammar: container for parsers, actions and values.

  - [vpeg_grammar.h](vpeg_grammar.h) - Declaration of Grammar.
  - [vpeg_grammar.cpp](vpeg_grammar.cpp) - Implementation.
  - [vpeg_grammar.void](vpeg_grammar.void) - Void declarations.

- Context, i.e. parsing state: grammar, input stream, buffer, memo, etc.

  - [vpeg_context.h](vpeg_context.h) - Declaration of Context.
  - [vpeg_context.cpp](vpeg_context.cpp) - Implementation.
  - [vpeg_context.void](vpeg_context.void) - Void declarations/implementations.

- Initial grammar for the "Starter Language".

  - [vpeg_voidc.h](vpeg_voidc.h) - Declaration...
  - [vpeg_voidc.cpp](vpeg_voidc.cpp) - Implementation.


### "Compilation" visitors...

- AST hierarchy...

  - [voidc_ast.h](voidc_ast.h) - Declaration.
  - [voidc_ast.cpp](voidc_ast.cpp) - Implementation.
  - [voidc_ast.void](voidc_ast.void) - Void declarations.

- Visitor: container for "methods" etc.

  - [voidc_visitor.h](voidc_visitor.h) - Declaration.
  - [voidc_visitor.cpp](voidc_visitor.cpp) - Implementation.
  - [voidc_visitor.void](voidc_visitor.void) - Void declarations.

- Initial compiler for the "Starter Language".

  - [voidc_compiler.h](voidc_compiler.h) - Declaration...
  - [voidc_compiler.cpp](voidc_compiler.cpp) - Implementation.


### Compilation "environment"...

- Type system: types and Type Context.

  - [voidc_types.h](voidc_types.h) - Declaration.
  - [voidc_types.cpp](voidc_types.cpp) - Implementation.
  - [voidc_types.void](voidc_types.void) - Void declarations.

- Compilation contexts.

  - [voidc_target.h](voidc_target.h) - Declaration.
  - [voidc_target.cpp](voidc_target.cpp) - Implementation.
  - [voidc_target.void](voidc_target.void) - Void declarations.


### Some utility...

- Quarks. Kinda "atoms" in spirit of GTK's Glib...

  - [voidc_quark.h](voidc_quark.h) - Declaration of Quarks.
  - [voidc_quark.cpp](voidc_quark.cpp) - Implementation.
  - [voidc_quark.void](voidc_quark.void) - Void declarations.

- Utility...

  - [voidc_util.h](voidc_util.h) - Declaration.
  - [voidc_util.cpp](voidc_util.cpp) - Implementation.
  - [voidc_util.void](voidc_util.void) - Void declarations/implementation.

- Windoze...

  - [voidc_dllexport.h](voidc_dllexport.h) - Declaration.


### Main...

- Importing and "Main Loop"...

  - [voidc_main.cpp](voidc_main.cpp) - Implementation.


### Whole Level 0.0 import

- [\_\_import\_\_.void](__import__.void) - ...


---

### Some, if I may say so, tests...

- [example.void](example.void) - ...
- [import.void](import.void) - ...
- [locals.void](locals.void) - ...

- [import_test.void](import_test.void) - ...

- [vpeg_tests.void](vpeg_tests.void) - ...
- [yatst.void](yatst.void) - ...

- [target_test.void](target_test.void) - ...




