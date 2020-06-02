#include "voidc_ast.h"

#include "voidc_util.h"

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


//----------------------------------------------------------------------
extern "C"
{

//----------------------------------------------------------------------
//- ...
//----------------------------------------------------------------------
static
void v_ast_make_unit(ast_unit_ptr_t *ret, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<ast_unit_t>(*stmt_list, line, column);
}

static
void v_ast_unit_get_stmt_list(const ast_unit_ptr_t *ptr, ast_stmt_list_ptr_t *list)
{
    auto pp = std::dynamic_pointer_cast<const ast_unit_t>(*ptr);
    assert(pp);

    *list = pp->stmt_list;
}

static
int v_ast_unit_get_line(const ast_unit_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_unit_t>(*ptr);
    assert(pp);

    return pp->line;
}

static
int v_ast_unit_get_column(const ast_unit_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_unit_t>(*ptr);
    assert(pp);

    return pp->column;
}

//------------------------------------------------------------------
static
void v_ast_make_stmt_list(ast_stmt_list_ptr_t *ret, const ast_stmt_list_ptr_t *list, const ast_stmt_ptr_t *stmt)
{
    *ret = std::make_shared<ast_stmt_list_t>(*list, *stmt);
}

static
int v_ast_stmt_list_get_size(const ast_stmt_list_ptr_t *ptr)
{
    return (*ptr)->data.size();
}

static
void v_ast_stmt_list_get_stmt(const ast_stmt_list_ptr_t *ptr, int i, ast_stmt_ptr_t *ret)
{
    *ret = (*ptr)->data[i];
}

//------------------------------------------------------------------
static
void v_ast_make_stmt(ast_stmt_ptr_t *ret, const std::string *var, const ast_call_ptr_t *call)
{
    *ret = std::make_shared<ast_stmt_t>(*var, *call);
}

static
void v_ast_stmt_get_var_name(const ast_stmt_ptr_t *ptr, std::string *var)
{
    auto pp = std::dynamic_pointer_cast<const ast_stmt_t>(*ptr);
    assert(pp);

    *var = pp->var_name;
}

static
void v_ast_stmt_get_call(const ast_stmt_ptr_t *ptr, ast_call_ptr_t *call)
{
    auto pp = std::dynamic_pointer_cast<const ast_stmt_t>(*ptr);
    assert(pp);

    *call = pp->call;
}

//------------------------------------------------------------------
static
void v_ast_make_call(ast_call_ptr_t *ret, const std::string *fun, const ast_arg_list_ptr_t *list)
{
    *ret = std::make_shared<ast_call_t>(*fun, *list);
}

static
void v_ast_call_get_fun_name(const ast_call_ptr_t *ptr, std::string *fun)
{
    auto pp = std::dynamic_pointer_cast<const ast_call_t>(*ptr);
    assert(pp);

    *fun = pp->fun_name;
}

static
void v_ast_call_get_arg_list(const ast_call_ptr_t *ptr, ast_arg_list_ptr_t *list)
{
    auto pp = std::dynamic_pointer_cast<const ast_call_t>(*ptr);
    assert(pp);

    *list = pp->arg_list;
}

//------------------------------------------------------------------
static
void v_ast_make_arg_list(ast_arg_list_ptr_t *ret, const ast_argument_ptr_t *list, int count)
{
    *ret = std::make_shared<ast_arg_list_t>(list, count);
}

static
int v_ast_arg_list_get_count(const ast_arg_list_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_list_t>(*ptr);
    assert(pp);

    return  int(pp->data.size());
}

static
void v_ast_arg_list_get_args(const ast_arg_list_ptr_t *ptr, ast_argument_ptr_t *ret)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_list_t>(*ptr);
    assert(pp);

    std::copy(pp->data.begin(), pp->data.end(), ret);
}

//------------------------------------------------------------------
static
void v_ast_make_arg_identifier(ast_argument_ptr_t *ret, const std::string *name)
{
    *ret = std::make_shared<ast_arg_identifier_t>(*name);
}

static
void v_ast_arg_identifier_get_name(const ast_argument_ptr_t *ptr, std::string *name)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_identifier_t>(*ptr);
    assert(pp);

    *name = pp->name;
}

