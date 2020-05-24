#include "vpeg_voidc.h"

#include "voidc_ast.h"


//---------------------------------------------------------------------
namespace vpeg
{

//---------------------------------------------------------------------
extern "C"
{

static void
mk_unit(std::any *ret, const std::any *args, size_t)
{
    auto p = std::any_cast<std::shared_ptr<const ast_stmt_list_t>>(args+0);

    if (!p)
    {
        static const std::shared_ptr<const ast_stmt_list_t> nil;

        p = &nil;
    }

    auto pos = std::any_cast<size_t>(args[1]);

    *ret = std::make_shared<const ast_unit_t>(*p, 0, pos);
}

static void
mk_stmt_list(std::any *ret, const std::any *args, size_t)
{
    auto plst = std::any_cast<std::shared_ptr<const ast_stmt_list_t>>(args+0);

    if (!plst)
    {
        static const std::shared_ptr<const ast_stmt_list_t> nil;

        plst = &nil;
    }

    auto item = std::any_cast<std::shared_ptr<const ast_stmt_t>>(args[1]);

    *ret = std::make_shared<const ast_stmt_list_t>(*plst, item);
}

static void
mk_stmt(std::any *ret, const std::any *args, size_t)
{
    std::string s = "";

    if (auto p = std::any_cast<const std::string>(args+0)) { s = *p; }

    auto c = std::any_cast<std::shared_ptr<const ast_call_t>>(args[1]);

    *ret = std::make_shared<const ast_stmt_t>(s, c);
}

static void
mk_call(std::any *ret, const std::any *args, size_t)
{
    auto f = std::any_cast<const std::string>(args[0]);

    auto a = std::any_cast<std::shared_ptr<const ast_arg_list_t>>(args+1);

    if (!a)
    {
        static const std::shared_ptr<const ast_arg_list_t> nil;

        a = &nil;
    }

    *ret = std::make_shared<const ast_call_t>(f, *a);
}

static void
mk_arg_list(std::any *ret, const std::any *args, size_t)
{
    auto plst = std::any_cast<std::shared_ptr<const ast_arg_list_t>>(args+0);

    if (!plst)
    {
        static const std::shared_ptr<const ast_arg_list_t> nil;

        plst = &nil;
    }

    auto item = std::any_cast<std::shared_ptr<const ast_argument_t>>(args[1]);

    *ret = std::make_shared<const ast_arg_list_t>(*plst, item);
}

static void
mk_arg_identifier(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<const std::string>(args[0]);

    std::shared_ptr<const ast_argument_t> r = std::make_shared<const ast_arg_identifier_t>(n);

    *ret = r;
}

static void
mk_arg_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    std::shared_ptr<const ast_argument_t> r = std::make_shared<const ast_arg_integer_t>(n);

    *ret = r;
}

static void
mk_arg_string(std::any *ret, const std::any *args, size_t)
{
    auto s = std::any_cast<const std::string>(args[0]);

    std::shared_ptr<const ast_argument_t> r = std::make_shared<const ast_arg_string_t>(s);

    *ret = r;
}

static void
mk_arg_char(std::any *ret, const std::any *args, size_t)
{
    auto p = std::any_cast<const std::string>(args[0]);

    auto get_character = [](const char * &str)
    {
        uint8_t c0 = *((const uint8_t * &)str)++;

        int n;

        char32_t r;

        if (c0 < 0xE0)
        {
            if (c0 < 0xC0)  { r = c0;           n = 0; }
            else            { r = (c0 & 0x1F);  n = 1; }
        }
        else
        {
            if (c0 < 0xF0)  { r = (c0 & 0x0F);  n = 2; }
            else            { r = (c0 & 0x07);  n = 3; }
        }

        for(; n; --n)
        {
            c0 = *((const uint8_t * &)str)++;

            r = (r << 6) | (c0 & 0x3F);
        }

        return r;
    };

    auto src = p.c_str();

    auto c = get_character(src);

    if (c == U'\\')
    {
        c = get_character(src);

        switch(c)
        {
        case U'n':  c = U'\n'; break;
        case U'r':  c = U'\r'; break;
        case U't':  c = U'\t'; break;
        }
    }

    std::shared_ptr<const ast_argument_t> r = std::make_shared<const ast_arg_char_t>(c);

    *ret = r;
}

static void
mk_neg_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    *ret = -n;
}

