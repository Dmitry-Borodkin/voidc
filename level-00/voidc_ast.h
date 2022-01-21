//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include "voidc_types.h"
#include "voidc_visitor.h"
#include "voidc_dllexport.h"

#include <memory>
#include <cstdio>
#include <cstdlib>

#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>


//---------------------------------------------------------------------
//- Visitor tags...
//---------------------------------------------------------------------
#define DEFINE_AST_VISITOR_METHOD_TAGS(DEF) \
    DEF(ast_stmt_list_t) \
    DEF(ast_expr_list_t) \
    DEF(ast_unit_t) \
    DEF(ast_stmt_t) \
    DEF(ast_expr_call_t) \
    DEF(ast_expr_identifier_t) \
    DEF(ast_expr_integer_t) \
    DEF(ast_expr_string_t) \
    DEF(ast_expr_char_t)

#define DEF(type) \
extern v_quark_t v_##type##_visitor_method_tag;

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
struct ast_base_t
{
    virtual ~ast_base_t() = default;

public:
    virtual void accept(const visitor_sptr_t &visitor) const = 0;

public:
    virtual v_quark_t method_tag(void) const = 0;
};

typedef std::shared_ptr<const ast_base_t> ast_base_sptr_t;

//---------------------------------------------------------------------
#define AST_VISITOR_TAG(type) \
    v_quark_t method_tag(void) const override { return v_##type##_visitor_method_tag; }


//---------------------------------------------------------------------
struct ast_unit_base_t : public virtual ast_base_t {};
struct ast_stmt_base_t : public virtual ast_base_t {};
struct ast_expr_base_t : public virtual ast_base_t {};

typedef std::shared_ptr<const ast_unit_base_t> ast_unit_sptr_t;
typedef std::shared_ptr<const ast_stmt_base_t> ast_stmt_sptr_t;
typedef std::shared_ptr<const ast_expr_base_t> ast_expr_sptr_t;


//---------------------------------------------------------------------
template<typename Self, typename T>
struct ast_list_t : public virtual ast_base_t, public std::enable_shared_from_this<Self>
{
    using item_t = T;

    const immer::vector<std::shared_ptr<const T>> data;

    ast_list_t() : data{} {}

    ast_list_t(const std::shared_ptr<const T> *items, size_t count)
      : data(items, items+count)
    {}

    ast_list_t(const std::shared_ptr<const ast_list_t<Self, T>> &list,
               const std::shared_ptr<const T> &item)
      : data(list->data.push_back(item))
    {}

    ast_list_t(const std::shared_ptr<const ast_list_t<Self, T>> &list,
               const std::shared_ptr<const T> *items, size_t count)
      : data(list->do_append(items, count))
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux, const std::shared_ptr<const Self> *self);

    void accept(const visitor_sptr_t &visitor) const override
    {
        auto self = this->shared_from_this();

        visitor->visit<visitor_method_t>(method_tag(), &self);
    }

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
struct ast_stmt_list_t : public ast_list_t<ast_stmt_list_t, ast_stmt_base_t>
{
    using base_t = ast_list_t<ast_stmt_list_t, ast_stmt_base_t>;

    ast_stmt_list_t() = default;

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_base_t> *items, size_t count)
      : base_t(items, count)
    {}

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_list_t> &list,
                    const std::shared_ptr<const ast_stmt_base_t> &item)
      : base_t(list, item)
    {}

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_list_t> &list,
                    const std::shared_ptr<const ast_stmt_base_t> *items, size_t count)
      : base_t(list, items, count)
    {}

    AST_VISITOR_TAG(ast_stmt_list_t)
};

typedef std::shared_ptr<const ast_stmt_list_t> ast_stmt_list_sptr_t;

//---------------------------------------------------------------------
struct ast_expr_list_t : public ast_list_t<ast_expr_list_t, ast_expr_base_t>
{
    using base_t = ast_list_t<ast_expr_list_t, ast_expr_base_t>;

    ast_expr_list_t() = default;

    ast_expr_list_t(const std::shared_ptr<const ast_expr_base_t> *items, size_t count)
      : base_t(items, count)
    {}

    ast_expr_list_t(const std::shared_ptr<const ast_expr_list_t> &list,
                    const std::shared_ptr<const ast_expr_base_t> &item)
      : base_t(list, item)
    {}

    ast_expr_list_t(const std::shared_ptr<const ast_expr_list_t> &list,
                    const std::shared_ptr<const ast_expr_base_t> *items, size_t count)
      : base_t(list, items, count)
    {}

    AST_VISITOR_TAG(ast_expr_list_t)
};

typedef std::shared_ptr<const ast_expr_list_t> ast_expr_list_sptr_t;


//---------------------------------------------------------------------
struct ast_unit_t : public ast_unit_base_t
{
    const ast_stmt_list_sptr_t stmt_list;

    const int line;
    const int column;

    explicit ast_unit_t(const ast_stmt_list_sptr_t &_stmt_list,
                        int _line, int _column)
      : stmt_list(_stmt_list),
        line(_line),
        column(_column)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const ast_stmt_list_sptr_t *stmts, int l, int col);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), &stmt_list, line, column);
    }

    AST_VISITOR_TAG(ast_unit_t)
};


