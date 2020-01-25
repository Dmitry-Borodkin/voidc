#include "voidc_ast.h"

#include <cassert>


//----------------------------------------------------------------------
//- PEG support ...
//----------------------------------------------------------------------
value_t mk_unit(auxil_t auxil, value_t stmt_list, int pos)
{
    const ast_stmt_list_t *sl = nullptr;

    if (stmt_list)
    {
        sl = dynamic_cast<const ast_stmt_list_t *>(stmt_list);

        assert(sl);
    }

    int line;
    int column;

    {   int the_pos = auxil->unit_pos + pos;

        auto it = auxil->newlines.upper_bound(the_pos);

        if (it != auxil->newlines.end())  line = it->second;
        else                              line = auxil->line_number;

        column = 1 + the_pos - (--it)->first;
    }

    return  auxil->do_util<ast_unit_t>(sl, line, column);
}

//----------------------------------------------------------------------
value_t mk_stmt_list(auxil_t auxil, value_t stmt, value_t stmt_list)
{
    auto s = dynamic_cast<const ast_stmt_t *>(stmt);
    assert(s);

    const ast_stmt_list_t *sl = nullptr;

    if (stmt_list)
    {
        sl = dynamic_cast<const ast_stmt_list_t *>(stmt_list);

        assert(sl);
    }

    return  auxil->do_util<ast_stmt_list_t>(*s, sl);
}

//----------------------------------------------------------------------
value_t mk_stmt(auxil_t auxil, value_t var, value_t call)
{
    auto s = (var ? (const char *)var : "");

    auto c = dynamic_cast<const ast_call_t *>(call);
    assert(c);

    return  auxil->do_util<ast_stmt_t>(s, *c);
}

//----------------------------------------------------------------------
value_t mk_call(auxil_t auxil, value_t fun, value_t arg_list)
{
    auto f = (const char *)fun;

    const ast_arg_list_t *al = nullptr;

    if (arg_list)
    {
        al = dynamic_cast<const ast_arg_list_t *>(arg_list);

        assert(al);
    }

    return  auxil->do_util<ast_call_t>(f, al);
}

//----------------------------------------------------------------------
value_t mk_arg_list(auxil_t auxil, value_t arg, value_t arg_list)
{
    auto a = dynamic_cast<const ast_argument_t *>(arg);
    assert(a);

    const ast_arg_list_t *al = nullptr;

    if (arg_list)
    {
        al = dynamic_cast<const ast_arg_list_t *>(arg_list);

        assert(al);
    }

    return  auxil->do_util<ast_arg_list_t>(*a, al);
}

//----------------------------------------------------------------------
value_t mk_arg_identifier(auxil_t auxil, value_t ident)
{
    return  auxil->do_util<ast_arg_identifier_t>((const char *)ident);
}

//----------------------------------------------------------------------
value_t mk_arg_integer(auxil_t auxil, value_t num)
{
    return  auxil->do_util<ast_arg_integer_t>((intptr_t)num);
}

//----------------------------------------------------------------------
static
char get_character(const char * &p)
{
    assert(*p);

    char c = *p++;

    if (c == '\\')
    {
        c = *p++;

        switch(c)
        {
        case 'n':   c = '\n'; break;
        case 'r':   c = '\r'; break;
        case 't':   c = '\t'; break;
        }
    }

    return  c;
}

//----------------------------------------------------------------------
ast_arg_string_t::ast_arg_string_t(const char *str)
{
    int len = 0;

    for (auto p=str; *p; ++p)
    {
        if (*p == '\\') ++p;        //- ?!?!?

        ++len;
    }

    string_private.resize(len);

    for (int i=0; i<len; ++i) string_private[i] = get_character(str);
}

//----------------------------------------------------------------------
value_t mk_arg_string(auxil_t auxil, value_t str)
{
    return  auxil->do_util<ast_arg_string_t>((const char *)str);
}

//----------------------------------------------------------------------
value_t mk_arg_char(auxil_t auxil, value_t c)
{
    char r = get_character((const char * &)c);

    return  auxil->do_util<ast_arg_char_t>(r);
}

//----------------------------------------------------------------------
void mk_newline(auxil_t auxil, int pos)
{
    return auxil->do_newline(pos);
}

//----------------------------------------------------------------------
int voidc_getchar(auxil_t auxil)
{
    return auxil->do_getchar();
}