//------------------------------------------------------------------
static
void v_ast_make_arg_integer(ast_argument_ptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<ast_arg_integer_t>(number);
}

static
intptr_t v_ast_arg_integer_get_number(const ast_argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_integer_t>(*ptr);
    assert(pp);

    return  pp->number;
}

//------------------------------------------------------------------
static
void v_ast_make_arg_string(ast_argument_ptr_t *ret, const std::string *string)
{
    *ret = std::make_shared<ast_arg_string_t>(*string);
}

static
void v_ast_arg_string_get_string(const ast_argument_ptr_t *ptr, std::string *string)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_string_t>(*ptr);
    assert(pp);

    *string = pp->string;
}

//------------------------------------------------------------------
static
void v_ast_make_arg_char(ast_argument_ptr_t *ret, char32_t c)
{
    *ret = std::make_shared<ast_arg_char_t>(c);
}

static
char32_t v_ast_arg_char_get_char(const ast_argument_ptr_t *ptr)
{
    auto pp = std::dynamic_pointer_cast<const ast_arg_char_t>(*ptr);
    assert(pp);

    return  pp->c;
}


//----------------------------------------------------------------------
}   //- extern "C"


//----------------------------------------------------------------------
LLVMTypeRef ast_opaque_unit_ptr_type;
LLVMTypeRef ast_unit_ref_type;

LLVMTypeRef ast_opaque_stmt_list_ptr_type;
LLVMTypeRef ast_stmt_list_ref_type;

LLVMTypeRef ast_opaque_stmt_ptr_type;
LLVMTypeRef ast_stmt_ref_type;

LLVMTypeRef ast_opaque_call_ptr_type;
LLVMTypeRef ast_call_ref_type;

LLVMTypeRef ast_opaque_arg_list_ptr_type;
LLVMTypeRef ast_arg_list_ref_type;

LLVMTypeRef ast_opaque_argument_ptr_type;
LLVMTypeRef ast_argument_ref_type;


