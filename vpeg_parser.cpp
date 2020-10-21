//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_parser.h"

#include "vpeg_grammar.h"
#include "vpeg_context.h"

#include <immer/array_transient.hpp>


//---------------------------------------------------------------------
//- Some utility
//---------------------------------------------------------------------
static char32_t
read_utf8_codepoint(const char * &utf8)
{
    if (!*utf8) return (char32_t)0;

    uint8_t c0 = (uint8_t)*utf8++;

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
        c0 = (uint8_t)*utf8++;

        r = (r << 6) | (c0 & 0x3F);
    }

    return (char32_t)r;
}


//---------------------------------------------------------------------
namespace vpeg
{


//---------------------------------------------------------------------
//- Parsers...
//---------------------------------------------------------------------
std::any choice_parser_t::parse(context_t &ctx) const
{
//  printf("(%d): choice?\n", (int)ctx.get_position());

    std::any r;

    for (auto &it : array)
    {
        r = it->parse(ctx);

        if (r.has_value())  break;
    }

    return r;
}

//---------------------------------------------------------------------
std::any sequence_parser_t::parse(context_t &ctx) const
{
//  printf("(%d): sequence?\n", (int)ctx.get_position());

    auto st = ctx.get_state();

    struct {} const dummy;

    std::any r(dummy);      //- Sic!

    for (auto &it : array)
    {
        r = it->parse(ctx);

        if (!r.has_value())
        {
            ctx.set_state(st);

            break;
        }
    }

    return r;
}


//-------------------------------------------------------------
std::any and_parser_t::parse(context_t &ctx) const
{
    auto st = ctx.get_state();

    auto r = parser->parse(ctx);

    ctx.set_state(st);

    return r;       //- ???
}

//-------------------------------------------------------------
std::any not_parser_t::parse(context_t &ctx) const
{
    auto st = ctx.get_state();

    auto r = parser->parse(ctx);

    ctx.set_state(st);

    struct {} const dummy;

    if (r.has_value())  r.reset();
    else                r = dummy;

    return r;
}

//-------------------------------------------------------------
std::any question_parser_t::parse(context_t &ctx) const
{
    auto r = parser->parse(ctx);

    if (r.has_value())  return r;

    struct {} const dummy;

    return dummy;
}

//-------------------------------------------------------------
std::any star_parser_t::parse(context_t &ctx) const
{
    struct {} const dummy;

    std::any ret = dummy;

    for(;;)
    {
        auto r = parser->parse(ctx);

        if (!r.has_value()) break;

        ret = r;
    }

    return ret;
}

//-------------------------------------------------------------
std::any plus_parser_t::parse(context_t &ctx) const
{
    struct {} const dummy;

    std::any ret;

    for(;;)
    {
        auto r = parser->parse(ctx);

        if (!r.has_value()) break;

        ret = r;
    }

    return ret;
}


//-------------------------------------------------------------
std::any catch_variable_parser_t::parse(context_t &ctx) const
{
    auto ret = parser->parse(ctx);

    if (ret.has_value())
    {
        auto &vmap = ctx.variables.values;

        vmap = vmap.set(q_name, ret);
    }

    return ret;
}

//-------------------------------------------------------------
std::any catch_string_parser_t::parse(context_t &ctx) const
{
    auto pos = ctx.get_position();

    auto ret = parser->parse(ctx);

    if (ret.has_value())
    {
        auto epos = ctx.get_position();

        auto &svec = ctx.variables.strings;

        svec = svec.push_back({pos, epos});
    }

    return ret;         //- ?
}


//-------------------------------------------------------------
std::any identifier_parser_t::parse(context_t &ctx) const
{
    return ctx.grammar.parse(q_ident, ctx);
}

//-------------------------------------------------------------
std::any backref_parser_t::parse(context_t &ctx) const
{
    auto st = ctx.get_state();

    auto v = st.variables.strings[number];

    if (number == 0)  v[1] = st.position;

    auto utf8 = ctx.take_string(v[0], v[1]);

    const char *str = utf8.c_str();

    while (char32_t ucs4 = read_utf8_codepoint(str))
    {
        if (!ctx.expect(ucs4))
        {
            ctx.set_state(st);

            return std::any();
        }
    }

    return utf8;
}

//-------------------------------------------------------------
std::any action_parser_t::parse(context_t &ctx) const
{
    return action->act(ctx);
}

//-------------------------------------------------------------
std::any literal_parser_t::parse(context_t &ctx) const
{
    auto st = ctx.get_state();

    const char *str = utf8.c_str();

    while (char32_t ucs4 = read_utf8_codepoint(str))
    {
        if (!ctx.expect(ucs4))
        {
            ctx.set_state(st);

            return std::any();
        }
    }

    return utf8;
}

//-------------------------------------------------------------
std::any character_parser_t::parse(context_t &ctx) const
{
    if (!ctx.expect(ucs4))  return std::any();

    return (int32_t)ucs4;
}

//-------------------------------------------------------------
static
immer::array<class_parser_t::range_t>
make_ranges_array(const char32_t (*list)[2], size_t count)
{
    auto tr = immer::array<class_parser_t::range_t>(count).transient();

    auto tmp = tr.data_mut();

    for (size_t i=0; i<count; ++i)
    {
        tmp[i] = {list[i][0], list[i][1]};
    }

    return tr.persistent();
}

class_parser_t::class_parser_t(const char32_t (*list)[2], size_t count)
  : ranges(make_ranges_array(list, count))
{}

std::any class_parser_t::parse(context_t &ctx) const
{
    auto ucs4 = ctx.peek_character();

    for (auto &it : ranges)
    {
        if (it[0] <= ucs4  &&  ucs4 <= it[1])
        {
            ctx.get_character();

            return (int32_t)ucs4;
        }
    }

    return std::any();
}

//-------------------------------------------------------------
std::any dot_parser_t::parse(context_t &ctx) const
{
    auto ucs4 = ctx.peek_character();

    if (ucs4 == char32_t(-1)) return std::any();

    ctx.get_character();

    return (int32_t)ucs4;
}


//-----------------------------------------------------------------
std::any call_action_t::act(context_t &ctx) const
{
    size_t N = args.size();

    auto a = std::make_unique<std::any[]>(N);

    for (size_t i=0; i<N; ++i)
    {
        a[i] = args[i]->value(ctx);
    }

//  printf("%s\n", v_quark_to_string(q_fun));

    std::any ret;

    auto fun = ctx.grammar.actions[q_fun];

#ifndef NDEBUG

    if (!fun)   fprintf(stderr, "grammar action not found: %s\n", v_quark_to_string(q_fun));

    assert(fun && "grammar action not found");

#endif

    fun(&ret, a.get(), N);

    return ret;
}


//-----------------------------------------------------------------
std::any identifier_argument_t::value(context_t &ctx) const
{
    return ctx.variables.values.at(q_ident);      //- ?...
}

//-----------------------------------------------------------------
std::any backref_argument_t::value(context_t &ctx) const
{
    auto v = ctx.variables.strings[number];

    if (number == 0)  v[1] = ctx.get_position();

    switch(b_kind)
    {
    case bk_string: return ctx.take_string(v[0], v[1]);
    case bk_start:  return v[0];
    case bk_end:    return v[1];
    }

    return std::any();      //- WTF?
}


//---------------------------------------------------------------------
}   //- namespace vpeg