static void
mk_dec_integer(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<intptr_t>(args[0]);

    intptr_t d = std::any_cast<char32_t>(args[1]) - U'0';

//  printf("dec_int: %d %d\n", (int)n, (int)d);

    if (n)  d += 10*n;

    *ret = d;
}

static void
mk_newline(std::any *ret, const std::any *args, size_t)
{
    auto n = std::any_cast<size_t>(args[0]);

//  printf("mk_newline: %d\n", (int)n);

    *ret = true;    //- ...
}



static void
mk_echo(std::any *ret, const std::any *args, size_t)
{
    auto s = std::any_cast<const std::string>(args[0]);

    printf("%s", s.c_str());    //- ?...

    *ret = true;    //- ...
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
    DEF(mk_call)
    DEF(mk_arg_list)
    DEF(mk_arg_identifier)
    DEF(mk_arg_integer)
    DEF(mk_arg_string)
    DEF(mk_arg_char)
    DEF(mk_neg_integer)
    DEF(mk_dec_integer)
    DEF(mk_newline)

    DEF(mk_echo)

#undef DEF

    //-------------------------------------------------------------
#define DEF(name) auto ip_##name = mk_identifier_parser(#name);

    DEF(stmt_list)
    DEF(stmt_list_lr)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(arg_list_lr)
    DEF(arg)

    DEF(identifier)
    DEF(ident_start)
    DEF(ident_cont)
    DEF(integer)
    DEF(dec_integer)
    DEF(dec_digit)
    DEF(string)
    DEF(str_char)
    DEF(char)
    DEF(character)

    DEF(_)
    DEF(comment)
    DEF(space)
    DEF(eol)
    DEF(EOL)

#undef DEF

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
            mk_not_parser(mk_dot_parser())
        })
    }));

    gr = gr.set_parser("stmt_list",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_stmt_list_lr),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("l"))
            )
        }),
        mk_action_parser(
            mk_return_action(mk_integer_argument(0))
        )
    }));

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

    gr = gr.set_parser("stmt",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("v", ip_identifier),
            ip__,
            mk_character_parser('='),
            ip__,
            mk_catch_variable_parser("c", ip_call),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_identifier_argument("v"),
                    mk_identifier_argument("c")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("c", ip_call),

            mk_action_parser(
                mk_call_action("mk_stmt",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("c")
                })
            )
        })
    }));

    gr = gr.set_parser("call",
    mk_sequence_parser(
    {
        mk_catch_variable_parser("f", ip_identifier),
        ip__,
        mk_character_parser('('),
        ip__,
        mk_catch_variable_parser("a", ip_arg_list),
        ip__,
        mk_character_parser(')'),
        ip__,
        mk_character_parser(';'),

        mk_action_parser(
            mk_call_action("mk_call",
            {
                mk_identifier_argument("f"),
                mk_identifier_argument("a")
            })
        )
    }));

    gr = gr.set_parser("arg_list",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_arg_list_lr),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("l"))
            )
        }),
        mk_action_parser(
            mk_return_action(mk_integer_argument(0))
        )
    }));

    gr = gr.set_parser("arg_list_lr",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("l", ip_arg_list_lr),
            ip__,
            mk_character_parser(','),
            ip__,
            mk_catch_variable_parser("a", ip_arg),

            mk_action_parser(
                mk_call_action("mk_arg_list",
                {
                    mk_identifier_argument("l"),
                    mk_identifier_argument("a")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("a", ip_arg),

            mk_action_parser(
                mk_call_action("mk_arg_list",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("a")
                })
            )
        })
    }), true);      //- Left recursion!

    gr = gr.set_parser("arg",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("i", ip_identifier),

            mk_action_parser(
                mk_call_action("mk_arg_identifier",
                {
                    mk_identifier_argument("i"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_integer),

            mk_action_parser(
                mk_call_action("mk_arg_integer",
                {
                    mk_identifier_argument("n"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("s", ip_string),

            mk_action_parser(
                mk_call_action("mk_arg_string",
                {
                    mk_identifier_argument("s"),
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_catch_variable_parser("c", ip_char),

            mk_action_parser(
                mk_call_action("mk_arg_char",
                {
                    mk_identifier_argument("c"),
                })
            )
        })
    }));

    //-------------------------------------------------------
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

    gr = gr.set_parser("ident_start",
    mk_class_parser({{'a','z'}, {'A','Z'}, {'_','_'},   {'$','$'}, {'.','.'}}));    //- ?

    gr = gr.set_parser("ident_cont",
    mk_choice_parser(
    {
        ip_ident_start,
        mk_character_parser('-'),       //- ?
        ip_dec_digit
    }));


    gr = gr.set_parser("integer",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_dec_integer),

            mk_action_parser(
                mk_return_action(mk_identifier_argument("n"))
            )
        }),
        mk_sequence_parser(
        {
            mk_character_parser('-'),
            mk_catch_variable_parser("n", ip_dec_integer),

            mk_action_parser(
                mk_call_action("mk_neg_integer",
                {
                    mk_identifier_argument("n")
                })
            )
        }),
        mk_sequence_parser(
        {
            mk_character_parser('0'),

            mk_action_parser(
                mk_return_action(mk_integer_argument(0))
            )
        })
    }));

    gr = gr.set_parser("dec_integer",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_catch_variable_parser("n", ip_dec_integer),
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
                mk_call_action("mk_dec_integer",
                {
                    mk_integer_argument(0),
                    mk_identifier_argument("d")
                })
            )
        })
    }), true);      //- Left recursion!

    gr = gr.set_parser("dec_digit",
    mk_sequence_parser(
    {
        mk_catch_variable_parser("d",
            mk_class_parser({{'0','9'}})
        ),

        mk_action_parser(
            mk_return_action(mk_identifier_argument("d"))
        )
    }));

    gr = gr.set_parser("string",
    mk_sequence_parser(
    {
        mk_character_parser('\"'),
        mk_catch_string_parser(
            mk_star_parser(ip_str_char)
        ),
        mk_character_parser('\"'),

        mk_action_parser(
            mk_return_action(mk_backref_argument(1))
        )
    }));

    gr = gr.set_parser("str_char",
    mk_sequence_parser(
    {
        mk_not_parser(
            mk_class_parser({{'\n','\n'},{'\r','\r'},{'\t','\t'},{'\'','\''},{'\"','\"'}})
        ),
        ip_character
    }));

    gr = gr.set_parser("char",
    mk_sequence_parser(
    {
        mk_character_parser('\''),
        mk_catch_string_parser(ip_str_char),
        mk_character_parser('\''),

        mk_action_parser(
            mk_return_action(mk_backref_argument(1))
        )
    }));

    gr = gr.set_parser("character",
    mk_choice_parser(
    {
        mk_sequence_parser(
        {
            mk_character_parser('\\'),
            mk_class_parser({{'n','n'},{'r','r'},{'t','t'},{'\'','\''},{'\"','\"'},{'\\','\\'}})
        }),
        mk_sequence_parser(
        {
            mk_not_parser(mk_character_parser('\\')),
            mk_dot_parser()
        }),
    }));

    gr = gr.set_parser("_",
    mk_star_parser(
        mk_choice_parser({ip_space, ip_comment})
    ));

    gr = gr.set_parser("comment",
    mk_sequence_parser(
    {
        mk_literal_parser("//"),
        mk_star_parser(
            mk_sequence_parser(
            {
                mk_not_parser(ip_eol),
                mk_dot_parser()
            })
        ),
        ip_EOL
    }));

    gr = gr.set_parser("space",
    mk_choice_parser(
    {
        mk_character_parser(' '),
        mk_character_parser('\t'),
        ip_EOL
    }));

    gr = gr.set_parser("eol",
    mk_choice_parser(
    {
        mk_literal_parser("\r\n"),
        mk_character_parser('\n'),
        mk_character_parser('\r')
    }));

    gr = gr.set_parser("EOL",
    mk_sequence_parser(
    {
        ip_eol,

        mk_action_parser(
            mk_call_action("mk_newline",
            {
                mk_backref_argument(0, backref_argument_t::bk_end)
            })
        )
    }));

    return gr;
}



