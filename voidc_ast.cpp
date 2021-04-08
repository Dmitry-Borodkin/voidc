//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_ast.h"

#include "voidc_util.h"
#include "voidc_target.h"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(ast_##name##_sptr_t, v_ast_initialize_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(ast_##name##_sptr_t, v_ast_terminate_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(ast_##name##_sptr_t, v_ast_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(ast_##name##_sptr_t, v_ast_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(ast_##name##_sptr_t, v_ast_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(ast_##name##_sptr_t, v_ast_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(ast_##name##_sptr_t, v_ast_std_any_set_pointer_##name##_impl)

    DEF(base)

    DEF(unit)

    DEF(stmt)
    DEF(stmt_list)

    DEF(expr)
    DEF(expr_list)

    DEF(generic)
    DEF(generic_list)

#undef DEF


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_ast_make_unit(ast_unit_sptr_t *ret, const ast_stmt_list_sptr_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<const ast_unit_t>(*stmt_list, line, column);
}

void
v_ast_unit_get_stmt_list(const ast_unit_sptr_t *ptr, ast_stmt_list_sptr_t *list)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    *list = r.stmt_list;
}

int
v_ast_unit_get_line(const ast_unit_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    return r.line;
}

int
v_ast_unit_get_column(const ast_unit_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    return r.column;
}

//---------------------------------------------------------------------
void
v_ast_make_stmt(ast_stmt_sptr_t *ret, const char *var, const ast_expr_sptr_t *expr)
{
    *ret = std::make_shared<const ast_stmt_t>(var, *expr);
}

const char *
v_ast_stmt_get_name(const ast_stmt_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_stmt_t &>(**ptr);

    return  r.name.c_str();
}

