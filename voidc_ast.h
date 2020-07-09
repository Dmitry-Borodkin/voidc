//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include "voidc_visitor.h"
#include "voidc_dllexport.h"

#include <memory>
#include <forward_list>
#include <istream>
#include <map>
#include <cstdio>

#include <immer/vector.hpp>


//---------------------------------------------------------------------
//- AST classes
//---------------------------------------------------------------------
class compile_ctx_t;

struct ast_base_t
{
    virtual ~ast_base_t() = default;

public:
    virtual void accept(const visitor_ptr_t &visitor) const = 0;

protected:
    virtual v_quark_t method_tag(void) const = 0;
};

typedef std::shared_ptr<const ast_base_t> ast_base_ptr_t;

//---------------------------------------------------------------------
struct ast_unit_base_t : public virtual ast_base_t {};
struct ast_stmt_base_t : public virtual ast_base_t {};
struct ast_call_base_t : public virtual ast_base_t {};
struct ast_argument_t :  public virtual ast_base_t {};

typedef std::shared_ptr<const ast_unit_base_t> ast_unit_ptr_t;
typedef std::shared_ptr<const ast_stmt_base_t> ast_stmt_ptr_t;
typedef std::shared_ptr<const ast_call_base_t> ast_call_ptr_t;
typedef std::shared_ptr<const ast_argument_t>  ast_argument_ptr_t;

//---------------------------------------------------------------------
template<typename T>
struct ast_list_t : public ast_base_t
{
    const immer::vector<std::shared_ptr<const T>> data;

    ast_list_t() : data{} {}

    ast_list_t(const std::shared_ptr<const ast_list_t<T>> &_list,
               const std::shared_ptr<const T>             &_item)
      : data(_list ? _list->data.push_back(_item) : immer::vector({_item}))
    {}

    ast_list_t(const std::shared_ptr<const T> *list, size_t count)
      : data(list, list+count)
    {}

public:
    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, size_t count, bool start);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, data.size(), true);

        for (auto &it : data)
        {
            it->accept(visitor);
        }

        method(&visitor, data.size(), false);
    }
};

//---------------------------------------------------------------------
struct ast_base_list_t : public ast_list_t<ast_base_t>
{
    ast_base_list_t() = default;

    ast_base_list_t(const std::shared_ptr<const ast_base_list_t> &list,
                    const std::shared_ptr<const ast_base_t> &item)
      : ast_list_t<ast_base_t>(list, item)
    {}

    ast_base_list_t(const std::shared_ptr<const ast_base_t> *list, size_t count)
      : ast_list_t<ast_base_t>(list, count)
    {}

public:
    static const v_quark_t &visitor_method_tag;

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

typedef std::shared_ptr<const ast_base_list_t> ast_base_list_ptr_t;

//---------------------------------------------------------------------
struct ast_stmt_list_t : public ast_list_t<ast_stmt_base_t>
{
    ast_stmt_list_t() = default;

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_list_t> &list,
                    const std::shared_ptr<const ast_stmt_base_t> &item)
      : ast_list_t<ast_stmt_base_t>(list, item)
    {}

    ast_stmt_list_t(const std::shared_ptr<const ast_stmt_base_t> *list, size_t count)
      : ast_list_t<ast_stmt_base_t>(list, count)
    {}

public:
    static const v_quark_t &visitor_method_tag;

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

typedef std::shared_ptr<const ast_stmt_list_t> ast_stmt_list_ptr_t;

//---------------------------------------------------------------------
struct ast_arg_list_t : public ast_list_t<ast_argument_t>
{
    ast_arg_list_t() = default;

    ast_arg_list_t(const std::shared_ptr<const ast_arg_list_t> &list,
                   const std::shared_ptr<const ast_argument_t> &item)
      : ast_list_t<ast_argument_t>(list, item)
    {}

    ast_arg_list_t(const std::shared_ptr<const ast_argument_t> *list, size_t count)
      : ast_list_t<ast_argument_t>(list, count)
    {}

public:
    static const v_quark_t &visitor_method_tag;

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

typedef std::shared_ptr<const ast_arg_list_t>  ast_arg_list_ptr_t;


//---------------------------------------------------------------------
struct ast_unit_t : public ast_unit_base_t
{
    const ast_stmt_list_ptr_t stmt_list;

    const int line;
    const int column;

    explicit ast_unit_t(const ast_stmt_list_ptr_t &_stmt_list,
                        int _line, int _column
                        )
      : stmt_list(_stmt_list),
        line(_line),
        column(_column)
    {}

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, const ast_stmt_list_ptr_t *stmts, int l, int col);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, &stmt_list, line, column);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};


