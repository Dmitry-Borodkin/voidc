//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_ast.h"

#include "voidc_util.h"
#include "voidc_target.h"

#include <cassert>

#include <llvm-c/Analysis.h>


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//-----------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(ast_##name##_ptr_t, v_ast_initialize_##name##_impl) \
    VOIDC_DEFINE_DESTROY_IMPL(ast_##name##_ptr_t, v_ast_destroy_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(ast_##name##_ptr_t, v_ast_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(ast_##name##_ptr_t, v_ast_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(ast_##name##_ptr_t, v_ast_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(ast_##name##_ptr_t, v_ast_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(ast_##name##_ptr_t, v_ast_std_any_set_pointer_##name##_impl)

    DEF(base)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)
    DEF(generic_list)

#undef DEF


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_ast_make_unit(ast_unit_ptr_t *ret, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<const ast_unit_t>(*stmt_list, line, column);
}

void
v_ast_unit_get_stmt_list(const ast_unit_ptr_t *ptr, ast_stmt_list_ptr_t *list)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    *list = r.stmt_list;
}

int
v_ast_unit_get_line(const ast_unit_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    return r.line;
}

int
v_ast_unit_get_column(const ast_unit_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    return r.column;
}

//-----------------------------------------------------------------
void
v_ast_make_stmt_list_nil(ast_stmt_list_ptr_t *ret)
{
    *ret = std::make_shared<const ast_stmt_list_t>();
}

void
v_ast_make_stmt_list(ast_stmt_list_ptr_t *ret, const ast_stmt_list_ptr_t *list, const ast_stmt_ptr_t *stmt)
{
    *ret = std::make_shared<const ast_stmt_list_t>(*list, *stmt);
}

int
v_ast_stmt_list_get_size(const ast_stmt_list_ptr_t *ptr)
{
    return (*ptr)->data.size();
}

void
v_ast_stmt_list_get_stmt(const ast_stmt_list_ptr_t *ptr, int i, ast_stmt_ptr_t *ret)
{
    *ret = (*ptr)->data[i];
}

//-----------------------------------------------------------------
void
v_ast_make_stmt(ast_stmt_ptr_t *ret, const char *var, const ast_call_ptr_t *call)
{
    *ret = std::make_shared<const ast_stmt_t>(var, *call);
}

const char *
v_ast_stmt_get_var_name(const ast_stmt_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_stmt_t &>(**ptr);

    return  r.var_name.c_str();
}

void
v_ast_stmt_get_call(const ast_stmt_ptr_t *ptr, ast_call_ptr_t *call)
{
    auto &r = dynamic_cast<const ast_stmt_t &>(**ptr);

    *call = r.call;
}

//-----------------------------------------------------------------
void
v_ast_make_call(ast_call_ptr_t *ret, const char *fun, const ast_arg_list_ptr_t *list)
{
    *ret = std::make_shared<const ast_call_t>(fun, *list);
}

const char *
v_ast_call_get_fun_name(const ast_call_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_call_t &>(**ptr);

    return  r.fun_name.c_str();
}

void
v_ast_call_get_arg_list(const ast_call_ptr_t *ptr, ast_arg_list_ptr_t *list)
{
    auto &r = dynamic_cast<const ast_call_t &>(**ptr);

    *list = r.arg_list;
}

//-----------------------------------------------------------------
void
v_ast_make_arg_list(ast_arg_list_ptr_t *ret, const ast_argument_ptr_t *list, int count)
{
    *ret = std::make_shared<const ast_arg_list_t>(list, count);
}

int
v_ast_arg_list_get_count(const ast_arg_list_ptr_t *ptr)
{
    return  int((*ptr)->data.size());
}

void
v_ast_arg_list_get_args(const ast_arg_list_ptr_t *ptr, ast_argument_ptr_t *ret)
{
    std::copy((*ptr)->data.begin(), (*ptr)->data.end(), ret);
}

//-----------------------------------------------------------------
void
v_ast_make_arg_identifier(ast_argument_ptr_t *ret, const char *name)
{
    *ret = std::make_shared<const ast_arg_identifier_t>(name);
}

const char *
v_ast_arg_identifier_get_name(const ast_argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_arg_identifier_t &>(**ptr);

    return  r.name.c_str();
}

//-----------------------------------------------------------------
void
v_ast_make_arg_integer(ast_argument_ptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const ast_arg_integer_t>(number);
}

intptr_t
v_ast_arg_integer_get_number(const ast_argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_arg_integer_t &>(**ptr);

    return  r.number;
}

//-----------------------------------------------------------------
void
v_ast_make_arg_string(ast_argument_ptr_t *ret, const char *string)
{
    *ret = std::make_shared<const ast_arg_string_t>(string);
}

