//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "vpeg_grammar.h"

#include "vpeg_context.h"
#include "voidc_target.h"
#include "voidc_util.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


#include <cstdio>


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

    auto st = ctx.get_state();

    {   auto &svec = ctx.variables.strings;

        svec = svec.push_back({st.position, 0});    //- #0

        st.variables.strings = svec;        //- Sic!
    }

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

    return ret;
}


//-----------------------------------------------------------------
void grammar_t::static_initialize(void)
{
    static_assert((sizeof(parser_ptr_t) % sizeof(intptr_t)) == 0);

    auto &gctx = *voidc_global_ctx_t::voidc;

    auto content_type = LLVMArrayType(gctx.intptr_t_type, sizeof(parser_ptr_t)/sizeof(intptr_t));

    auto gc = LLVMGetGlobalContext();

#define DEF(name) \
    static_assert(sizeof(parser_ptr_t) == sizeof(name##_ptr_t)); \
    auto name##_ptr_type = LLVMStructCreateNamed(gc, "struct.v_peg_opaque_" #name "_ptr"); \
    LLVMStructSetBody(name##_ptr_type, &content_type, 1, false); \
    gctx.add_symbol("v_peg_opaque_" #name "_ptr", gctx.LLVMOpaqueType_type, (void *)name##_ptr_type);

    DEF(parser)
    DEF(action)
    DEF(argument)
    DEF(grammar)

#undef DEF

#define DEF(name, kind) \
    gctx.constants["v_peg_" #name "_kind_" #kind] = LLVMConstInt(gctx.int_type, name##_t::k_##kind, 0);

    DEF(parser, choice)
    DEF(parser, sequence)
    DEF(parser, and)
    DEF(parser, not)
    DEF(parser, question)
    DEF(parser, star)
    DEF(parser, plus)
    DEF(parser, catch_variable)
    DEF(parser, catch_string)
    DEF(parser, identifier)
    DEF(parser, backref)
    DEF(parser, action)
    DEF(parser, literal)
    DEF(parser, character)
    DEF(parser, class)
    DEF(parser, dot)

    DEF(action, call)
    DEF(action, return)

    DEF(argument, identifier)
    DEF(argument, backref)
    DEF(argument, integer)
    DEF(argument, literal)
    DEF(argument, character)

#undef DEF

#define DEF(kind) \
    gctx.constants["v_peg_backref_argument_kind_" #kind] = \
        LLVMConstInt(gctx.int_type, backref_argument_t::bk_##kind, 0);

    DEF(string)
    DEF(start)
    DEF(end)

#undef DEF

}

//-----------------------------------------------------------------
void grammar_t::static_terminate(void)
{
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
    VOIDC_DEFINE_INITIALIZE_IMPL(name##_ptr_t, v_peg_initialize_##name##_impl) \
    VOIDC_DEFINE_DESTROY_IMPL(name##_ptr_t, v_peg_destroy_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(name##_ptr_t, v_peg_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(name##_ptr_t, v_peg_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(name##_ptr_t, v_peg_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(name##_ptr_t, v_peg_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(name##_ptr_t, v_peg_std_any_set_pointer_##name##_impl)

    DEF(parser)
    DEF(action)
    DEF(argument)
    DEF(grammar)

#undef DEF

#define DEF_KIND(ptr_t, fun_name) \
int fun_name(const ptr_t *ptr) \
{ \
    return (*ptr)->kind(); \
}

#define DEF(name) \
    DEF_KIND(name##_ptr_t, v_peg_##name##_get_kind)

    DEF(parser)
    DEF(action)
    DEF(argument)

#undef DEF

#undef DEF_KIND


//-----------------------------------------------------------------
int v_peg_parser_get_parsers_count(const parser_ptr_t *ptr)
{
    return int((*ptr)->parsers_count());
}

void v_peg_parser_get_parsers(const parser_ptr_t *ptr, parser_ptr_t *list)
{
    (*ptr)->get_parsers(list);
}

//-----------------------------------------------------------------
void v_peg_make_choice_parser(parser_ptr_t *ret, const parser_ptr_t *list, int count)
{
    *ret = std::make_shared<const choice_parser_t>(list, size_t(count));
}

void v_peg_make_sequence_parser(parser_ptr_t *ret, const parser_ptr_t *list, int count)
{
    *ret = std::make_shared<const sequence_parser_t>(list, size_t(count));
}


void v_peg_make_and_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const and_parser_t>(*ptr);
}

void v_peg_make_not_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const not_parser_t>(*ptr);
}

void v_peg_make_question_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const question_parser_t>(*ptr);
}

void v_peg_make_star_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const star_parser_t>(*ptr);
}

void v_peg_make_plus_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const plus_parser_t>(*ptr);
}

void v_peg_make_catch_variable_parser(parser_ptr_t *ret, const char *name, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const catch_variable_parser_t>(name, *ptr);
}

const char *v_peg_catch_variable_parser_get_name(const parser_ptr_t *ptr)
{
    auto &r = dynamic_cast<const catch_variable_parser_t &>(**ptr);

    return  v_quark_to_string(r.q_name);
}

void v_peg_make_catch_string_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<const catch_string_parser_t>(*ptr);
}


void v_peg_make_identifier_parser(parser_ptr_t *ret, const char *ident)
{
    *ret = std::make_shared<const identifier_parser_t>(ident);
}

const char *v_peg_identifier_parser_get_identifier(const parser_ptr_t *ptr)
{
    auto &r = dynamic_cast<const identifier_parser_t &>(**ptr);

    return  v_quark_to_string(r.q_ident);
}

void v_peg_make_backref_parser(parser_ptr_t *ret, int number)
{
    *ret = std::make_shared<const backref_parser_t>(size_t(number));
}

int v_peg_backref_parser_get_number(const parser_ptr_t *ptr)
{
    auto &r = dynamic_cast<const backref_parser_t &>(**ptr);

    return int(r.number);
}

void v_peg_make_action_parser(parser_ptr_t *ret, const action_ptr_t *ptr)
{
    *ret = std::make_shared<const action_parser_t>(*ptr);
}

void v_peg_action_parser_get_action(const parser_ptr_t *ptr, action_ptr_t *action)
{
    auto &r = dynamic_cast<const action_parser_t &>(**ptr);

    *action = r.action;
}

void v_peg_make_literal_parser(parser_ptr_t *ret, const char *utf8)
{
    *ret = std::make_shared<const literal_parser_t>(utf8);
}

const char *v_peg_literal_parser_get_literal(const parser_ptr_t *ptr)
{
    auto &r = dynamic_cast<const literal_parser_t &>(**ptr);

    return r.utf8.c_str();
}

void v_peg_make_character_parser(parser_ptr_t *ret, char32_t ucs4)
{
    *ret = std::make_shared<const character_parser_t>(ucs4);
}

char32_t v_peg_character_parser_get_character(const parser_ptr_t *ptr)
{
    auto &r = dynamic_cast<const character_parser_t &>(**ptr);

    return r.ucs4;
}

void v_peg_make_class_parser(parser_ptr_t *ret, const char32_t (*ranges)[2], int count)
{
    *ret = std::make_shared<const class_parser_t>(ranges, size_t(count));
}

int v_peg_class_parser_get_ranges_count(const parser_ptr_t *ptr)
{
    auto &r = dynamic_cast<const class_parser_t &>(**ptr);

    return  int(r.ranges.size());
}

void v_peg_class_parser_get_ranges(const parser_ptr_t *ptr, char32_t (*ranges)[2])
{
    auto &r = dynamic_cast<const class_parser_t &>(**ptr);

    size_t count = r.ranges.size();

    for (size_t i=0; i<count; ++i)
    {
        ranges[i][0] = r.ranges[i][0];
        ranges[i][1] = r.ranges[i][1];
    }
}

void v_peg_make_dot_parser(parser_ptr_t *ret)
{
    static const auto p = std::make_shared<const dot_parser_t>();

    *ret = p;
}


//-----------------------------------------------------------------
void v_peg_make_call_action(action_ptr_t *ret, const char *fun, const argument_ptr_t *args, int count)
{
    *ret = std::make_shared<const call_action_t>(fun, args, size_t(count));
}

const char *v_peg_call_action_get_function_name(const action_ptr_t *ptr)
{
    auto &r = dynamic_cast<const call_action_t &>(**ptr);

    return  v_quark_to_string(r.q_fun);
}

int v_peg_call_action_get_arguments_count(const action_ptr_t *ptr)
{
    auto &r = dynamic_cast<const call_action_t &>(**ptr);

    return  int(r.args.size());
}

void v_peg_call_action_get_arguments(const action_ptr_t *ptr, argument_ptr_t *args)
{
    auto &r = dynamic_cast<const call_action_t &>(**ptr);

    size_t count = r.args.size();

    for (size_t i=0; i<count; ++i)
    {
        args[i] = r.args[i];
    }
}

void v_peg_make_return_action(action_ptr_t *ret, const argument_ptr_t *arg)
{
    *ret = std::make_shared<const return_action_t>(*arg);
}

void v_peg_return_action_get_argument(const action_ptr_t *ptr, argument_ptr_t *arg)
{
    auto &r = dynamic_cast<const return_action_t &>(**ptr);

    *arg = r.arg;
}


//-----------------------------------------------------------------
void v_peg_make_identifier_argument(argument_ptr_t *ret, const char *ident)
{
    *ret = std::make_shared<const identifier_argument_t>(ident);
}

const char *v_peg_identifier_argument_get_identifier(const argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const identifier_argument_t &>(**ptr);

    return  v_quark_to_string(r.q_ident);
}

void v_peg_make_backref_argument(argument_ptr_t *ret, int number, int kind)
{
    *ret = std::make_shared<const backref_argument_t>(size_t(number), backref_argument_t::b_kind_t(kind));
}

int v_peg_backref_argument_get_number(const argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const backref_argument_t &>(**ptr);

    return int(r.number);
}

int v_peg_backref_argument_get_kind(const argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const backref_argument_t &>(**ptr);

    return int(r.b_kind);
}

void v_peg_make_integer_argument(argument_ptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const integer_argument_t>(number);
}

intptr_t v_peg_integer_argument_get_number(const argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const integer_argument_t &>(**ptr);

    return r.number;
}

void v_peg_make_literal_argument(argument_ptr_t *ret, const char *utf8)
{
    *ret = std::make_shared<const literal_argument_t>(utf8);
}

const char *v_peg_literal_argument_get_literal(const argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const literal_argument_t &>(**ptr);

    return r.utf8.c_str();
}

void v_peg_make_character_argument(argument_ptr_t *ret, char32_t ucs4)
{
    *ret = std::make_shared<const character_argument_t>(ucs4);
}

char32_t v_peg_character_argument_get_character(const argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const character_argument_t &>(**ptr);

    return r.ucs4;
}


//-----------------------------------------------------------------
void v_peg_make_grammar(grammar_ptr_t *ret)
{
    *ret = std::make_shared<const grammar_t>();
}


//-----------------------------------------------------------------
void v_peg_grammar_get_parser(const grammar_ptr_t *ptr, const char *name, parser_ptr_t *parser, int *leftrec)
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

void v_peg_grammar_set_parser(grammar_ptr_t *dst, const grammar_ptr_t *src, const char *name, const parser_ptr_t *parser, int leftrec)
{
    auto grammar = (*src)->set_parser(name, *parser, leftrec);

    *dst = std::make_shared<const grammar_t>(grammar);
}


grammar_action_fun_t
v_peg_grammar_get_action(const grammar_ptr_t *ptr, const char *name)
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

void v_peg_grammar_set_action(grammar_ptr_t *dst, const grammar_ptr_t *src, const char *name, grammar_action_fun_t fun)
{
    auto grammar = (*src)->set_action(name, fun);

    *dst = std::make_shared<const grammar_t>(grammar);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


