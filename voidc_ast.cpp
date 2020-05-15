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
static inline
std::string make_arg_string(const std::string &vstr)
{
    const char *str = vstr.c_str();

    size_t len = 0;

    for (auto p=str; *p; ++p)
    {
        if (*p == '\\') ++p;        //- ?!?!?

        ++len;
    }

    std::string ret(len, ' ');

    char *dst = ret.data();

    for (int i=0; i<len; ++i) dst[i] = get_raw_character(str);

    return ret;
}

ast_arg_string_t::ast_arg_string_t(const std::string &vstr)
  : string(make_arg_string(vstr))
{}



