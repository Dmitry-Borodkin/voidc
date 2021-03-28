//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include "voidc_types.h"
#include "voidc_visitor.h"
#include "voidc_dllexport.h"

#include <memory>
#include <cstdio>

#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>


//---------------------------------------------------------------------
//- Visitor tags...
//---------------------------------------------------------------------
#define DEFINE_AST_VISITOR_METHOD_TAGS(DEF) \
    DEF(ast_stmt_list_t) \
    DEF(ast_arg_list_t) \
    DEF(ast_unit_t) \
    DEF(ast_stmt_t) \
    DEF(ast_call_t) \
    DEF(ast_arg_identifier_t) \
    DEF(ast_arg_integer_t) \
    DEF(ast_arg_string_t) \
    DEF(ast_arg_char_t)

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
    virtual void accept(const visitor_sptr_t &visitor, void *aux) const = 0;

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
struct ast_call_base_t : public virtual ast_base_t {};
struct ast_argument_t :  public virtual ast_base_t {};

typedef std::shared_ptr<const ast_unit_base_t> ast_unit_sptr_t;
typedef std::shared_ptr<const ast_stmt_base_t> ast_stmt_sptr_t;
typedef std::shared_ptr<const ast_call_base_t> ast_call_sptr_t;
typedef std::shared_ptr<const ast_argument_t>  ast_argument_sptr_t;


//---------------------------------------------------------------------
template<typename T>
struct ast_list_t : public ast_base_t
{
    const immer::vector<std::shared_ptr<const T>> data;

    ast_list_t() : data{} {}

    ast_list_t(const std::shared_ptr<const T> *items, size_t count)
      : data(items, items+count)
    {}

    ast_list_t(const std::shared_ptr<const ast_list_t<T>> &list,
               const std::shared_ptr<const T> &item)
      : data(list->data.push_back(item))
    {}

    ast_list_t(const std::shared_ptr<const ast_list_t<T>> &list,
               const std::shared_ptr<const T> *items, size_t count)
      : data(list->do_append(items, count))
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux, size_t count, bool start);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        auto tag = method_tag();

        visitor->visit<visitor_method_t>(tag, visitor, aux, data.size(), true);

        for (auto &it : data)
        {
            it->accept(visitor, aux);
        }

        visitor->visit<visitor_method_t>(tag, visitor, aux, data.size(), false);
    }

private:
    immer::vector<std::shared_ptr<const T>>
    do_append(const std::shared_ptr<const T> *list, size_t count) const
    {
        auto t = data.transient();

        for (size_t i=0; i<count; ++i)  t.push_back(list[i]);

        return t.persistent();
    }
};

//---------------------------------------------------------------------
struct ast_stmt_list_t : public ast_list_t<ast_stmt_base_t>
{
    ast_stmt_list_t() = default;

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_base_t> *items, size_t count)
      : ast_list_t<ast_stmt_base_t>(items, count)
    {}

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_list_t> &list,
                    const std::shared_ptr<const ast_stmt_base_t> &item)
      : ast_list_t<ast_stmt_base_t>(list, item)
    {}

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_list_t> &list,
                    const std::shared_ptr<const ast_stmt_base_t> *items, size_t count)
      : ast_list_t<ast_stmt_base_t>(list, items, count)
    {}

    AST_VISITOR_TAG(ast_stmt_list_t)
};

typedef std::shared_ptr<const ast_stmt_list_t> ast_stmt_list_sptr_t;

//---------------------------------------------------------------------
struct ast_arg_list_t : public ast_list_t<ast_argument_t>
{
    ast_arg_list_t() = default;

    ast_arg_list_t(const std::shared_ptr<const ast_argument_t> *items, size_t count)
      : ast_list_t<ast_argument_t>(items, count)
    {}

    ast_arg_list_t(const std::shared_ptr<const ast_arg_list_t> &list,
                   const std::shared_ptr<const ast_argument_t> &item)
      : ast_list_t<ast_argument_t>(list, item)
    {}

    ast_arg_list_t(const std::shared_ptr<const ast_arg_list_t> &list,
                   const std::shared_ptr<const ast_argument_t> *items, size_t count)
      : ast_list_t<ast_argument_t>(list, items, count)
    {}

    AST_VISITOR_TAG(ast_arg_list_t)
};

typedef std::shared_ptr<const ast_arg_list_t>  ast_arg_list_sptr_t;


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

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, &stmt_list, line, column);
    }

    AST_VISITOR_TAG(ast_unit_t)
};


//---------------------------------------------------------------------
struct ast_stmt_t : public ast_stmt_base_t
{
    const std::string var_name;
    const ast_call_sptr_t call;

