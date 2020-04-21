#include "vpeg_parser.h"

#include "vpeg_grammar.h"
#include "vpeg_context.h"


//---------------------------------------------------------------------
namespace vpeg
{

static struct {} const dummy;       //- Sic!


//---------------------------------------------------------------------
//- Parsers...
//---------------------------------------------------------------------
std::any choice_parser_t::parse(context_t &ctx) const
{
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
    auto st = ctx.get_state();

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

    if (r.has_value())  r.reset();
    else                r = dummy;

    return r;
}

//-------------------------------------------------------------
std::any question_parser_t::parse(context_t &ctx) const
{
    parser->parse(ctx);

    return dummy;
}

//-------------------------------------------------------------
std::any star_parser_t::parse(context_t &ctx) const
{
    for(;;)
    {
        auto r = parser->parse(ctx);

        if (!r.has_value()) break;
    }

    return dummy;
}

//-------------------------------------------------------------
std::any plus_parser_t::parse(context_t &ctx) const
{
    std::any ret;

    for(;;)
    {
        auto r = parser->parse(ctx);

        if (!r.has_value()) break;

        ret = dummy;
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

        vmap = vmap.set(name, ret);
    }

    return ret;
}

//-------------------------------------------------------------
std::any catch_string_parser_t::parse(context_t &ctx) const
{
    auto pos = ctx.get_state().position;

    auto ret = parser->parse(ctx);

    if (ret.has_value())
    {
        auto epos = ctx.get_state().position;

        auto &svec = ctx.variables.strings;

        svec = svec.push_back({pos, epos});
    }

    return ret;         //- ?
}


//-------------------------------------------------------------
std::any identifier_parser_t::parse(context_t &ctx) const
{
    return ctx.grammar.parse(ident, ctx);
}

//-------------------------------------------------------------
std::any backref_parser_t::parse(context_t &ctx) const
{
    auto st = ctx.get_state();

    auto v = st.variables.strings[number];

    if (number == 0)  v[1] = st.position;

    for (char32_t ucs4 : ctx.take_string(v[0], v[1]))
    {
        if (!ctx.expect(ucs4))
        {
            ctx.set_state(st);

            return std::any();
        }
    }

    return dummy;
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

    for (char32_t ucs4 : utf8)
    {
        if (!ctx.expect(ucs4))
        {
            ctx.set_state(st);

            return std::any();
        }
    }

    return dummy;
}

//-------------------------------------------------------------
std::any character_parser_t::parse(context_t &ctx) const
{
    if (!ctx.expect(ucs4))  return std::any();

    return dummy;
}

//-------------------------------------------------------------
std::any class_parser_t::parse(context_t &ctx) const
{
    auto ucs4 = ctx.peek_character();

    for (auto &it : ranges)
    {
        if (it[0] <= ucs4  &&  ucs4 <= it[1])
        {
            ctx.get_character();

            return ucs4;
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

    return ucs4;
}


//-----------------------------------------------------------------
std::any call_action_t::act(context_t &ctx) const
{
    size_t N = args.size();

    auto a = std::make_unique<std::any[]>(N);

    for (uint i=0; i<N; ++i)
    {
        a[i] = args[i]->value(ctx);
    }

//  printf("%s\n", fun.c_str());

    return functions[fun](ctx, a.get(), N);
}


//-----------------------------------------------------------------
std::any identifier_argument_t::value(context_t &ctx) const
{
    return ctx.variables.values[ident];
}

//-----------------------------------------------------------------
std::any backref_argument_t::value(context_t &ctx) const
{
    auto st = ctx.get_state();

    auto v = st.variables.strings[number];

    if (number == 0)  v[1] = st.position;

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