//----------------------------------------------------------------------
void v_ast_static_initialize(void)
{
    static_assert(sizeof(ast_unit_ptr_t) == sizeof(ast_stmt_list_ptr_t));
    static_assert(sizeof(ast_unit_ptr_t) == sizeof(ast_stmt_ptr_t));
    static_assert(sizeof(ast_unit_ptr_t) == sizeof(ast_call_ptr_t));
    static_assert(sizeof(ast_unit_ptr_t) == sizeof(ast_arg_list_ptr_t));
    static_assert(sizeof(ast_unit_ptr_t) == sizeof(ast_argument_ptr_t));

    auto char_ptr_type = LLVMPointerType(compile_ctx_t::char_type, 0);

    auto content_type = LLVMArrayType(char_ptr_type, sizeof(ast_unit_ptr_t)/sizeof(char *));

    auto gctx = LLVMGetGlobalContext();

#define DEF(name) \
    ast_opaque_##name##_ptr_type = LLVMStructCreateNamed(gctx, "struct.v_ast_opaque_" #name "_ptr"); \
    LLVMStructSetBody(ast_opaque_##name##_ptr_type, &content_type, 1, false); \
    ast_##name##_ref_type = LLVMPointerType(ast_opaque_##name##_ptr_type, 0); \
\
    v_add_symbol("v_ast_opaque_" #name "_ptr", compile_ctx_t::LLVMOpaqueType_type, (void *)ast_opaque_##name##_ptr_type); \
    v_add_symbol("v_ast_" #name "_ref", compile_ctx_t::LLVMOpaqueType_type, (void *)ast_##name##_ref_type); \
\
    utility::register_initialize_impl<ast_##name##_ptr_t>(ast_opaque_##name##_ptr_type, \
                                                          "v_ast_initialize_" #name "_impl"); \
\
    utility::register_reset_impl<ast_##name##_ptr_t>(ast_opaque_##name##_ptr_type, \
                                                     "v_ast_reset_" #name "_impl"); \
\
    utility::register_copy_impl<ast_##name##_ptr_t>(ast_opaque_##name##_ptr_type, \
                                                    "v_ast_copy_" #name "_impl"); \
\
    utility::register_move_impl<ast_##name##_ptr_t>(ast_opaque_##name##_ptr_type, \
                                                    "v_ast_move_" #name "_impl"); \
\
    utility::register_std_any_get_pointer_impl<ast_##name##_ptr_t>(ast_opaque_##name##_ptr_type, \
                                                                   "v_ast_std_any_get_pointer_" #name "_impl"); \
\
    utility::register_std_any_set_pointer_impl<ast_##name##_ptr_t>(ast_opaque_##name##_ptr_type, \
                                                                   "v_ast_std_any_set_pointer_" #name "_impl");

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

#undef DEF

    LLVMTypeRef args[4];

#define DEF(name, ret, num) \
    v_add_symbol(#name, LLVMFunctionType(ret, args, num, false), (void *)name);

    args[0] = ast_unit_ref_type;
    args[1] = ast_stmt_list_ref_type;
    args[2] = compile_ctx_t::int_type;
    args[3] = compile_ctx_t::int_type;

    DEF(v_ast_make_unit, compile_ctx_t::void_type, 4);
    DEF(v_ast_unit_get_stmt_list, compile_ctx_t::void_type, 2);
    DEF(v_ast_unit_get_line, compile_ctx_t::int_type, 1);
    DEF(v_ast_unit_get_column, compile_ctx_t::int_type, 1);

    args[0] = ast_stmt_list_ref_type;
    args[1] = ast_stmt_list_ref_type;
    args[2] = ast_stmt_ref_type;

    DEF(v_ast_make_stmt_list, compile_ctx_t::void_type, 3);
    DEF(v_ast_stmt_list_get_size, compile_ctx_t::int_type, 1);

    args[1] = compile_ctx_t::int_type;
    args[2] = ast_stmt_ref_type;

    DEF(v_ast_stmt_list_get_stmt, compile_ctx_t::void_type, 3);

    args[0] = ast_stmt_ref_type;
    args[1] = utility::std_string_ref_type;
    args[2] = ast_call_ref_type;

    DEF(v_ast_make_stmt, compile_ctx_t::void_type, 3);
    DEF(v_ast_stmt_get_var_name, compile_ctx_t::void_type, 2);

    args[1] = ast_call_ref_type;

    DEF(v_ast_stmt_get_call, compile_ctx_t::void_type, 2);

    args[0] = ast_call_ref_type;
    args[1] = utility::std_string_ref_type;
    args[2] = ast_arg_list_ref_type;

    DEF(v_ast_make_call, compile_ctx_t::void_type, 3);
    DEF(v_ast_call_get_fun_name, compile_ctx_t::void_type, 2);

    args[1] = ast_arg_list_ref_type;

    DEF(v_ast_call_get_arg_list, compile_ctx_t::void_type, 2);

    args[0] = ast_arg_list_ref_type;
    args[1] = ast_argument_ref_type;
    args[2] = compile_ctx_t::int_type;

    DEF(v_ast_make_arg_list, compile_ctx_t::void_type, 3);
    DEF(v_ast_arg_list_get_count, compile_ctx_t::int_type, 1);
    DEF(v_ast_arg_list_get_args, compile_ctx_t::void_type, 2);

    args[0] = ast_argument_ref_type;
    args[1] = utility::std_string_ref_type;

    DEF(v_ast_make_arg_identifier, compile_ctx_t::void_type, 2);
    DEF(v_ast_arg_identifier_get_name, compile_ctx_t::void_type, 2);

    args[1] = compile_ctx_t::intptr_t_type;

    DEF(v_ast_make_arg_integer, compile_ctx_t::void_type, 2);
    DEF(v_ast_arg_integer_get_number, compile_ctx_t::intptr_t_type, 1);

    args[1] = utility::std_string_ref_type;

    DEF(v_ast_make_arg_string, compile_ctx_t::void_type, 2);
    DEF(v_ast_arg_string_get_string, compile_ctx_t::void_type, 2);

    args[1] = compile_ctx_t::char32_t_type;

    DEF(v_ast_make_arg_char, compile_ctx_t::void_type, 2);
    DEF(v_ast_arg_char_get_char, compile_ctx_t::char32_t_type, 1);


    //- ...


#undef DEF

}


//----------------------------------------------------------------------
void v_ast_static_terminate(void)
{
}


