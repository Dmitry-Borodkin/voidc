//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include "voidc_types.h"
#include "voidc_quark.h"
#include "voidc_dllexport.h"

#include <memory>
#include <cstdio>
#include <cstdlib>
#include <any>
#include <unordered_map>

#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>


//---------------------------------------------------------------------
//- Visitor tags...
//---------------------------------------------------------------------
#define DEFINE_AST_VISITOR_METHOD_TAGS(DEF) \
    DEF(stmt_list) \
    DEF(expr_list) \
    DEF(unit) \
    DEF(stmt) \
    DEF(expr_call) \
    DEF(expr_identifier) \
    DEF(expr_integer) \
    DEF(expr_string) \
    DEF(expr_char)

#define DEF(name) \
extern v_quark_t v_ast_##name##_visitor_method_tag;

extern "C"
{
VOIDC_DLLEXPORT_BEGIN_VARIABLE

    DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

VOIDC_DLLEXPORT_END
}

#undef DEF


//---------------------------------------------------------------------
//- AST classes
//---------------------------------------------------------------------
struct ast_base_data_t
{
    virtual ~ast_base_data_t() = default;

public:
    mutable std::unordered_map<v_quark_t, std::any> attributes;     //- Sic!!!

public:
    virtual v_quark_t method_tag(void) const = 0;
};

typedef std::shared_ptr<const ast_base_data_t> ast_base_t;

