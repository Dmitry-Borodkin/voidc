//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_grammar.h"

#include "vpeg_context.h"
#include "voidc_types.h"
#include "voidc_target.h"
#include "voidc_util.h"

#include <llvm-c/Core.h>

#include <cstdio>
#include <functional>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
void grammar_t::check_hash(void)
{
    if (hash != size_t(-1)) return;

    static size_t static_hash = 0;

    _hash = static_hash++ ;
}


//-----------------------------------------------------------------
std::any grammar_t::parse(v_quark_t q_name, context_t &ctx) const
{
//  printf("parse? %s\n", v_quark_to_string(q_name));

    context_t::variables_t saved_vars;      //- empty(!)

    std::swap(ctx.variables, saved_vars);       //- save (and clear)

    {   auto &vvec = ctx.variables.values;
        auto &svec = ctx.variables.strings;

        vvec = values;                                  //- "Global" values
        svec = svec.push_back({ctx.get_position(), 0}); //- #0
    }

    auto st = ctx.get_state();

    std::any ret;

    assert(hash != size_t(-1));

    auto key = std::make_tuple(hash, st.position, q_name);

    if (ctx.memo.count(key))
    {
        auto &[res, est] = ctx.memo[key];

        ctx.set_state(est);

        ret = res;
    }
    else
    {
        auto &[parser, leftrec] = parsers.at(q_name);

        if (leftrec)        //- Left-recursive ?
        {
            auto lastres = std::any();
            auto last_st = st;

            ctx.memo[key] = {lastres, st};

            for(;;)
            {
                ctx.set_state(st);

                auto res = parser->parse(ctx);
                auto est = ctx.get_state();

                if (est.position <= last_st.position) break;

                lastres = res;
                last_st = est;

                ctx.memo[key] = {lastres, est};
            }

            ctx.set_state(last_st);

            ret = lastres;
        }
        else                //- NOT left-recursive
        {
            auto res = parser->parse(ctx);
            auto est = ctx.get_state();

            ctx.memo[key] = {res, est};

            ret = res;
        }
    }

    ctx.variables = saved_vars;                 //- restore saved

//  printf("parse! %s: %s\n", v_quark_to_string(q_name), (ret.has_value() ? "OK" : "Fail"));
//  if (ret.has_value())
//  {
//      size_t line, column;    ctx.get_line_column(ctx.get_position(), line, column);
//      printf("parse! %s (%zd, %zd)\n", v_quark_to_string(q_name), line, column);
//  }

    return ret;
}


//-----------------------------------------------------------------
void grammar_t::static_initialize(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(parser_sptr_t)/sizeof(intptr_t));

    auto add_type = [&gctx](const char *raw_name, v_type_t *type)
    {
        gctx.decls.constants_insert({raw_name, type});

        gctx.decls.symbols_insert({raw_name, gctx.opaque_type_type});

        gctx.add_symbol_value(raw_name, type);
    };

#define DEF(name) \
    static_assert(sizeof(parser_sptr_t) == sizeof(name##_sptr_t)); \
    auto name##_sptr_type = gctx.make_struct_type("struct.v_peg_opaque_" #name "_sptr"); \
    name##_sptr_type->set_body(&content_type, 1, false); \
    add_type("v_peg_opaque_" #name "_sptr", name##_sptr_type);

    DEF(grammar)

#undef DEF

    parsers_static_initialize();
}

//-----------------------------------------------------------------
void grammar_t::static_terminate(void)
{
    parsers_static_terminate();
}


//---------------------------------------------------------------------
}   //- namespace vpeg


//---------------------------------------------------------------------
//- !!!
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

    DEF(grammar)

#undef DEF


//-----------------------------------------------------------------
void v_peg_make_grammar(grammar_sptr_t *ret)
{
    *ret = std::make_shared<const grammar_t>();
}


//-----------------------------------------------------------------
void v_peg_grammar_get_parser(const grammar_sptr_t *ptr, const char *name, parser_sptr_t *parser, int *leftrec)
{
    if (auto *pair = (*ptr)->parsers.find(v_quark_from_string(name)))
    {
        if (parser)   *parser  = pair->first;
        if (leftrec)  *leftrec = pair->second;
    }
    else
    {
        if (parser)   *parser  = nullptr;
    }
}

void v_peg_grammar_set_parser(grammar_sptr_t *dst, const grammar_sptr_t *src, const char *name, const parser_sptr_t *parser, int leftrec)
{
    auto grammar = (*src)->set_parser(name, *parser, leftrec);

    *dst = std::make_shared<const grammar_t>(grammar);
}

void v_peg_grammar_erase_parser(grammar_sptr_t *dst, const grammar_sptr_t *src, const char *name)
{
    auto grammar = (*src)->erase_parser(name);

    *dst = std::make_shared<const grammar_t>(grammar);
}


//-----------------------------------------------------------------
grammar_action_fun_t
v_peg_grammar_get_action(const grammar_sptr_t *ptr, const char *name)
{
    if (auto *fun = (*ptr)->actions.find(v_quark_from_string(name)))
    {
        return *fun;
    }
    else
    {
        return nullptr;
    }
}

void v_peg_grammar_set_action(grammar_sptr_t *dst, const grammar_sptr_t *src, const char *name, grammar_action_fun_t fun)
{
    auto grammar = (*src)->set_action(name, fun);

    *dst = std::make_shared<const grammar_t>(grammar);
}

void v_peg_grammar_erase_action(grammar_sptr_t *dst, const grammar_sptr_t *src, const char *name)
{
    auto grammar = (*src)->erase_action(name);

    *dst = std::make_shared<const grammar_t>(grammar);
}


//-----------------------------------------------------------------
void v_peg_grammar_get_value(const grammar_sptr_t *ptr, const char *name, std::any *value)
{
    *value = (*ptr)->values.find(v_quark_from_string(name));
}

void v_peg_grammar_set_value(grammar_sptr_t *dst, const grammar_sptr_t *src, const char *name, const std::any *value)
{
    auto grammar = (*src)->set_value(name, *value);

    *dst = std::make_shared<const grammar_t>(grammar);
}

void v_peg_grammar_erase_value(grammar_sptr_t *dst, const grammar_sptr_t *src, const char *name)
{
    auto grammar = (*src)->erase_value(name);

    *dst = std::make_shared<const grammar_t>(grammar);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


