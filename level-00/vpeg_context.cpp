//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_context.h"

#include "voidc_target.h"
#include "voidc_util.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
context_sptr_t context_t::current_ctx;


//-----------------------------------------------------------------
context_t::context_t(context_fgetc_fun_t fun, void *data, const grammar_t &_grammar)
  : grammar(_grammar),
    fgetc_fun(fun),
    fgetc_fun_data(data)
{
    grammar.check_hash();
}


//-----------------------------------------------------------------
context_t::context_t(FILE *input, const grammar_t &_grammar)
  : context_t(reinterpret_cast<context_fgetc_fun_t>(std::fgetc), input, _grammar)
{}


//-----------------------------------------------------------------
void context_t::static_initialize(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(parser_sptr_t)/sizeof(intptr_t));

    auto add_type = [&gctx](const char *raw_name, v_type_t *type)
    {
        gctx.decls.constants_insert({raw_name, gctx.static_type_type});

        gctx.constant_values.insert({raw_name, reinterpret_cast<LLVMValueRef>(type)});

        gctx.decls.symbols_insert({raw_name, gctx.opaque_type_type});

        gctx.add_symbol_value(raw_name, type);
    };

#define DEF(name) \
    static_assert(sizeof(parser_sptr_t) == sizeof(name##_sptr_t)); \
    auto name##_sptr_type = gctx.make_struct_type("struct.v_peg_opaque_" #name "_sptr"); \
    name##_sptr_type->set_body(&content_type, 1, false); \
    add_type("v_peg_opaque_" #name "_sptr", name##_sptr_type);

    DEF(context)

#undef DEF
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

    int c_eof = fgetc_fun(fgetc_fun_data);

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
        c0 = uint8_t(fgetc_fun(fgetc_fun_data));

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

//-----------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(name##_sptr_t, v_peg_initialize_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(name##_sptr_t, v_peg_terminate_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(name##_sptr_t, v_peg_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(name##_sptr_t, v_peg_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(name##_sptr_t, v_peg_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(name##_sptr_t, v_peg_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(name##_sptr_t, v_peg_std_any_set_pointer_##name##_impl)

    DEF(context)

#undef DEF


//-----------------------------------------------------------------
void v_peg_get_context(context_sptr_t *ret)
{
    *ret = context_t::current_ctx;
}

void v_peg_set_context(context_sptr_t *ctx)
{
    context_t::current_ctx = *ctx;
}


//-----------------------------------------------------------------
void v_peg_make_context(context_sptr_t *ret, context_fgetc_fun_t fun, void *data, const grammar_sptr_t *grm)
{
    *ret = std::make_shared<context_t>(fun, data, **grm);
}


//-----------------------------------------------------------------
void v_peg_parse(std::any *ret, v_quark_t q)
{
    auto &pctx = *context_t::current_ctx;

    *ret = pctx.grammar.parse(q, pctx);

    pctx.memo.clear();          //- ?
}


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

void v_peg_get_line_column(size_t pos, size_t *line, size_t *column)
{
    if (context_t::current_ctx)
    {
        context_t::current_ctx->get_line_column(pos, *line, *column);
    }
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


