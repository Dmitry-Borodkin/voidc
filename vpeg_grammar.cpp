#include "vpeg_grammar.h"

#include "vpeg_context.h"

#include "voidc_llvm.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
void grammar_t::check_hash(void)
{
    if (hash != size_t(-1)) return;

    static size_t static_hash = 0;

    _hash = static_hash++ ;

//  printf("hash: %d\n", int(hash));
}


std::any grammar_t::parse(const std::string &name, context_t &ctx) const
{
//  printf("(%d): %s ?\n", (int)ctx.get_position(), symbol.c_str());

    context_t::variables_t saved_vars;      //- empty(!)

    std::swap(ctx.variables, saved_vars);       //- save (and clear)

    auto st = ctx.get_state();

    {   auto &svec = ctx.variables.strings;

        svec = svec.push_back({st.position, 0});    //- #0

        st.variables.strings = svec;        //- Sic!
    }

    std::any ret;

    assert(hash != size_t(-1));

    auto key = std::make_tuple(hash, st.position, name);

    if (ctx.memo.count(key) != 0)
    {
        auto &respos = ctx.memo[key];

        ctx.set_state(respos.second);

        ret = respos.first;
    }
    else
    {
        auto &pl = parsers.at(name);

        if (pl.second)      //- Left-recursive ?
        {
            auto lastres = std::any();
            auto last_st = st;

            ctx.memo[key] = {lastres, st};

            for(;;)
            {
                ctx.set_state(st);

                auto res = pl.first->parse(ctx);
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
            auto res = pl.first->parse(ctx);
            auto est = ctx.get_state();

            ctx.memo[key] = {res, est};

            ret = res;
        }
    }

//    if (ret.has_value())
//    {
//        printf("(%d,%d): %p %s\n", (int)st.position, (int)ctx.get_position(), &saved_vars, symbol.c_str());
//    }

    ctx.variables = saved_vars;                 //- restore saved

    return ret;
}




extern "C" {


//-----------------------------------------------------------------
static
void v_peg_initialize_parser_ptrs(parser_ptr_t *ptr, int count)
{
    std::uninitialized_default_construct_n(ptr, size_t(count));
}

static
void v_peg_initialize_action_ptrs(action_ptr_t *ptr, int count)
{
    std::uninitialized_default_construct_n(ptr, size_t(count));
}

static
void v_peg_initialize_argument_ptrs(argument_ptr_t *ptr, int count)
{
    std::uninitialized_default_construct_n(ptr, size_t(count));
}

static
void v_peg_initialize_grammar_ptrs(grammar_ptr_t *ptr, int count)
{
    std::uninitialized_default_construct_n(ptr, size_t(count));
}

//-----------------------------------------------------------------
static
void v_peg_reset_parser_ptrs(parser_ptr_t *ptr, int count)
{
    for (int i=0; i<count; ++i) ptr[i].reset();
}

static
void v_peg_reset_action_ptrs(action_ptr_t *ptr, int count)
{
    for (int i=0; i<count; ++i) ptr[i].reset();
}

static
void v_peg_reset_argument_ptrs(argument_ptr_t *ptr, int count)
{
    for (int i=0; i<count; ++i) ptr[i].reset();
}

static
void v_peg_reset_grammar_ptrs(grammar_ptr_t *ptr, int count)
{
    for (int i=0; i<count; ++i) ptr[i].reset();
}

//-----------------------------------------------------------------
static
void v_peg_copy_parser_ptr(const parser_ptr_t *src, parser_ptr_t *dst)
{
    *dst = *src;
}

static
void v_peg_copy_action_ptr(const action_ptr_t *src, action_ptr_t *dst)
{
    *dst = *src;
}

static
void v_peg_copy_argument_ptr(const argument_ptr_t *src, argument_ptr_t *dst)
{
    *dst = *src;
}

static
void v_peg_copy_grammar_ptr(const grammar_ptr_t *src, grammar_ptr_t *dst)
{
    *dst = *src;
}

//-----------------------------------------------------------------
static
int v_peg_parser_get_kind(const parser_ptr_t *ptr)
{
    return (*ptr)->kind();
}

static
int v_peg_action_get_kind(const action_ptr_t *ptr)
{
    return (*ptr)->kind();
}

static
int v_peg_argument_get_kind(const argument_ptr_t *ptr)
{
    return (*ptr)->kind();
}


//-----------------------------------------------------------------
static
int v_peg_parser_get_parsers_count(const parser_ptr_t *ptr)
{
    return int((*ptr)->parsers_count());
}

static
void v_peg_parser_get_parsers(const parser_ptr_t *ptr, parser_ptr_t *list)
{
    (*ptr)->get_parsers(list);
}

//-----------------------------------------------------------------
static
void v_peg_make_choice_parser(parser_ptr_t *ret, const parser_ptr_t *list, int count)
{
    *ret = std::make_shared<choice_parser_t>(list, size_t(count));
}

static
void v_peg_make_sequence_parser(parser_ptr_t *ret, const parser_ptr_t *list, int count)
{
    *ret = std::make_shared<sequence_parser_t>(list, size_t(count));
}


static
void v_peg_make_and_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<and_parser_t>(*ptr);
}

static
void v_peg_make_not_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<not_parser_t>(*ptr);
}

