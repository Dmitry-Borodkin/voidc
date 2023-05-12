//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
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
void grammar_data_t::check_hash(void)
{
    if (hash != size_t(-1)) return;

    static size_t static_hash = 0;

    _hash = static_hash++ ;
}


//-----------------------------------------------------------------
std::any grammar_data_t::parse(v_quark_t q_name, context_data_t &ctx) const
{
//  printf("parse? %s\n", v_quark_to_string(q_name));

    context_data_t::variables_t saved_vars;     //- empty(!)

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
void grammar_data_t::static_initialize(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(parser_t)/sizeof(intptr_t));

#define DEF(name) \
    static_assert(sizeof(parser_t) == sizeof(name##_t)); \
    auto name##_q = v_quark_from_string("v_peg_" #name "_t"); \
    auto name##_type = vctx.make_struct_type(name##_q); \
    name##_type->set_body(&content_type, 1, false); \
    vctx.initialize_type(name##_q, name##_type);

    DEF(grammar)

#undef DEF

    parsers_static_initialize();
}

//-----------------------------------------------------------------
void grammar_data_t::static_terminate(void)
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
    VOIDC_DEFINE_INITIALIZE_IMPL(name##_t, v_peg_initialize_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(name##_t, v_peg_terminate_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(name##_t, v_peg_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(name##_t, v_peg_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(name##_t, v_peg_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(name##_t, v_peg_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(name##_t, v_peg_std_any_set_pointer_##name##_impl)

    DEF(grammar)

#undef DEF


//-----------------------------------------------------------------
void
v_peg_make_grammar(grammar_t *ret)
{
    *ret = std::make_shared<const grammar_data_t>();
}


//-----------------------------------------------------------------
const parser_t *
v_peg_grammar_get_parser(const grammar_t *ptr, const char *name, int *leftrec)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    if (auto *pair = (*ptr)->parsers.find(qname))
    {
        if (leftrec)  *leftrec = pair->second;

        return &pair->first;
    }
    else
    {
        return nullptr;
    }
}

void
v_peg_grammar_set_parser(grammar_t *dst, const grammar_t *src, const char *name, const parser_t *parser, int leftrec)
{
    auto grammar = (*src)->set_parser(name, *parser, leftrec);

    *dst = std::make_shared<const grammar_data_t>(grammar);
}

void
v_peg_grammar_erase_parser(grammar_t *dst, const grammar_t *src, const char *name)
{
    auto grammar = (*src)->erase_parser(name);

    *dst = std::make_shared<const grammar_data_t>(grammar);
}


//-----------------------------------------------------------------
grammar_action_fun_t
v_peg_grammar_get_action(const grammar_t *ptr, const char *name, void **aux)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    if (auto *pair = (*ptr)->actions.find(qname))
    {
        if (aux)  *aux = pair->second;

        return pair->first;
    }
    else
    {
        return nullptr;
    }
}

void
v_peg_grammar_set_action(grammar_t *dst, const grammar_t *src, const char *name, grammar_action_fun_t fun, void *aux)
{
    auto grammar = (*src)->set_action(name, fun, aux);

    *dst = std::make_shared<const grammar_data_t>(grammar);
}

void
v_peg_grammar_erase_action(grammar_t *dst, const grammar_t *src, const char *name)
{
    auto grammar = (*src)->erase_action(name);

    *dst = std::make_shared<const grammar_data_t>(grammar);
}


//-----------------------------------------------------------------
const std::any *
v_peg_grammar_get_value(const grammar_t *ptr, const char *name)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    return (*ptr)->values.find(qname);
}

void
v_peg_grammar_set_value(grammar_t *dst, const grammar_t *src, const char *name, const std::any *value)
{
    auto grammar = (*src)->set_value(name, *value);

    *dst = std::make_shared<const grammar_data_t>(grammar);
}

void
v_peg_grammar_erase_value(grammar_t *dst, const grammar_t *src, const char *name)
{
    auto grammar = (*src)->erase_value(name);

    *dst = std::make_shared<const grammar_data_t>(grammar);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