    explicit ast_stmt_t(const std::string &var,
                        const ast_call_sptr_t &_call)
      : var_name(var),
        call(_call)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *vname, const ast_call_sptr_t *call);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, &var_name, &call);
    }

    AST_VISITOR_TAG(ast_stmt_t)
};

//---------------------------------------------------------------------
struct ast_call_t : public ast_call_base_t
{
    const std::string fun_name;
    const ast_arg_list_sptr_t arg_list;

    explicit ast_call_t(const std::string &fun,
                        const ast_arg_list_sptr_t &_arg_list)
      : fun_name(fun),
        arg_list(_arg_list)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *fname, const ast_arg_list_sptr_t *args);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, &fun_name, &arg_list);
    }

    AST_VISITOR_TAG(ast_call_t)
};


//---------------------------------------------------------------------
struct ast_arg_identifier_t : public ast_argument_t
{
    const std::string name;

    explicit ast_arg_identifier_t(const std::string &_name)
      : name(_name)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *name);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, &name);
    }

    AST_VISITOR_TAG(ast_arg_identifier_t)
};

//---------------------------------------------------------------------
struct ast_arg_integer_t : public ast_argument_t
{
    const intptr_t number;

    explicit ast_arg_integer_t(intptr_t _number)
      : number(_number)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     intptr_t num);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, number);
    }

    AST_VISITOR_TAG(ast_arg_integer_t)
};

//---------------------------------------------------------------------
struct ast_arg_string_t : public ast_argument_t
{
    const std::string string;

    explicit ast_arg_string_t(const std::string &_string)
      : string(_string)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const std::string *str);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, &string);
    }

    AST_VISITOR_TAG(ast_arg_string_t)
};

//---------------------------------------------------------------------
struct ast_arg_char_t : public ast_argument_t
{
    const char32_t char_;

    explicit ast_arg_char_t(char32_t _char_)
      : char_(_char_)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     char32_t char_);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, char_);
    }

    AST_VISITOR_TAG(ast_arg_char_t)
};


//---------------------------------------------------------------------
//- Generics...
//---------------------------------------------------------------------
extern "C"
{

struct ast_generic_vtable
{
    void (*destroy)(void *object);

    void (*accept)(const void *object, const visitor_sptr_t *visitor, void *aux);

    v_quark_t visitor_method_tag;
};

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
struct ast_generic_t : public virtual ast_base_t
{
    ast_generic_t(const ast_generic_vtable *vtab, void *obj)
      : vtable(vtab),
        _object(obj)
    {}

    ~ast_generic_t() override
    {
        vtable->destroy(_object);
    }

public:
    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        vtable->accept(object, &visitor, aux);
    }

    const ast_generic_vtable * const vtable;

    const void * const &object = _object;

protected:
    v_quark_t method_tag(void) const override
    {
        return  vtable->visitor_method_tag;
    }

private:
    void * const _object;
};

typedef std::shared_ptr<const ast_generic_t>  ast_generic_sptr_t;

//---------------------------------------------------------------------
struct ast_unit_generic_t : public ast_unit_base_t, public ast_generic_t
{
    explicit ast_unit_generic_t(const ast_generic_vtable *vtab, void *obj)
      : ast_generic_t(vtab, obj)
    {}
};

struct ast_stmt_generic_t : public ast_stmt_base_t, public ast_generic_t
{
    explicit ast_stmt_generic_t(const ast_generic_vtable *vtab, void *obj)
      : ast_generic_t(vtab, obj)
    {}
};

struct ast_call_generic_t : public ast_call_base_t, public ast_generic_t
{
    explicit ast_call_generic_t(const ast_generic_vtable *vtab, void *obj)
      : ast_generic_t(vtab, obj)
    {}
};

struct ast_argument_generic_t : public ast_argument_t, public ast_generic_t
{
    explicit ast_argument_generic_t(const ast_generic_vtable *vtab, void *obj)
      : ast_generic_t(vtab, obj)
    {}
};


//---------------------------------------------------------------------
struct ast_generic_list_t : public ast_list_t<ast_base_t>
{
    explicit ast_generic_list_t(v_quark_t tag)
      : visitor_method_tag(tag)
    {}

    ast_generic_list_t(v_quark_t tag, const ast_base_sptr_t *items, size_t count)
      : ast_list_t<ast_base_t>(items, count),
        visitor_method_tag(tag)
    {}

    ast_generic_list_t(const std::shared_ptr<const ast_generic_list_t> &list,
                       const ast_base_sptr_t &item)
      : ast_list_t<ast_base_t>(list, item),
        visitor_method_tag(list->visitor_method_tag)
    {}

    ast_generic_list_t(const std::shared_ptr<const ast_generic_list_t> &list,
                       const ast_base_sptr_t *items, size_t count)
      : ast_list_t<ast_base_t>(list, items, count),
        visitor_method_tag(list->visitor_method_tag)
    {}

protected:
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