static
void v_peg_make_question_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<question_parser_t>(*ptr);
}

static
void v_peg_make_star_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<star_parser_t>(*ptr);
}

static
void v_peg_make_plus_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<plus_parser_t>(*ptr);
}

static
void v_peg_make_catch_variable_parser(parser_ptr_t *ret, const char *name, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<catch_variable_parser_t>(name, *ptr);
}

static
const char *v_peg_catch_variable_parser_get_name(const parser_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const catch_variable_parser_t>(*ptr);

    assert(pp);

    return pp->name.c_str();
}

static
void v_peg_make_catch_string_parser(parser_ptr_t *ret, const parser_ptr_t *ptr)
{
    *ret = std::make_shared<catch_string_parser_t>(*ptr);
}


static
void v_peg_make_identifier_parser(parser_ptr_t *ret, const char *ident)
{
    *ret = std::make_shared<identifier_parser_t>(ident);
}

static
const char *v_peg_identifier_parser_get_identifier(const parser_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const identifier_parser_t>(*ptr);

    assert(pp);

    return pp->ident.c_str();
}

static
void v_peg_make_backref_parser(parser_ptr_t *ret, int number)
{
    *ret = std::make_shared<backref_parser_t>(size_t(number));
}

static
int v_peg_backref_parser_get_number(const parser_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const backref_parser_t>(*ptr);

    assert(pp);

    return int(pp->number);
}

static
void v_peg_make_action_parser(parser_ptr_t *ret, const action_ptr_t *ptr)
{
    *ret = std::make_shared<action_parser_t>(*ptr);
}

static
void v_peg_action_parser_get_action(const parser_ptr_t *ptr, action_ptr_t *action)
{
    auto pp = std::dynamic_pointer_cast<const action_parser_t>(*ptr);

    assert(pp);

    *action = pp->action;
}

static
void v_peg_make_literal_parser(parser_ptr_t *ret, const char *utf8)
{
    *ret = std::make_shared<literal_parser_t>(utf8);
}

static
const char *v_peg_literal_parser_get_literal(const parser_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const literal_parser_t>(*ptr);

    assert(pp);

    return pp->utf8.c_str();
}

static
void v_peg_make_character_parser(parser_ptr_t *ret, char32_t ucs4)
{
    *ret = std::make_shared<character_parser_t>(ucs4);
}

static
char32_t v_peg_character_parser_get_character(const parser_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const character_parser_t>(*ptr);

    assert(pp);

    return pp->ucs4;
}

static
void v_peg_make_class_parser(parser_ptr_t *ret, const char32_t (*ranges)[2], int count)
{
    *ret = std::make_shared<class_parser_t>(ranges, size_t(count));
}

static
int v_peg_class_parser_get_ranges_count(const parser_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const class_parser_t>(*ptr);

    assert(pp);

    return  int(pp->ranges.size());
}

static
void v_peg_class_parser_get_ranges(const parser_ptr_t *ptr, char32_t (*ranges)[2])
{
    auto pp = std::dynamic_pointer_cast<const class_parser_t>(*ptr);

    assert(pp);

    size_t count = pp->ranges.size();

    for (size_t i=0; i<count; ++i)
    {
        ranges[i][0] = pp->ranges[i][0];
        ranges[i][1] = pp->ranges[i][1];
    }
}

static
void v_peg_make_dot_parser(parser_ptr_t *ret)
{
    static const auto p = std::make_shared<dot_parser_t>();

    *ret = p;
}


