#include "voidc_ast.h"

#include <cassert>


//----------------------------------------------------------------------
static
char get_raw_character(const char * &p)
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
ast_arg_string_t::ast_arg_string_t(const vpeg::string &vstr)
{
    const char *str = vstr.c_str();

    size_t len = 0;

    for (auto p=str; *p; ++p)
    {
        if (*p == '\\') ++p;        //- ?!?!?

        ++len;
    }

    string_private = vpeg::string(len, ' ');

    char *dst = string_private.data();

    for (int i=0; i<len; ++i) dst[i] = get_raw_character(str);
}


