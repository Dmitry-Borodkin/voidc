//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_voidc.h"
#include "vpeg_context.h"

#include "voidc_ast.h"


//---------------------------------------------------------------------
namespace vpeg
{

//---------------------------------------------------------------------
extern "C"
{

static const ast_stmt_list_sptr_t stmt_list_nil = std::make_shared<const ast_stmt_list_t>();
static const ast_expr_list_sptr_t expr_list_nil = std::make_shared<const ast_expr_list_t>();

static void
mk_unit(std::any *ret, const std::any *args, size_t)
{
    auto p = std::any_cast<ast_stmt_list_sptr_t>(args+0);

    if (!p) p = &stmt_list_nil;

    auto pos = std::any_cast<size_t>(args[1]);

    size_t line;
    size_t column;

    context_t::current_ctx->get_line_column(pos, line, column);

    ast_unit_sptr_t ptr = std::make_shared<const ast_unit_t>(*p, line+1, column+1);

    *ret = ptr;
}

static void
mk_stmt_list(std::any *ret, const std::any *args, size_t)
{
    auto plst = std::any_cast<ast_stmt_list_sptr_t>(args+0);

    if (!plst)  plst = &stmt_list_nil;

    auto item = std::any_cast<ast_stmt_sptr_t>(args+1);

    if (item)   *ret = std::make_shared<const ast_stmt_list_t>(*plst, *item);
    else        *ret = *plst;
}

static void
mk_stmt(std::any *ret, const std::any *args, size_t)
{
    std::string s = "";

    if (auto p = std::any_cast<const std::string>(args+0))  { s = *p; }

    ast_expr_sptr_t e;

    if (auto p = std::any_cast<ast_expr_sptr_t>(args+1))    { e = *p; }

    ast_stmt_sptr_t ptr = std::make_shared<const ast_stmt_t>(s, e);

    *ret = ptr;
}

static void
mk_expr_call(std::any *ret, const std::any *args, size_t)
{
    auto f = std::any_cast<ast_expr_sptr_t>(args[0]);

    auto a = std::any_cast<ast_expr_list_sptr_t>(args[1]);

    ast_expr_sptr_t ptr = std::make_shared<const ast_expr_call_t>(f, a);

    *ret = ptr;
}

static void
mk_expr_list(std::any *ret, const std::any *args, size_t)
{
    auto plst = std::any_cast<ast_expr_list_sptr_t>(args+0);

    if (!plst) plst = &expr_list_nil;

    auto item = std::any_cast<ast_expr_sptr_t>(args+1);

    if (item)   *ret = std::make_shared<const ast_expr_list_t>(*plst, *item);
    else        *ret = *plst;
}

static void
mk_expr_identifier(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<const std::string>(args[0]);

    ast_expr_sptr_t ptr = std::make_shared<const ast_expr_identifier_t>(n);

    *ret = ptr;
}

static void
mk_expr_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    ast_expr_sptr_t ptr = std::make_shared<const ast_expr_integer_t>(n);

    *ret = ptr;
}

static void
mk_expr_string(std::any *ret, const std::any *args, size_t)
{
    auto s = std::any_cast<const std::string>(args[0]);

    ast_expr_sptr_t ptr = std::make_shared<const ast_expr_string_t>(s);

    *ret = ptr;
}

static void
mk_expr_char(std::any *ret, const std::any *args, size_t)
{
//  auto c = std::any_cast<char32_t>(args[0]);
    auto c = (char32_t)std::any_cast<uint32_t>(args[0]);

    ast_expr_sptr_t ptr = std::make_shared<const ast_expr_char_t>(c);

    *ret = ptr;
}

static void
mk_pos_integer(std::any *ret, const std::any *args, size_t)
{
    uintptr_t n = std::any_cast<uintptr_t>(args[0]);

    if (n > (~uintptr_t(0) >> 1)) return;               //- Fail!

    *ret = intptr_t(n);
}

static void
mk_neg_integer(std::any *ret, const std::any *args, size_t)
{
    uintptr_t n = std::any_cast<uintptr_t>(args[0]);

    if (n > ~(~uintptr_t(0) >> 1))  return;             //- Fail!

    *ret = intptr_t(~n + 1);    //- "Safe" negate...
}

static void
mk_dec_integer(std::any *ret, const std::any *args, size_t)
{
    uintptr_t n = std::any_cast<uintptr_t>(args[0]);

    if (n > UINTPTR_MAX/10) return;                             //- Fail!

    uintptr_t d = uintptr_t(char32_t(std::any_cast<uint32_t>(args[1])) - U'0');

    if (n == UINTPTR_MAX/10  &&  d > UINTPTR_MAX%10) return;    //- Fail!

    if (n)  d += 10*n;

    *ret = d;
}

static void
mk_dec_numdigit(std::any *ret, const std::any *args, size_t)
{
    uintptr_t d = uintptr_t(char32_t(std::any_cast<uint32_t>(args[0])) - U'0');

    *ret = d;
}

static void
mk_string(std::any *ret, const std::any *args, size_t)
{
    auto s = std::any_cast<const std::string>(args[0]);

    auto c = char32_t(std::any_cast<uint32_t>(args[1]));

    char d[5];

    int r;

    if (c <= 0x7FF)
    {
        if (c <= 0x7F)  r = 0;
        else            r = 1;
    }
    else
    {
        if (c <= 0xFFFF)  r = 2;
        else              r = 3;
    }

    if (r == 0)
    {
        d[0] = c & 0x7F;
    }
    else
    {
        for (int j = r; j > 0; --j)
        {
            d[j] = 0x80 | (c & 0x3F);

            c >>= 6;
        }

        d[0] = (0x1E << (6-r)) | (c & (0x3F >> r));
    }

    d[r+1] = 0;

    *ret = s + d;
}

static void
mk_EOF(std::any *ret, const std::any *args, size_t)
{
    //- Just a placeholder...

    ast_unit_sptr_t ptr = nullptr;

    *ret = ptr;
}


//---------------------------------------------------------------------
}   //- extern "C"

