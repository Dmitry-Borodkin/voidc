//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_ast.h"

#include "voidc_util.h"
#include "voidc_target.h"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
v_quark_t v_ast_generic_list_dummy_method_tag = 0;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(ast_##name##_t, v_ast_initialize_##name##_impl) \
    VOIDC_DEFINE_TERMINATE_IMPL(ast_##name##_t, v_ast_terminate_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(ast_##name##_t, v_ast_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(ast_##name##_t, v_ast_move_##name##_impl) \
    VOIDC_DEFINE_EMPTY_IMPL(ast_##name##_t, v_ast_empty_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(ast_##name##_t, v_ast_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(ast_##name##_t, v_ast_std_any_set_pointer_##name##_impl)

    DEF(base)

    DEF(unit)

    DEF(stmt)
    DEF(stmt_list)

    DEF(expr)
    DEF(expr_list)

    DEF(generic_list)

#undef DEF


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void
v_ast_make_unit(ast_unit_t *ret, const ast_stmt_list_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<const ast_unit_data_t>(*stmt_list, line, column);
}

void
v_ast_unit_get_stmt_list(const ast_unit_t *ptr, ast_stmt_list_t *list)
{
    auto &r = dynamic_cast<const ast_unit_data_t &>(**ptr);

    *list = r.stmt_list;
}

int
v_ast_unit_get_line(const ast_unit_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_data_t &>(**ptr);

    return r.line;
}

int
v_ast_unit_get_column(const ast_unit_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_data_t &>(**ptr);

    return r.column;
}

//---------------------------------------------------------------------
void
v_ast_make_stmt(ast_stmt_t *ret, const char *var, const ast_expr_t *expr)
{
    *ret = std::make_shared<const ast_stmt_data_t>(var, *expr);
}

const char *
v_ast_stmt_get_name(const ast_stmt_t *ptr)
{
    auto &r = dynamic_cast<const ast_stmt_data_t &>(**ptr);

    return  r.name.c_str();
}

void
v_ast_stmt_get_expr(const ast_stmt_t *ptr, ast_expr_t *call)
{
    auto &r = dynamic_cast<const ast_stmt_data_t &>(**ptr);

    *call = r.expr;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_call(ast_expr_t *ret, const ast_expr_t *fun, const ast_expr_list_t *list)
{
    *ret = std::make_shared<const ast_expr_call_data_t>(*fun, *list);
}

void
v_ast_expr_call_get_fun_expr(const ast_expr_t *ptr, ast_expr_t *fun)
{
    auto &r = dynamic_cast<const ast_expr_call_data_t &>(**ptr);

    *fun = r.fun_expr;
}

void
v_ast_expr_call_get_arg_list(const ast_expr_t *ptr, ast_expr_list_t *list)
{
    auto &r = dynamic_cast<const ast_expr_call_data_t &>(**ptr);

    *list = r.arg_list;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_identifier(ast_expr_t *ret, const char *name)
{
    *ret = std::make_shared<const ast_expr_identifier_data_t>(name);
}

const char *
v_ast_expr_identifier_get_name(const ast_expr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_identifier_data_t &>(**ptr);

    return  r.name.c_str();
}

//---------------------------------------------------------------------
void
v_ast_make_expr_integer(ast_expr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const ast_expr_integer_data_t>(number);
}

intptr_t
v_ast_expr_integer_get_number(const ast_expr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_integer_data_t &>(**ptr);

    return  r.number;
}

//---------------------------------------------------------------------
void
v_ast_make_expr_string(ast_expr_t *ret, const char *string)
{
    *ret = std::make_shared<const ast_expr_string_data_t>(string);
}

const char *
v_ast_expr_string_get_string(const ast_expr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_string_data_t &>(**ptr);

    return  r.string.c_str();
}

//---------------------------------------------------------------------
void
v_ast_make_expr_char(ast_expr_t *ret, char32_t c)
{
    *ret = std::make_shared<const ast_expr_char_data_t>(c);
}

char32_t
v_ast_expr_char_get_char(const ast_expr_t *ptr)
{
    auto &r = dynamic_cast<const ast_expr_char_data_t &>(**ptr);

    return  r.char_;
}


//---------------------------------------------------------------------
//- Generics ...
//---------------------------------------------------------------------
void
v_ast_make_generic(ast_base_t *ret, const ast_generic_vtable_t *vtab, size_t size)
{
    *ret = std::make_shared<const ast_generic_data_t>(vtab, size);
}

//---------------------------------------------------------------------
void
v_ast_make_unit_generic(ast_unit_t *ret, const ast_generic_vtable_t *vtab, size_t size)
{
    *ret = std::make_shared<const ast_unit_generic_data_t>(vtab, size);
}

void
v_ast_make_stmt_generic(ast_stmt_t *ret, const ast_generic_vtable_t *vtab, size_t size)
{
    *ret = std::make_shared<const ast_stmt_generic_data_t>(vtab, size);
}

void
v_ast_make_expr_generic(ast_expr_t *ret, const ast_generic_vtable_t *vtab, size_t size)
{
    *ret = std::make_shared<const ast_expr_generic_data_t>(vtab, size);
}

//---------------------------------------------------------------------
const ast_generic_vtable_t *
v_ast_generic_get_vtable(const ast_base_t *base_ptr)
{
    auto ptr = std::dynamic_pointer_cast<const ast_generic_data_t>(*base_ptr);
    assert(ptr);

    return ptr->vtable;
}

const void *
v_ast_generic_get_object(const ast_base_t *base_ptr)
{
    auto ptr = std::dynamic_pointer_cast<const ast_generic_data_t>(*base_ptr);
    assert(ptr);

    return ptr->object;
}


//---------------------------------------------------------------------
//- Visitors ...
//---------------------------------------------------------------------
v_quark_t
v_ast_base_get_visitor_method_tag(const ast_base_t *ptr)
{
    return  (*ptr)->method_tag();
}


//---------------------------------------------------------------------
#define DEF(name) \
v_quark_t v_ast_##name##_visitor_method_tag;

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF


#define DEF(name) \
void \
v_visitor_set_method_##name(visitor_sptr_t *dst, const visitor_sptr_t *src, ast_##name##_data_t::visitor_method_t method, void *aux) \
{ \
    auto visitor = (*src)->set_void_method(v_ast_##name##_visitor_method_tag, (void *)method, aux); \
    *dst = std::make_shared<const voidc_visitor_t>(visitor); \
}

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF


//---------------------------------------------------------------------
void
v_ast_accept_visitor(const ast_base_t *object, const visitor_sptr_t *visitor)
{
    (*object)->accept(*visitor);
}


//---------------------------------------------------------------------
//- Lists ...
//---------------------------------------------------------------------
#define AST_DEFINE_LIST_APPEND_IMPL(list_data_t, fun_name) \
void fun_name(std::shared_ptr<const list_data_t> *ret, \
              const std::shared_ptr<const list_data_t> *list, \
              const std::shared_ptr<const list_data_t::item_t> *items, int count) \
{ \
    (*ret) = std::make_shared<const list_data_t>(*list, items, count); \
}

#define AST_DEFINE_LIST_GET_SIZE_IMPL(list_data_t, fun_name) \
int fun_name(const std::shared_ptr<const list_data_t> *list) \
{ \
    return int((*list)->data.size()); \
}

#define AST_DEFINE_LIST_GET_ITEM_IMPL(list_data_t, fun_name) \
const std::shared_ptr<const list_data_t::item_t> * \
fun_name(const std::shared_ptr<const list_data_t> *list, int idx) \
{ \
    return &(*list)->data[idx]; \
}


#define AST_DEFINE_LIST_AGSGI_IMPL(list_data_t, name) \
\
    AST_DEFINE_LIST_APPEND_IMPL(list_data_t, v_ast_list_append_##name##_impl) \
    AST_DEFINE_LIST_GET_SIZE_IMPL(list_data_t, v_ast_list_get_size_##name##_impl) \
    AST_DEFINE_LIST_GET_ITEM_IMPL(list_data_t, v_ast_list_get_item_##name##_impl)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_stmt_list_impl(ast_stmt_list_t *ret)
{
    (*ret) = std::make_shared<const ast_stmt_list_data_t>();
}

void
v_ast_make_list_stmt_list_impl(ast_stmt_list_t *ret,
                               const ast_stmt_t *items, int count)
{
    (*ret) = std::make_shared<const ast_stmt_list_data_t>(items, size_t(count));
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_stmt_list_data_t, stmt_list)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_expr_list_impl(ast_expr_list_t *ret)
{
    (*ret) = std::make_shared<const ast_expr_list_data_t>();
}

void
v_ast_make_list_expr_list_impl(ast_expr_list_t *ret,
                               const ast_expr_t *items, int count)
{
    (*ret) = std::make_shared<const ast_expr_list_data_t>(items, size_t(count));
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_expr_list_data_t, expr_list)


//---------------------------------------------------------------------
void
v_ast_make_list_nil_generic_list_impl(ast_generic_list_t *ret, v_quark_t tag)
{
    (*ret) = std::make_shared<const ast_generic_list_data_t>(tag);
}

void
v_ast_make_list_generic_list_impl(ast_generic_list_t *ret, v_quark_t tag,
                                  const ast_base_t *items, int count)
{
    (*ret) = std::make_shared<const ast_generic_list_data_t>(tag, items, size_t(count));
}

AST_DEFINE_LIST_AGSGI_IMPL(ast_generic_list_data_t, generic_list)


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
    static_assert((sizeof(ast_base_t) % sizeof(intptr_t)) == 0);

    auto &gctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(ast_base_t)/sizeof(intptr_t));

    auto add_type = [&gctx](const char *raw_name, v_type_t *type)
    {
        gctx.decls.constants_insert({raw_name, gctx.static_type_type});

        gctx.constant_values.insert({raw_name, reinterpret_cast<LLVMValueRef>(type)});

        gctx.decls.symbols_insert({raw_name, gctx.opaque_type_type});

        gctx.add_symbol_value(raw_name, type);
    };

#define DEF(name) \
    static_assert(sizeof(ast_base_t) == sizeof(ast_##name##_t)); \
    auto opaque_##name##_type = gctx.make_struct_type("v_ast_" #name "_t"); \
    opaque_##name##_type->set_body(&content_type, 1, false); \
    add_type("v_ast_" #name "_t", opaque_##name##_type);

    DEF(base)

    DEF(unit)

    DEF(stmt)
    DEF(stmt_list)

    DEF(expr)
    DEF(expr_list)

    DEF(generic_list)

#undef DEF

#define DEF(name) \
    v_ast_##name##_visitor_method_tag = v_quark_from_string(#name);

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF
}


//---------------------------------------------------------------------
void
v_ast_static_terminate(void)
{
}