//---------------------------------------------------------------------
static
void v_peg_make_call_action(action_ptr_t *ret, const char *fun, const argument_ptr_t *args, int count)
{
    *ret = std::make_shared<call_action_t>(fun, args, size_t(count));
}

static
const char *v_peg_call_action_get_function_name(const action_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const call_action_t>(*ptr);

    assert(pp);

    return pp->fun.c_str();
}

static
int v_peg_call_action_get_arguments_count(const action_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const call_action_t>(*ptr);

    assert(pp);

    return  int(pp->args.size());
}

static
void v_peg_call_action_get_arguments(const action_ptr_t *ptr, argument_ptr_t *args)
{
    auto pp = std::dynamic_pointer_cast<const call_action_t>(*ptr);

    assert(pp);

    size_t count = pp->args.size();

    for (size_t i=0; i<count; ++i)
    {
        args[i] = pp->args[i];
    }
}

static
void v_peg_make_return_action(action_ptr_t *ret, const argument_ptr_t *arg)
{
    *ret = std::make_shared<return_action_t>(*arg);
}

static
void v_peg_return_action_get_argument(const action_ptr_t *ptr, argument_ptr_t *arg)
{
    auto pp = std::dynamic_pointer_cast<const return_action_t>(*ptr);

    assert(pp);

    *arg = pp->arg;
}


//---------------------------------------------------------------------
static
void v_peg_make_identifier_argument(argument_ptr_t *ret, const char *ident)
{
    *ret = std::make_shared<identifier_argument_t>(ident);
}

static
const char *v_peg_identifier_argument_get_identifier(const argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const identifier_argument_t>(*ptr);

    assert(pp);

    return pp->ident.c_str();
}

static
void v_peg_make_backref_argument(argument_ptr_t *ret, int number, int kind)
{
    *ret = std::make_shared<backref_argument_t>(size_t(number), backref_argument_t::b_kind_t(kind));
}

static
int v_peg_backref_argument_get_number(const argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const backref_argument_t>(*ptr);

    assert(pp);

    return int(pp->number);
}

static
int v_peg_backref_argument_get_kind(const argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const backref_argument_t>(*ptr);

    assert(pp);

    return int(pp->b_kind);
}

static
void v_peg_make_integer_argument(argument_ptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<integer_argument_t>(number);
}

static
intptr_t v_peg_integer_argument_get_number(const argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const integer_argument_t>(*ptr);

    assert(pp);

    return pp->number;
}

static
void v_peg_make_literal_argument(argument_ptr_t *ret, const char *utf8)
{
    *ret = std::make_shared<literal_argument_t>(utf8);
}

static
const char *v_peg_literal_argument_get_literal(const argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const literal_argument_t>(*ptr);

    assert(pp);

    return pp->utf8.c_str();
}

static
void v_peg_make_character_argument(argument_ptr_t *ret, char32_t ucs4)
{
    *ret = std::make_shared<character_argument_t>(ucs4);
}

static
char32_t v_peg_character_argument_get_character(const argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const character_argument_t>(*ptr);

    assert(pp);

    return pp->ucs4;
}


//---------------------------------------------------------------------
static
void v_peg_grammar_get_parser(const grammar_ptr_t *ptr, const char *name, parser_ptr_t *parser, int *leftrec)
{
    auto &pair = (*ptr)->parsers[name];

    if (parser)   *parser  = pair.first;
    if (leftrec)  *leftrec = pair.second;
}

static
void v_peg_grammar_set_parser(grammar_ptr_t *dst, const grammar_ptr_t *src, const char *name, const parser_ptr_t *parser, int leftrec)
{
    auto grammar = (*src)->set_parser(name, *parser, leftrec);

    *dst = std::make_shared<const grammar_t>(grammar);
}




}