//---------------------------------------------------------------------
}   //- namespace vpeg


//---------------------------------------------------------------------
vpeg::grammar_t make_voidc_grammar(void)
{
    using namespace vpeg;

    grammar_t gr;

#define DEF(name) gr = gr.set_action(#name, name);

    DEF(mk_unit)
    DEF(mk_stmt_list)
    DEF(mk_stmt)
    DEF(mk_expr_call)
    DEF(mk_expr_list)
    DEF(mk_expr_identifier)
    DEF(mk_expr_integer)
    DEF(mk_expr_string)
    DEF(mk_expr_char)
    DEF(mk_pos_integer)
    DEF(mk_neg_integer)
    DEF(mk_dec_integer)
    DEF(mk_dec_numdigit)
    DEF(mk_string)
    DEF(mk_EOF)

#undef DEF

    //-------------------------------------------------------------
#define DEF(name) auto ip_##name = mk_identifier_parser(#name);

    DEF(stmt_list)
    DEF(stmt_list_lr)
    DEF(stmt)
    DEF(expr)
    DEF(expr_list)
    DEF(expr_list_lr)
    DEF(prim)

    DEF(identifier)
    DEF(ident_start)
    DEF(ident_cont)
    DEF(integer)
    DEF(dec_natural)
    DEF(dec_positive)
    DEF(dec_digit)
    DEF(string)
    DEF(char)
    DEF(str_body)
    DEF(str_char)
    DEF(character)
    DEF(esc_sequence)

    DEF(_)
    DEF(comment)
    DEF(space)
    DEF(EOL)

#undef DEF

    //-------------------------------------------------------------
    //- unit <- _ <'{'> _ l:stmt_list _ '}'     { mk_unit(l, $1s) }
    //-       / _ !.                            { mk_EOF() }

    gr = gr.set_parser("unit",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            ip__,
            mk_catch_string_parser(mk_character_parser('{')),
            ip__,
            mk_catch_variable_parser("l", ip_stmt_list),
            ip__,
            mk_character_parser('}'),

            mk_action_parser(
                mk_call_action("mk_unit",
                {
                    mk_identifier_argument("l"),
                    mk_backref_argument(1, backref_argument_t::bk_start)
                })
            )
        }),
        mk_sequence_parser(
        {
            ip__,
            mk_not_parser(mk_dot_parser()),

            mk_action_parser(
                mk_call_action("mk_EOF", {})
            )
        })
    }));

    //-------------------------------------------------------------
    //- stmt_list <- stmt_list_lr
    //-            /                    { mk_stmt_list(0, 0) }

    gr = gr.set_parser("stmt_list",
    mk_choice_parser(
    {
        ip_stmt_list_lr,

        mk_action_parser(
            mk_call_action("mk_stmt_list",
            {
                mk_integer_argument(0),
                mk_integer_argument(0)
            })
        )
    }));

    //-------------------------------------------------------------
    //- stmt_list_lr <- l:stmt_list_lr _ s:stmt     { mk_stmt_list(l, s) }
    //-               / s:stmt                      { mk_stmt_list(0, s) }

    gr = gr.set_parser("stmt_list_lr",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_stmt_list_lr),
            ip__,
            mk_catch_variable_parser("s", ip_stmt),

            mk_action_parser(
                mk_call_action("mk_stmt_list",
                {
                    mk_identifier_argument("l"),
                    mk_identifier_argument("s")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("s", ip_stmt),

            mk_action_parser(
                mk_call_action("mk_stmt_list",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("s")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- stmt <- v:identifier _ '=' _ e:expr _ ';'   { mk_stmt(v, e) }
    //-       / e:expr _ ';'                        { mk_stmt(0, e) }
    //-       / ';'                                 { mk_stmt(0, 0) }

    gr = gr.set_parser("stmt",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("v", ip_identifier),
            ip__,
            mk_character_parser('='),
            ip__,
            mk_catch_variable_parser("e", ip_expr),
            ip__,
            mk_character_parser(';'),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_identifier_argument("v"),
                    mk_identifier_argument("e")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("e", ip_expr),
            ip__,
            mk_character_parser(';'),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("e")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_character_parser(';'),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_integer_argument(0),
                    mk_integer_argument(0)
                })
            )
        })
    }));

    //-------------------------------------------------------------
    //- expr <- f:expr _ '(' _ a:expr_list _ ')'   { mk_expr_call(f, a) }
    //-       / prim

    gr = gr.set_parser("expr",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("f", ip_expr),
            ip__,
            mk_character_parser('('),
            ip__,
            mk_catch_variable_parser("a", ip_expr_list),
            ip__,
            mk_character_parser(')'),

            mk_action_parser(
                mk_call_action("mk_expr_call",
                {
                    mk_identifier_argument("f"),
                    mk_identifier_argument("a")
                })
            )
        }),
        ip_prim
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- expr_list <- expr_list_lr
    //-            /                 { mk_expr_list(0, 0) }

    gr = gr.set_parser("expr_list",
    mk_choice_parser(
    {
        ip_expr_list_lr,

        mk_action_parser(
            mk_call_action("mk_expr_list",
            {
                mk_integer_argument(0),
                mk_integer_argument(0)
            })
        )
    }));

    //-------------------------------------------------------------
    //- expr_list_lr <- l:expr_list_lr _ ',' _ e:expr  { mk_expr_list(l, e) }
    //-               / e:expr                         { mk_expr_list(0, e) }

    gr = gr.set_parser("expr_list_lr",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_expr_list_lr),
            ip__,
            mk_character_parser(','),
            ip__,
            mk_catch_variable_parser("e", ip_expr),

            mk_action_parser(
                mk_call_action("mk_expr_list",
                {
                    mk_identifier_argument("l"),
                    mk_identifier_argument("e")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("e", ip_expr),

            mk_action_parser(
                mk_call_action("mk_expr_list",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("e")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- prim <- i:identifier     { mk_expr_identifier(i) }
    //-       / n:integer        { mk_expr_integer(n) }
    //-       / s:string         { mk_expr_string(s) }
    //-       / c:char           { mk_expr_char(c) }

    gr = gr.set_parser("prim",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("i", ip_identifier),

            mk_action_parser(
                mk_call_action("mk_expr_identifier",
                {
                    mk_identifier_argument("i"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_integer),

            mk_action_parser(
                mk_call_action("mk_expr_integer",
                {
                    mk_identifier_argument("n"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("s", ip_string),

            mk_action_parser(
                mk_call_action("mk_expr_string",
                {
                    mk_identifier_argument("s"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("c", ip_char),

            mk_action_parser(
                mk_call_action("mk_expr_char",
                {
                    mk_identifier_argument("c"),
                })
            )
        })
    }));


    //=============================================================
    //- identifier <- ident_start ident_cont*       { $0 }

    gr = gr.set_parser("identifier",
    mk_sequence_parser(
    {
        mk_sequence_parser(
        {
            ip_ident_start,
            mk_star_parser(ip_ident_cont)
        }),

        mk_action_parser(
            mk_return_action(mk_backref_argument(0))
        )
    }));

    //-------------------------------------------------------------
    //- ident_start <- [a-zA-Z_]

    gr = gr.set_parser("ident_start",
    mk_class_parser({{'a','z'}, {'A','Z'}, {'_','_'}}));

    //-------------------------------------------------------------
    //- ident_cont <- ident_start / dec_digit

    gr = gr.set_parser("ident_cont",
    mk_choice_parser(
    {
        ip_ident_start,
        ip_dec_digit
    }));


    //-------------------------------------------------------------
    //- integer <- '+'? n:dec_natural   { mk_pos_integer(n) }
    //-          / '-'  n:dec_natural   { mk_neg_integer(n) }

    gr = gr.set_parser("integer",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_question_parser(mk_character_parser('+')),
            mk_catch_variable_parser("n", ip_dec_natural),

            mk_action_parser(
                mk_call_action("mk_pos_integer",
                {
                    mk_identifier_argument("n")
                })
            )
        }),

        mk_sequence_parser(
        {
            mk_character_parser('-'),
            mk_catch_variable_parser("n", ip_dec_natural),

            mk_action_parser(
                mk_call_action("mk_neg_integer",
                {
                    mk_identifier_argument("n")
                })
            )
        })
    }));

    //-------------------------------------------------------------
    //- dec_natural <- dec_positive
    //-              / '0'              { mk_dec_numdigit('0') }

    gr = gr.set_parser("dec_natural",
    mk_choice_parser(
    {
        ip_dec_positive,

        mk_sequence_parser(
        {
            mk_character_parser('0'),

            mk_action_parser(
                mk_call_action("mk_dec_numdigit",
                {
                    mk_character_argument('0')
                })
            )
        })
    }));

    //-------------------------------------------------------------
    //- dec_positive <- n:dec_positive d:dec_digit      { mk_dec_integer(n, d) }
    //-               / d:[1-9]                         { mk_dec_numdigit(d) }

    gr = gr.set_parser("dec_positive",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_dec_positive),
            mk_catch_variable_parser("d", ip_dec_digit),

            mk_action_parser(
                mk_call_action("mk_dec_integer",
                {
                    mk_identifier_argument("n"),
                    mk_identifier_argument("d")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("d",
                mk_class_parser({{'1','9'}})
            ),

            mk_action_parser(
                mk_call_action("mk_dec_numdigit",
                {
                    mk_identifier_argument("d")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- dec_digit <- [0-9]

    gr = gr.set_parser("dec_digit",
    mk_class_parser({{'0','9'}})
    );


    //-------------------------------------------------------------
    //- string <- '\"' s:str_body '\"'      { s }
    //-         / "\"\""                    { "" }

    gr = gr.set_parser("string",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_character_parser('\"'),
            mk_catch_variable_parser("s", ip_str_body),
            mk_character_parser('\"'),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("s"))
            )
        }),
        mk_sequence_parser(
        {
            mk_literal_parser("\"\""),

            mk_action_parser(
                mk_return_action(mk_literal_argument(""))
            )
        })
    }));

    //-------------------------------------------------------------
    //- char <- '\'' c:str_char '\''        { c }

    gr = gr.set_parser("char",
    mk_sequence_parser(
    {
        mk_character_parser('\''),
        mk_catch_variable_parser("c", ip_str_char),
        mk_character_parser('\''),

        mk_action_parser(
            mk_return_action(mk_identifier_argument("c"))
        )
    }));

    //-------------------------------------------------------------
    //- str_body <- s:str_body c:str_char   { mk_string(s, c) }
    //-           / c:str_char              { mk_string("", c) }

    gr = gr.set_parser("str_body",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("s", ip_str_body),
            mk_catch_variable_parser("c", ip_str_char),

            mk_action_parser(
                mk_call_action("mk_string",
                {
                    mk_identifier_argument("s"),
                    mk_identifier_argument("c")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("c", ip_str_char),

            mk_action_parser(
                mk_call_action("mk_string",
                {
                    mk_literal_argument(""),
                    mk_identifier_argument("c")
                })
            )
        })
    }), true);      //- Left recursion!

    //-------------------------------------------------------------
    //- str_char <- ![\n\r\t'"] character

    gr = gr.set_parser("str_char",
    mk_sequence_parser(
    {
        mk_not_parser(
            mk_class_parser({{'\n','\n'},{'\r','\r'},{'\t','\t'},{'\'','\''},{'\"','\"'}})
        ),
        ip_character
    }));

    //-------------------------------------------------------------
    //- character <- '\\' esc_sequence
    //-            / !'\\' .

    gr = gr.set_parser("character",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_character_parser('\\'),
            ip_esc_sequence
        }),
        mk_sequence_parser(
        {
            mk_not_parser(mk_character_parser('\\')),
            mk_dot_parser()
        }),
    }));

    //-------------------------------------------------------------
    //- esc_sequence <- 'n'                 { '\n' }
    //-               / 'r'                 { '\r' }
    //-               / 't'                 { '\t' }
    //-               / '\''
    //-               / '\"'
    //-               / '\\'

    gr = gr.set_parser("esc_sequence",
    mk_choice_parser(
    {
        mk_sequence_parser({ mk_character_parser('n'),  mk_action_parser(mk_return_action(mk_character_argument('\n'))) }),
        mk_sequence_parser({ mk_character_parser('r'),  mk_action_parser(mk_return_action(mk_character_argument('\r'))) }),
        mk_sequence_parser({ mk_character_parser('t'),  mk_action_parser(mk_return_action(mk_character_argument('\t'))) }),
        mk_character_parser('\''),
        mk_character_parser('\"'),
        mk_character_parser('\\')
    }));


    //-------------------------------------------------------------
    //- _ <- (space / comment)*

    gr = gr.set_parser("_",
    mk_star_parser(
        mk_choice_parser({ip_space, ip_comment})
    ));

    //-------------------------------------------------------------
    //- comment <- "//" (!EOL .)* EOL

    gr = gr.set_parser("comment",
    mk_sequence_parser(
    {
        mk_literal_parser("//"),
        mk_star_parser(
            mk_sequence_parser(
            {
                mk_not_parser(ip_EOL),
                mk_dot_parser()
            })
        ),
        ip_EOL
    }));

    //-------------------------------------------------------------
    //- space <- ' ' / '\t' / EOL

    gr = gr.set_parser("space",
    mk_choice_parser(
    {
        mk_character_parser(' '),
        mk_character_parser('\t'),
        ip_EOL
    }));

    //-------------------------------------------------------------
    //- EOL <- '\n' / "\r\n" / '\r'

    gr = gr.set_parser("EOL",
    mk_choice_parser(
    {
        mk_character_parser('\n'),
        mk_literal_parser("\r\n"),
        mk_character_parser('\r')
    }));


    //=============================================================
    return gr;
}



