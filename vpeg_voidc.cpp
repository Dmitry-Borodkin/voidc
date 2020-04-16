#include "vpeg_voidc.h"


//---------------------------------------------------------------------
vpeg::grammar_t make_voidc_grammar(void)
{
    using namespace vpeg;

    auto ip_unit        = mk_identifier_parser("unit");
    auto ip_stmt_list   = mk_identifier_parser("stmt_list");
    auto ip_stmt        = mk_identifier_parser("stmt");
    auto ip_call        = mk_identifier_parser("call");
    auto ip_arg_list    = mk_identifier_parser("arg_list");
    auto ip_arg         = mk_identifier_parser("arg");
    auto ip_identifier  = mk_identifier_parser("identifier");
    auto ip_ident_start = mk_identifier_parser("ident_start");
    auto ip_ident_cont  = mk_identifier_parser("ident_cont");
    auto ip_integer     = mk_identifier_parser("integer");
    auto ip_dec_integer = mk_identifier_parser("dec_integer");
    auto ip_dec_digit   = mk_identifier_parser("dec_digit");
    auto ip_string      = mk_identifier_parser("string");
    auto ip_str_char    = mk_identifier_parser("str_char");
    auto ip_char        = mk_identifier_parser("char");
    auto ip_character   = mk_identifier_parser("character");
    auto ip__           = mk_identifier_parser("_");
    auto ip_comment     = mk_identifier_parser("comment");
    auto ip_space       = mk_identifier_parser("space");
    auto ip_eol         = mk_identifier_parser("eol");
    auto ip_EOL         = mk_identifier_parser("EOL");

    grammar_t gr;

    //-------------------------------------------------------------
    gr = gr.set("unit",
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
                })
               );

    gr = gr.set("stmt_list",
                mk_choice_parser(
                {
                    mk_sequence_parser(
                    {
                        mk_catch_variable_parser("s", ip_stmt),
                        ip__,
                        mk_catch_variable_parser("l", ip_stmt_list),

                        mk_action_parser(
                            mk_call_action("mk_stmt_list",
                            {
                                mk_identifier_argument("s"),
                                mk_identifier_argument("l")
                            })
                        )
                    }),
                    mk_action_parser(
                        mk_return_action(mk_integer_argument(0))
                    )
                })
               );

    gr = gr.set("stmt",
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
                })
               );

    gr = gr.set("call",
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
                })
               );

    gr = gr.set("arg_list",
                mk_choice_parser(
                {
                    mk_sequence_parser(
                    {
                        mk_catch_variable_parser("a", ip_arg),
                        ip__,
                        mk_character_parser(','),
                        ip__,
                        mk_catch_variable_parser("l", ip_arg_list),

                        mk_action_parser(
                            mk_call_action("mk_arg_list",
                            {
                                mk_identifier_argument("a"),
                                mk_identifier_argument("l")
                            })
                        )
                    }),
                    mk_sequence_parser(
                    {
                        mk_catch_variable_parser("a", ip_arg),

                        mk_action_parser(
                            mk_call_action("mk_arg_list",
                            {
                                mk_identifier_argument("a"),
                                mk_integer_argument(0)
                            })
                        )
                    }),
                    mk_action_parser(
                        mk_return_action(mk_integer_argument(0))
                    )
                })
               );

    gr = gr.set("arg",
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
                })
               );

    //-------------------------------------------------------------
    gr = gr.set("identifier",
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
                })
               );

    gr = gr.set("ident_start",
                mk_class_parser({{'a','z'}, {'A','Z'}, {'_','_'}})
               );

    gr = gr.set("ident_cont",
                mk_choice_parser(
                {
                    ip_ident_start,
                    ip_dec_digit
                })
               );


    gr = gr.set("integer",
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
                })
               );

    gr = gr.set("dec_integer",
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
                }),
                true    //- Left recursion!
               );

    gr = gr.set("dec_digit",
                mk_sequence_parser(
                {
                    mk_catch_variable_parser("d",
                        mk_class_parser({{'0','9'}})
                    ),

                    mk_action_parser(
                        mk_call_action("mk_dec_integer",
                        {
                            mk_integer_argument(0),
                            mk_identifier_argument("d")
                        })
                    )
                })
               );

    gr = gr.set("string",
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
                })
               );

    gr = gr.set("str_char",
                mk_sequence_parser(
                {
                    mk_not_parser(
                        mk_class_parser({{'\n','\n'},{'\r','\r'},{'\t','\t'},{'\'','\''},{'\"','\"'},{'\\','\\'}})
                    ),
                    ip_character
                })
               );

    gr = gr.set("char",
                mk_sequence_parser(
                {
                    mk_character_parser('\''),
                    mk_catch_string_parser(ip_str_char),
                    mk_character_parser('\''),

                    mk_action_parser(
                        mk_return_action(mk_backref_argument(1))
                    )
                })
               );

    gr = gr.set("character",
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
                })
               );

    gr = gr.set("_",
                mk_star_parser(
                    mk_choice_parser({ip_space, ip_comment})
                )
               );

    gr = gr.set("comment",
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
                })
               );

    gr = gr.set("space",
                mk_choice_parser(
                {
                    mk_character_parser(' '),
                    mk_character_parser('\t'),
                    ip_EOL
                })
               );

    gr = gr.set("eol",
                mk_choice_parser(
                {
                    mk_literal_parser("\r\n"),
                    mk_character_parser('\n'),
                    mk_character_parser('\r')
                })
               );

    gr = gr.set("EOL",
                mk_sequence_parser(
                {
                    ip_eol,

                    mk_action_parser(
                        mk_call_action("mk_newline",
                        {
                            mk_backref_argument(0, backref_argument_t::bk_end)
                        })
                    )
                })
               );

    return gr;
}