//-----------------------------------------------------------------
void grammar_t::static_initialize(void)
{
    assert(sizeof(parser_ptr_t) == sizeof(action_ptr_t));
    assert(sizeof(parser_ptr_t) == sizeof(argument_ptr_t));

    auto char_ptr_type = LLVMPointerType(compile_ctx_t::char_type, 0);

    auto content_type = LLVMArrayType(char_ptr_type, sizeof(parser_ptr_t)/sizeof(char *));

    auto gctx = LLVMGetGlobalContext();

    LLVMTypeRef args[5];

#define DEF(name) \
    auto opaque_##name##_ptr_type = LLVMStructCreateNamed(gctx, "struct.v_peg_opaque_" #name "_ptr"); \
    LLVMStructSetBody(opaque_##name##_ptr_type,   &content_type, 1, false); \
    auto name##_ref_type = LLVMPointerType(opaque_##name##_ptr_type,   0); \
\
    compile_ctx_t::symbol_types["v_peg_opaque_" #name "_ptr"] = compile_ctx_t::LLVMOpaqueType_type; \
    LLVMAddSymbol("v_peg_opaque_" #name "_ptr", (void *)opaque_##name##_ptr_type); \
\
    compile_ctx_t::symbol_types["v_peg_" #name "_ref"] = compile_ctx_t::LLVMOpaqueType_type; \
    LLVMAddSymbol("v_peg_" #name "_ref", (void *)name##_ref_type); \
\
    args[0] = name##_ref_type; \
    args[1] = compile_ctx_t::int_type; \
\
    compile_ctx_t::symbol_types["v_peg_initialize_" #name "_ptrs"] = LLVMFunctionType(compile_ctx_t::void_type, args, 2, false); \
    LLVMAddSymbol("v_peg_initialize_" #name "_ptrs", (void *)v_peg_initialize_##name##_ptrs); \
\
    compile_ctx_t::symbol_types["v_peg_reset_" #name "_ptrs"] = LLVMFunctionType(compile_ctx_t::void_type, args, 2, false); \
    LLVMAddSymbol("v_peg_reset_" #name "_ptrs", (void *)v_peg_reset_##name##_ptrs); \
\
    args[1] = name##_ref_type; \
\
    compile_ctx_t::symbol_types["v_peg_copy_" #name "_ptr"] = LLVMFunctionType(compile_ctx_t::void_type, args, 2, false); \
    LLVMAddSymbol("v_peg_copy_" #name "_ptr", (void *)v_peg_copy_##name##_ptr);

    DEF(parser)
    DEF(action)
    DEF(argument)
    DEF(grammar)

#undef DEF

#define DEF(name) \
    compile_ctx_t::symbol_types["v_peg_" #name "_kind"] = compile_ctx_t::LLVMOpaqueType_type; \
    LLVMAddSymbol("v_peg_" #name "_kind", (void *)compile_ctx_t::int_type); \
\
    compile_ctx_t::symbol_types["v_peg_" #name "_get_kind"] = \
        LLVMFunctionType(compile_ctx_t::int_type, &name##_ref_type, 1, false); \
\
    LLVMAddSymbol("v_peg_" #name "_get_kind", (void *)v_peg_##name##_get_kind);

    DEF(parser)
    DEF(action)
    DEF(argument)

#undef DEF

#define DEF(name, kind) \
    compile_ctx_t::constants["v_peg_" #name "_kind_" #kind] = LLVMConstInt(compile_ctx_t::int_type, name##_t::k_##kind, 0);

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
    compile_ctx_t::constants["v_peg_backref_argument_kind_" #kind] = \
        LLVMConstInt(compile_ctx_t::int_type, backref_argument_t::bk_##kind, 0);

    DEF(string)
    DEF(start)
    DEF(end)

#undef DEF

#define DEF(name, ret, num) \
    compile_ctx_t::symbol_types[#name] = LLVMFunctionType(ret, args, num, false); \
    LLVMAddSymbol(#name, (void *)name);

    args[0] = parser_ref_type;

    DEF(v_peg_parser_get_parsers_count, compile_ctx_t::int_type, 1);

    args[1] = parser_ref_type;

    DEF(v_peg_parser_get_parsers, compile_ctx_t::void_type, 2);

    args[2] = compile_ctx_t::int_type;

    DEF(v_peg_make_choice_parser, compile_ctx_t::void_type, 3);
    DEF(v_peg_make_sequence_parser, compile_ctx_t::void_type, 3);

    DEF(v_peg_make_and_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_make_not_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_make_question_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_make_star_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_make_plus_parser, compile_ctx_t::void_type, 2);

    args[1] = char_ptr_type;
    args[2] = parser_ref_type;

    DEF(v_peg_make_catch_variable_parser, compile_ctx_t::void_type, 3);
    DEF(v_peg_catch_variable_parser_get_name, char_ptr_type, 1);

    args[1] = parser_ref_type;

    DEF(v_peg_make_catch_string_parser, compile_ctx_t::void_type, 2);

    args[1] = char_ptr_type;

    DEF(v_peg_make_identifier_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_identifier_parser_get_identifier, char_ptr_type, 1);

    args[1] = compile_ctx_t::int_type;

    DEF(v_peg_make_backref_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_backref_parser_get_number, compile_ctx_t::int_type, 1);

    args[1] = action_ref_type;

    DEF(v_peg_make_action_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_action_parser_get_action, compile_ctx_t::void_type, 2);

    args[1] = char_ptr_type;

    DEF(v_peg_make_literal_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_literal_parser_get_literal, char_ptr_type, 1);

    args[1] = compile_ctx_t::char32_t_type;

    DEF(v_peg_make_character_parser, compile_ctx_t::void_type, 2);
    DEF(v_peg_character_parser_get_character, compile_ctx_t::char32_t_type, 1);

    auto range_type = LLVMArrayType(compile_ctx_t::char32_t_type, 2);

    auto range_ptr_type = LLVMPointerType(range_type, 0);

    args[1] = range_ptr_type;
    args[2] = compile_ctx_t::int_type;

    DEF(v_peg_make_class_parser, compile_ctx_t::void_type, 3);
    DEF(v_peg_class_parser_get_ranges_count, compile_ctx_t::int_type, 1);
    DEF(v_peg_class_parser_get_ranges, compile_ctx_t::void_type, 2);

    DEF(v_peg_make_dot_parser, compile_ctx_t::void_type, 1);

    args[0] = action_ref_type;
    args[1] = char_ptr_type;
    args[2] = argument_ref_type;
    args[3] = compile_ctx_t::int_type;

    DEF(v_peg_make_call_action, compile_ctx_t::void_type, 4);
    DEF(v_peg_call_action_get_function_name, char_ptr_type, 1);
    DEF(v_peg_call_action_get_arguments_count, compile_ctx_t::int_type, 1);

    args[1] = argument_ref_type;

    DEF(v_peg_call_action_get_arguments, compile_ctx_t::void_type, 2);

    DEF(v_peg_make_return_action, compile_ctx_t::void_type, 2);
    DEF(v_peg_return_action_get_argument, compile_ctx_t::void_type, 2);

    args[0] = argument_ref_type;
    args[1] = char_ptr_type;

    DEF(v_peg_make_identifier_argument, compile_ctx_t::void_type, 2);
    DEF(v_peg_identifier_argument_get_identifier, char_ptr_type, 1);

    args[1] = compile_ctx_t::int_type;
    args[2] = compile_ctx_t::int_type;

    DEF(v_peg_make_backref_argument, compile_ctx_t::void_type, 3);
    DEF(v_peg_backref_argument_get_number, compile_ctx_t::int_type, 1);
    DEF(v_peg_backref_argument_get_kind, compile_ctx_t::int_type, 1);

    args[1] = compile_ctx_t::intptr_t_type;

    DEF(v_peg_make_integer_argument, compile_ctx_t::void_type, 2);
    DEF(v_peg_integer_argument_get_number, compile_ctx_t::intptr_t_type, 1);

    args[1] = char_ptr_type;

    DEF(v_peg_make_literal_argument, compile_ctx_t::void_type, 2);
    DEF(v_peg_literal_argument_get_literal, char_ptr_type, 1);

    args[1] = compile_ctx_t::char32_t_type;

    DEF(v_peg_make_character_argument, compile_ctx_t::void_type, 2);
    DEF(v_peg_character_argument_get_character, compile_ctx_t::char32_t_type, 1);

    args[0] = grammar_ref_type;
    args[1] = char_ptr_type;
    args[2] = parser_ref_type;
    args[3] = LLVMPointerType(compile_ctx_t::int_type, 0);

    DEF(v_peg_grammar_get_parser, compile_ctx_t::void_type, 4);

    args[1] = grammar_ref_type;
    args[2] = char_ptr_type;
    args[3] = parser_ref_type;
    args[4] = compile_ctx_t::int_type;

    DEF(v_peg_grammar_set_parser, compile_ctx_t::void_type, 5);


#undef DEF











}

//-----------------------------------------------------------------
void grammar_t::static_terminate(void)
{
}


//---------------------------------------------------------------------
}   //- namespace vpeg