const char *
v_ast_arg_string_get_string(const ast_argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_arg_string_t &>(**ptr);

    return  r.string.c_str();
}

//-----------------------------------------------------------------
void
v_ast_make_arg_char(ast_argument_ptr_t *ret, char32_t c)
{
    *ret = std::make_shared<const ast_arg_char_t>(c);
}

char32_t
v_ast_arg_char_get_char(const ast_argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_arg_char_t &>(**ptr);

    return  r.c;
}


//-----------------------------------------------------------------
//- Generics ...
//-----------------------------------------------------------------
void
v_ast_make_generic(ast_generic_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_generic_t>(vtab, obj);
}

//-----------------------------------------------------------------
void
v_ast_make_unit_generic(ast_unit_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_unit_generic_t>(vtab, obj);
}

void
v_ast_make_stmt_generic(ast_stmt_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_stmt_generic_t>(vtab, obj);
}

void
v_ast_make_call_generic(ast_call_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_call_generic_t>(vtab, obj);
}

void
v_ast_make_argument_generic(ast_argument_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_argument_generic_t>(vtab, obj);
}

//-----------------------------------------------------------------
const ast_generic_vtable *
v_ast_generic_get_vtable(const ast_generic_ptr_t *ptr)
{
    return (*ptr)->vtable;
}

const void *
v_ast_generic_get_object(const ast_generic_ptr_t *ptr)
{
    return (*ptr)->object;
}


//-----------------------------------------------------------------
void
v_ast_make_generic_list_nil(ast_generic_list_ptr_t *ret, v_quark_t tag)
{
    *ret = std::make_shared<const ast_generic_list_t>(tag);
}

void
v_ast_make_generic_list(ast_generic_list_ptr_t *ret, v_quark_t tag, const ast_base_ptr_t *list, int count)
{
    *ret = std::make_shared<const ast_generic_list_t>(tag, list, count);
}

void
v_ast_generic_list_append(ast_generic_list_ptr_t *ret,
                          const ast_generic_list_ptr_t *list,
                          const ast_base_ptr_t *item)
{
    *ret = std::make_shared<const ast_generic_list_t>(*list, *item);
}

int
v_ast_generic_list_get_size(const ast_generic_list_ptr_t *ptr)
{
    return (*ptr)->data.size();
}

void
v_ast_generic_list_get_item(const ast_generic_list_ptr_t *ptr, int i, ast_base_ptr_t *ret)
{
    *ret = (*ptr)->data[i];
}

void
v_ast_generic_list_get_items(const ast_generic_list_ptr_t *ptr, ast_base_ptr_t *ret)
{
    std::copy((*ptr)->data.begin(), (*ptr)->data.end(), ret);
}


//-----------------------------------------------------------------
//- Visitors ...
//-----------------------------------------------------------------
#define DEF(type) \
v_quark_t v_##type##_visitor_method_tag;

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF


#define DEF(type) \
void \
v_visitor_set_method_##type(visitor_ptr_t *dst, const visitor_ptr_t *src, type::visitor_method_t method) \
{ \
    auto visitor = (*src)->set_void_method(v_##type##_visitor_method_tag, (void *)method); \
    *dst = std::make_shared<const voidc_visitor_t>(visitor); \
}

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF


//-----------------------------------------------------------------
void
v_ast_accept_visitor(const ast_base_ptr_t *object, const visitor_ptr_t *visitor)
{
    (*object)->accept(*visitor);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_ast_static_initialize(void)
{
    static_assert((sizeof(ast_base_ptr_t) % sizeof(intptr_t)) == 0);

    auto &gctx = *voidc_global_ctx_t::voidc;

    auto content_type = LLVMArrayType(gctx.intptr_t_type, sizeof(ast_base_ptr_t)/sizeof(intptr_t));

    auto gc = LLVMGetGlobalContext();

#define DEF(name) \
    static_assert(sizeof(ast_base_ptr_t) == sizeof(ast_##name##_ptr_t)); \
    auto opaque_##name##_ptr_type = LLVMStructCreateNamed(gc, "struct.v_ast_opaque_" #name "_ptr"); \
    LLVMStructSetBody(opaque_##name##_ptr_type, &content_type, 1, false); \
    gctx.add_symbol("v_ast_opaque_" #name "_ptr", gctx.LLVMOpaqueType_type, (void *)opaque_##name##_ptr_type);

    DEF(base)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)
    DEF(generic_list)

#undef DEF

#define DEF(type) \
    v_##type##_visitor_method_tag = v_quark_from_string(#type);

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF
}


//---------------------------------------------------------------------
void
v_ast_static_terminate(void)
{
}