//---------------------------------------------------------------------
struct ast_stmt_t : public ast_stmt_base_t
{
    const std::string     name;
    const ast_expr_sptr_t expr;

    explicit ast_stmt_t(const std::string     &_name,
                        const ast_expr_sptr_t &_expr)
      : name(_name),
        expr(_expr)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *name, const ast_expr_sptr_t *expr);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), &name, &expr);
    }

    AST_VISITOR_TAG(ast_stmt_t)
};


//---------------------------------------------------------------------
struct ast_expr_call_t : public ast_expr_base_t
{
    const ast_expr_sptr_t      fun_expr;
    const ast_expr_list_sptr_t arg_list;

    explicit ast_expr_call_t(const ast_expr_sptr_t      &_fun_expr,
                             const ast_expr_list_sptr_t &_arg_list)
      : fun_expr(_fun_expr),
        arg_list(_arg_list)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const ast_expr_sptr_t      *fexpr,
                                     const ast_expr_list_sptr_t *args);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), &fun_expr, &arg_list);
    }

    AST_VISITOR_TAG(ast_expr_call_t)
};


//---------------------------------------------------------------------
struct ast_expr_identifier_t : public ast_expr_base_t
{
    const std::string name;

    explicit ast_expr_identifier_t(const std::string &_name)
      : name(_name)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *name);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), &name);
    }

    AST_VISITOR_TAG(ast_expr_identifier_t)
};

//---------------------------------------------------------------------
struct ast_expr_integer_t : public ast_expr_base_t
{
    const intptr_t number;

    explicit ast_expr_integer_t(intptr_t _number)
      : number(_number)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     intptr_t num);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), number);
    }

    AST_VISITOR_TAG(ast_expr_integer_t)
};

//---------------------------------------------------------------------
struct ast_expr_string_t : public ast_expr_base_t
{
    const std::string string;

    explicit ast_expr_string_t(const std::string &_string)
      : string(_string)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *str);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), &string);
    }

    AST_VISITOR_TAG(ast_expr_string_t)
};

//---------------------------------------------------------------------
struct ast_expr_char_t : public ast_expr_base_t
{
    const char32_t char_;

    explicit ast_expr_char_t(char32_t _char_)
      : char_(_char_)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     char32_t char_);

    void accept(const visitor_sptr_t &visitor) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), char_);
    }

    AST_VISITOR_TAG(ast_expr_char_t)
};


//---------------------------------------------------------------------
//- Generics...
//---------------------------------------------------------------------
extern "C"
{

struct ast_generic_vtable
{
    void (*destroy)(void *object);

    void (*accept)(const void *object, const visitor_sptr_t *visitor);

    v_quark_t visitor_method_tag;
};

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
struct ast_generic_t : public virtual ast_base_t
{
    ast_generic_t(const ast_generic_vtable *vtab, size_t size)
      : vtable(vtab),
        object(std::malloc(size))
    {
        assert(object);
    }

    ~ast_generic_t() override
    {
        vtable->destroy(object);

        std::free(object);
    }

public:
    void accept(const visitor_sptr_t &visitor) const override
    {
        vtable->accept(object, &visitor);
    }

    const ast_generic_vtable * const vtable;

    void * const object;

public:
    v_quark_t method_tag(void) const override
    {
        return  vtable->visitor_method_tag;
    }
};

typedef std::shared_ptr<const ast_generic_t>  ast_generic_sptr_t;

//---------------------------------------------------------------------
struct ast_unit_generic_t : public ast_unit_base_t, public ast_generic_t
{
    explicit ast_unit_generic_t(const ast_generic_vtable *vtab, size_t size)
      : ast_generic_t(vtab, size)
    {}
};

struct ast_stmt_generic_t : public ast_stmt_base_t, public ast_generic_t
{
    explicit ast_stmt_generic_t(const ast_generic_vtable *vtab, size_t size)
      : ast_generic_t(vtab, size)
    {}
};

struct ast_expr_generic_t : public ast_expr_base_t, public ast_generic_t
{
    explicit ast_expr_generic_t(const ast_generic_vtable *vtab, size_t size)
      : ast_generic_t(vtab, size)
    {}
};


//---------------------------------------------------------------------
struct ast_generic_list_t : public ast_list_t<ast_generic_list_t, ast_base_t>
{
    using base_t = ast_list_t<ast_generic_list_t, ast_base_t>;

    explicit ast_generic_list_t(v_quark_t tag)
      : visitor_method_tag(tag)
    {}

    ast_generic_list_t(v_quark_t tag, const ast_base_sptr_t *items, size_t count)
      : base_t(items, count),
        visitor_method_tag(tag)
    {}

    ast_generic_list_t(const std::shared_ptr<const ast_generic_list_t> &list,
                       const ast_base_sptr_t &item)
      : base_t(list, item),
        visitor_method_tag(list->visitor_method_tag)
    {}

    ast_generic_list_t(const std::shared_ptr<const ast_generic_list_t> &list,
                       const ast_base_sptr_t *items, size_t count)
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

typedef std::shared_ptr<const ast_generic_list_t> ast_generic_list_sptr_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void v_ast_static_initialize(void);
void v_ast_static_terminate(void);


#endif  //- VOIDC_AST_H