void
v_ast_stmt_get_expr(const ast_stmt_sptr_t *ptr, ast_expr_sptr_t *call)
{
    auto &r = dynamic_cast<const ast_stmt_t &>(**ptr);

    *call = r.expr;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_call(ast_expr_sptr_t *ret, const ast_expr_sptr_t *fun, const ast_expr_list_sptr_t *list)
{
    *ret = std::make_shared<const ast_expr_call_t>(*fun, *list);
}

void
v_ast_expr_call_get_fun_expr(const ast_expr_sptr_t *ptr, ast_expr_sptr_t *fun)
{
    auto &r = dynamic_cast<const ast_expr_call_t &>(**ptr);

    *fun = r.fun_expr;
}

void
v_ast_expr_call_get_arg_list(const ast_expr_sptr_t *ptr, ast_expr_list_sptr_t *list)
{
    auto &r = dynamic_cast<const ast_expr_call_t &>(**ptr);

    *list = r.arg_list;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_identifier(ast_expr_sptr_t *ret, const char *name)
{
    *ret = std::make_shared<const ast_expr_identifier_t>(name);
}

const char *
v_ast_expr_identifier_get_name(const ast_expr_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_identifier_t &>(**ptr);

    return  r.name.c_str();
}

//---------------------------------------------------------------------
void
v_ast_make_expr_integer(ast_expr_sptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const ast_expr_integer_t>(number);
}

intptr_t
v_ast_expr_integer_get_number(const ast_expr_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_integer_t &>(**ptr);

    return  r.number;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_string(ast_expr_sptr_t *ret, const char *string)
{
    *ret = std::make_shared<const ast_expr_string_t>(string);
}

const char *
v_ast_expr_string_get_string(const ast_expr_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_string_t &>(**ptr);

    return  r.string.c_str();
}

//---------------------------------------------------------------------
void
v_ast_make_expr_char(ast_expr_sptr_t *ret, char32_t c)
{
    *ret = std::make_shared<const ast_expr_char_t>(c);
}

char32_t
v_ast_expr_char_get_char(const ast_expr_sptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_char_t &>(**ptr);

    return  r.char_;
}


//---------------------------------------------------------------------
//- Generics ...
//---------------------------------------------------------------------
void
v_ast_make_generic(ast_generic_sptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_generic_t>(vtab, obj);
}

//---------------------------------------------------------------------
void
v_ast_make_unit_generic(ast_unit_sptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_unit_generic_t>(vtab, obj);
}

void
v_ast_make_stmt_generic(ast_stmt_sptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_stmt_generic_t>(vtab, obj);
}

//---------------------------------------------------------------------
const ast_generic_vtable *
v_ast_generic_get_vtable(const ast_generic_sptr_t *ptr)
{
    return (*ptr)->vtable;
}

const void *
v_ast_generic_get_object(const ast_generic_sptr_t *ptr)
{
    return (*ptr)->object;
}


//---------------------------------------------------------------------
//- Visitors ...
//---------------------------------------------------------------------
v_quark_t
v_ast_base_get_visitor_method_tag(const ast_base_sptr_t *ptr)
{
    return  (*ptr)->method_tag();
}


//---------------------------------------------------------------------
#define DEF(type) \
v_quark_t v_##type##_visitor_method_tag;

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF


#define DEF(type) \
void \
v_visitor_set_method_##type(visitor_sptr_t *dst, const visitor_sptr_t *src, type::visitor_method_t method) \
{ \
    auto visitor = (*src)->set_void_method(v_##type##_visitor_method_tag, (void *)method); \
    *dst = std::make_shared<const voidc_visitor_t>(visitor); \
}

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF


//---------------------------------------------------------------------
void
v_ast_accept_visitor(const ast_base_sptr_t *object, const visitor_sptr_t *visitor, void *aux)
{
    (*object)->accept(*visitor, aux);
}


//---------------------------------------------------------------------
//- Lists ...
//---------------------------------------------------------------------
#define AST_DEFINE_LIST_APPEND_IMPL(list_t, fun_name, item_t) \
void fun_name(std::shared_ptr<const list_t> *ret, const std::shared_ptr<const list_t> *list, const item_t *items, int count) \
{ \
    (*ret) = std::make_shared<const list_t>(*list, items, count); \
}

#define AST_DEFINE_LIST_GET_SIZE_IMPL(list_t, fun_name) \
int fun_name(const std::shared_ptr<const list_t> *list) \
{ \
    return int((*list)->data.size()); \
}

#define AST_DEFINE_LIST_GET_ITEMS_IMPL(list_t, fun_name, item_t) \
void fun_name(const std::shared_ptr<const list_t> *list, int idx, item_t *items, int count) \
{ \
    std::copy_n((*list)->data.begin() + idx, size_t(count), items); \
}


#define AST_DEFINE_LIST_AGSGI_IMPL(list_t, item_t, name) \
\
    AST_DEFINE_LIST_APPEND_IMPL(list_t, v_ast_list_append_##name##_impl, item_t) \
    AST_DEFINE_LIST_GET_SIZE_IMPL(list_t, v_ast_list_get_size_##name##_impl) \
    AST_DEFINE_LIST_GET_ITEMS_IMPL(list_t, v_ast_list_get_items_##name##_impl, item_t)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_stmt_list_impl(ast_stmt_list_sptr_t *ret)
{
    (*ret) = std::make_shared<const ast_stmt_list_t>();
}

void
v_ast_make_list_stmt_list_impl(ast_stmt_list_sptr_t *ret,
                               const ast_stmt_sptr_t *items, int count)
{
    (*ret) = std::make_shared<const ast_stmt_list_t>(items, size_t(count));
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_stmt_list_t, ast_stmt_sptr_t, stmt_list)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_expr_list_impl(ast_expr_list_sptr_t *ret)
{
    (*ret) = std::make_shared<const ast_expr_list_t>();
}

void
v_ast_make_list_expr_list_impl(ast_expr_list_sptr_t *ret,
                               const ast_expr_sptr_t *items, int count)
{
    (*ret) = std::make_shared<const ast_expr_list_t>(items, size_t(count));
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_expr_list_t, ast_expr_sptr_t, expr_list)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_generic_list_impl(ast_generic_list_sptr_t *ret, v_quark_t tag)
{
    (*ret) = std::make_shared<const ast_generic_list_t>(tag);
}

void
v_ast_make_list_generic_list_impl(ast_generic_list_sptr_t *ret, v_quark_t tag,
                                  const ast_base_sptr_t *items, int count)
{
    (*ret) = std::make_shared<const ast_generic_list_t>(tag, items, size_t(count));
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_generic_list_t, ast_base_sptr_t, generic_list)


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
    static_assert((sizeof(ast_base_sptr_t) % sizeof(intptr_t)) == 0);

    auto &gctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(ast_base_sptr_t)/sizeof(intptr_t));

#define DEF(name) \
    static_assert(sizeof(ast_base_sptr_t) == sizeof(ast_##name##_sptr_t)); \
    auto opaque_##name##_sptr_type = gctx.make_struct_type("struct.v_ast_opaque_" #name "_sptr"); \
    opaque_##name##_sptr_type->set_body(&content_type, 1, false); \
    gctx.add_symbol("v_ast_opaque_" #name "_sptr", gctx.opaque_type_type, (void *)opaque_##name##_sptr_type);

    DEF(base)

    DEF(unit)

    DEF(stmt)
    DEF(stmt_list)

    DEF(expr)
    DEF(expr_list)

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


