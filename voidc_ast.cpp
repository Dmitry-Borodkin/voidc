#include "voidc_ast.h"

#include <cassert>


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