//---------------------------------------------------------------------
#define AST_VISITOR_TAG(name) \
    v_quark_t method_tag(void) const override { return v_ast_##name##_visitor_method_tag; }


//---------------------------------------------------------------------
#define DEFINE_BASE_DATA_T(name) \
struct ast_##name##_base_data_t : ast_base_data_t {}; \
typedef std::shared_ptr<const ast_##name##_base_data_t> ast_##name##_t;

DEFINE_BASE_DATA_T(unit)
DEFINE_BASE_DATA_T(stmt)
DEFINE_BASE_DATA_T(expr)

#undef DEFINE_BASE_DATA_T


//---------------------------------------------------------------------
template<typename T>
struct ast_base_list_data_t : ast_base_data_t
{
    using item_t = T;

    const immer::vector<std::shared_ptr<const T>> data;

    ast_base_list_data_t() : data{} {}

    ast_base_list_data_t(const std::shared_ptr<const T> *items, size_t count)
      : data(items, items+count)
    {}

    ast_base_list_data_t(const std::shared_ptr<const ast_base_list_data_t<T>> &list,
                         const std::shared_ptr<const T> &item)
      : data(list->data.push_back(item))
    {}

    ast_base_list_data_t(const std::shared_ptr<const ast_base_list_data_t<T>> &list,
                         const std::shared_ptr<const T> *items, size_t count)
      : data(list->do_append(items, count))
    {}

private:
    immer::vector<std::shared_ptr<const T>>
    do_append(const std::shared_ptr<const T> *items, size_t count) const
    {
        auto t = data.transient();

        for (size_t i=0; i<count; ++i)  t.push_back(items[i]);

        return t.persistent();
    }
};

//---------------------------------------------------------------------
template<typename T, v_quark_t &Tag>
struct ast_list_data_t : ast_base_list_data_t<T>
{
    using base_t = ast_base_list_data_t<T>;

    ast_list_data_t()
      : base_t()
    {}

    ast_list_data_t(const std::shared_ptr<const T> *items, size_t count)
      : base_t(items, count)
    {}

    ast_list_data_t(const std::shared_ptr<const ast_list_data_t<T, Tag>> &list,
                    const std::shared_ptr<const T> &item)
      : base_t(list, item)
    {}

    ast_list_data_t(const std::shared_ptr<const ast_list_data_t<T, Tag>> &list,
                    const std::shared_ptr<const T> *items, size_t count)
      : base_t(list, items, count)
    {}

public:
    v_quark_t method_tag(void) const override { return Tag; }
};

//---------------------------------------------------------------------
using ast_stmt_list_data_t = ast_list_data_t<ast_stmt_base_data_t, v_ast_stmt_list_visitor_method_tag>;
using ast_expr_list_data_t = ast_list_data_t<ast_expr_base_data_t, v_ast_expr_list_visitor_method_tag>;

typedef std::shared_ptr<const ast_stmt_list_data_t> ast_stmt_list_t;
typedef std::shared_ptr<const ast_expr_list_data_t> ast_expr_list_t;


//---------------------------------------------------------------------
struct ast_unit_data_t : ast_unit_base_data_t
{
    const ast_stmt_list_t stmt_list;

    const int line;
    const int column;

    explicit ast_unit_data_t(const ast_stmt_list_t &_stmt_list,
                             int _line, int _column)
      : stmt_list(_stmt_list),
        line(_line),
        column(_column)
    {}

public:
    AST_VISITOR_TAG(unit)
};


//---------------------------------------------------------------------
struct ast_stmt_data_t : ast_stmt_base_data_t
{
    const v_quark_t  name;
    const ast_expr_t expr;

    explicit ast_stmt_data_t(v_quark_t          _name,
                             const ast_expr_t  &_expr)
      : name(_name),
        expr(_expr)
    {}

public:
    AST_VISITOR_TAG(stmt)
};


//---------------------------------------------------------------------
struct ast_expr_call_data_t : ast_expr_base_data_t
{
    const ast_expr_t      fun_expr;
    const ast_expr_list_t arg_list;

    explicit ast_expr_call_data_t(const ast_expr_t      &_fun_expr,
                                  const ast_expr_list_t &_arg_list)
      : fun_expr(_fun_expr),
        arg_list(_arg_list)
    {}

public:
    AST_VISITOR_TAG(expr_call)
};


//---------------------------------------------------------------------
struct ast_expr_identifier_data_t : ast_expr_base_data_t
{
    const v_quark_t name;

    explicit ast_expr_identifier_data_t(v_quark_t _name)
      : name(_name)
    {}

public:
    AST_VISITOR_TAG(expr_identifier)
};

//---------------------------------------------------------------------
struct ast_expr_integer_data_t : ast_expr_base_data_t
{
    const intptr_t number;

    explicit ast_expr_integer_data_t(intptr_t _number)
      : number(_number)
    {}

public:
    AST_VISITOR_TAG(expr_integer)
};

//---------------------------------------------------------------------
struct ast_expr_string_data_t : ast_expr_base_data_t
{
    const std::string string;

    explicit ast_expr_string_data_t(const std::string &_string)
      : string(_string)
    {}

public:
    AST_VISITOR_TAG(expr_string)
};

//---------------------------------------------------------------------
struct ast_expr_char_data_t : ast_expr_base_data_t
{
    const char32_t char_;

    explicit ast_expr_char_data_t(char32_t _char_)
      : char_(_char_)
    {}

public:
    AST_VISITOR_TAG(expr_char)
};


//---------------------------------------------------------------------
//- Generics...
//---------------------------------------------------------------------
extern "C"
{

struct ast_generic_vtable_t
{
    void (*init)(void *object);
    void (*term)(void *object);

    v_quark_t visitor_method_tag;
};

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
template <typename T>
struct ast_template_generic_data_t : T
{
    ast_template_generic_data_t(const ast_generic_vtable_t *vtab, size_t size)
      : vtable(vtab),
        object(std::malloc(size))
    {
        if (vtable && vtable->init)  vtable->init(object);
    }

    ~ast_template_generic_data_t() override
    {
        if (vtable && vtable->term)  vtable->term(object);

        std::free(object);
    }

public:
    const ast_generic_vtable_t * const vtable;

    void * const object;

public:
    v_quark_t method_tag(void) const override
    {
        return  (vtable ? vtable->visitor_method_tag : 0);
    }
};

//---------------------------------------------------------------------
using ast_generic_data_t = ast_template_generic_data_t<ast_base_data_t>;

using ast_unit_generic_data_t = ast_template_generic_data_t<ast_unit_base_data_t>;
using ast_stmt_generic_data_t = ast_template_generic_data_t<ast_stmt_base_data_t>;
using ast_expr_generic_data_t = ast_template_generic_data_t<ast_expr_base_data_t>;


//---------------------------------------------------------------------
struct ast_generic_list_data_t : ast_base_list_data_t<ast_base_data_t>
{
    using base_t = ast_base_list_data_t<ast_base_data_t>;

    explicit ast_generic_list_data_t(v_quark_t tag)
      : visitor_method_tag(tag)
    {}

    ast_generic_list_data_t(v_quark_t tag, const ast_base_t *items, size_t count)
      : base_t(items, count),
        visitor_method_tag(tag)
    {}

    ast_generic_list_data_t(const std::shared_ptr<const ast_generic_list_data_t> &list,
                            const ast_base_t &item)
      : base_t(list, item),
        visitor_method_tag(list->visitor_method_tag)
    {}

    ast_generic_list_data_t(const std::shared_ptr<const ast_generic_list_data_t> &list,
                            const ast_base_t *items, size_t count)
      : base_t(list, items, count),
        visitor_method_tag(list->visitor_method_tag)
    {}

public:
    v_quark_t method_tag(void) const override
    {
        return  visitor_method_tag;
    }

private:
    const v_quark_t visitor_method_tag;
};

typedef std::shared_ptr<const ast_generic_list_data_t> ast_generic_list_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void v_ast_static_initialize(void);
void v_ast_static_terminate(void);


#endif  //- VOIDC_AST_H