//---------------------------------------------------------------------
struct ast_stmt_t : public ast_stmt_base_t
{
    const std::string var_name;
    const ast_call_ptr_t call;

    explicit ast_stmt_t(const std::string &var,
                        const ast_call_ptr_t &_call)
      : var_name(var),
        call(_call)
    {}

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, const std::string *vname, const ast_call_ptr_t *call);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, &var_name, &call);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

//---------------------------------------------------------------------
struct ast_call_t : public ast_call_base_t
{
    const std::string fun_name;
    const ast_arg_list_ptr_t arg_list;

    explicit ast_call_t(const std::string &fun,
                        const ast_arg_list_ptr_t &_arg_list)
      : fun_name(fun),
        arg_list(_arg_list)
    {}

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, const std::string *fname, const ast_arg_list_ptr_t *args);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, &fun_name, &arg_list);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};


//---------------------------------------------------------------------
struct ast_arg_identifier_t : public ast_argument_t
{
    const std::string name;

    explicit ast_arg_identifier_t(const std::string &_name)
      : name(_name)
    {}

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, const std::string *name);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, &name);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

//---------------------------------------------------------------------
struct ast_arg_integer_t : public ast_argument_t
{
    const intptr_t number;

    explicit ast_arg_integer_t(intptr_t _number)
      : number(_number)
    {}

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, intptr_t num);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, number);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

//---------------------------------------------------------------------
struct ast_arg_string_t : public ast_argument_t
{
    const std::string string;

    explicit ast_arg_string_t(const std::string &_string);     //- Sic!

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, const std::string *str);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, &string);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};

//---------------------------------------------------------------------
struct ast_arg_char_t : public ast_argument_t
{
    const char32_t c;

    explicit ast_arg_char_t(char32_t _c)
      : c(_c)
    {}

public:
    static const v_quark_t &visitor_method_tag;

    typedef void (*visitor_method_t)(const visitor_ptr_t *vis, char32_t c);

    void accept(const visitor_ptr_t &visitor) const override
    {
        auto method = visitor_method_t(visitor->void_methods[method_tag()]);

        method(&visitor, c);
    }

protected:
    v_quark_t method_tag(void) const override { return visitor_method_tag; }
};


//---------------------------------------------------------------------
//- Generics...
//---------------------------------------------------------------------
extern "C"
{

struct ast_generic_vtable
{
    void (*destroy)(void *object);

    void (*accept)(const void *object, const visitor_ptr_t *visitor);

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
    void accept(const visitor_ptr_t &visitor) const override
    {
        vtable->accept(object, &visitor);
    }

    const ast_generic_vtable * const vtable;

    const void * const &object = _object;

protected:
    explicit ast_generic_t(const std::shared_ptr<const ast_generic_t> &other)
      : vtable(other->vtable),
        _object(other->_object)
    {}

    v_quark_t method_tag(void) const override
    {
        return  vtable->visitor_method_tag;
    }

private:
    void * const _object;
};

typedef std::shared_ptr<const ast_generic_t>  ast_generic_ptr_t;

//---------------------------------------------------------------------
struct ast_unit_generic_t : public ast_unit_base_t, public ast_generic_t
{
    explicit ast_unit_generic_t(const ast_generic_ptr_t &gen)
      : ast_generic_t(gen)
    {}
};

struct ast_stmt_generic_t : public ast_stmt_base_t, public ast_generic_t
{
    explicit ast_stmt_generic_t(const ast_generic_ptr_t &gen)
      : ast_generic_t(gen)
    {}
};

struct ast_call_generic_t : public ast_call_base_t, public ast_generic_t
{
    explicit ast_call_generic_t(const ast_generic_ptr_t &gen)
      : ast_generic_t(gen)
    {}
};

struct ast_argument_generic_t : public ast_argument_t, public ast_generic_t
{
    explicit ast_argument_generic_t(const ast_generic_ptr_t &gen)
      : ast_generic_t(gen)
    {}
};


//-----------------------------------------------------------------
//- Visitors ...
//-----------------------------------------------------------------
#define DEFINE_AST_VISITOR_METHOD_TAGS(DEF) \
    DEF(ast_base_list_t) \
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
//- ...
//---------------------------------------------------------------------
void v_ast_static_initialize(void);
void v_ast_static_terminate(void);


#endif  //- VOIDC_AST_H
