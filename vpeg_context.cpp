//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_context.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
context_t *context_t::current_ctx = nullptr;


//-----------------------------------------------------------------
context_t::context_t(FILE *_input, const grammar_t &_grammar)
  : input(_input),
    grammar(_grammar)
{
    grammar.check_hash();
}


//-----------------------------------------------------------------
void context_t::static_initialize(void)
{
}

//-----------------------------------------------------------------
void context_t::static_terminate(void)
{
}



//-----------------------------------------------------------------
std::string context_t::take_string(size_t from, size_t to) const
{
    auto src = buffer.begin() + from;

    size_t count = to - from;

    size_t len = 0;

    auto s = src;

    for (size_t i=0; i<count; ++i)
    {
        char32_t c = *s++;

        if (c <= 0x7FF)
        {
            if (c <= 0x7F)  len += 1;
            else            len += 2;
        }
        else
        {
            if (c <= 0xFFFF)  len += 3;
            else              len += 4;
        }
    }

    std::string dst(len, ' ');

    char *d = dst.data();

    s = src;

    for (size_t i=0; i<count; ++i)
    {
        char32_t c = *s++;

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
            for (int j = 0; j < r; ++j)
            {
                d[r-j] = 0x80 | (c & 0x3F);

                c >>= 6;
            }

            d[0] = (0x1E << (6-r)) | (c & (0x3F >> r));
        }

        d += r + 1;
    }

    return dst;
}


//-----------------------------------------------------------------
void context_t::get_line_column(size_t pos, size_t &line, size_t &column) const
{
    auto it = newlines.upper_bound(pos);

    it = std::prev(it);

    assert(it != newlines.end());

    auto &[lpos, lnum] = *it;

    line = lnum;

    column = pos - lpos;
}


//-----------------------------------------------------------------
char32_t context_t::read_character(void)
{
    //- First, obtain a utf-8 codepoint

    int c_eof = std::fgetc(input);

    if (c_eof == EOF) return char32_t(-1);      //- Sic!

    uint8_t c0 = uint8_t(c_eof);

    int n;

    uint32_t r;

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
        c0 = uint8_t(std::fgetc(input));

        r = (r << 6) | (c0 & 0x3F);
    }

    //- Now, check for EOL

    if (r == U'\n')
    {
        newlines[buffer.size()+1] = current_line++;

        cr_flag = false;
    }
    else
    {
        if (cr_flag)
        {
            newlines[buffer.size()] = current_line++;
        }

        cr_flag = (r == U'\r');
    }

    //- ...

    return r;
}


//---------------------------------------------------------------------
}   //- namespace vpeg


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
extern "C"
{

using namespace vpeg;

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
void v_peg_get_grammar(grammar_sptr_t *ptr)
{
    if (context_t::current_ctx)
    {
        *ptr = std::make_shared<const grammar_t>(context_t::current_ctx->grammar);
    }
    else
    {
        (*ptr).reset();
    }
}

void v_peg_set_grammar(const grammar_sptr_t *ptr)
{
    if (context_t::current_ctx)
    {
        auto &grammar = context_t::current_ctx->grammar;

        grammar = **ptr;

        grammar.check_hash();
    }
}

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


