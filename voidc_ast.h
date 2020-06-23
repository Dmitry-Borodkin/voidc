//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include <memory>
#include <forward_list>
#include <istream>
#include <map>
#include <cstdio>

#include <immer/vector.hpp>


//----------------------------------------------------------------------
//- AST classes
//----------------------------------------------------------------------
class compile_ctx_t;

struct ast_base_t
{
    virtual ~ast_base_t() = default;

    virtual void compile(compile_ctx_t &cctx) const = 0;
};

typedef std::shared_ptr<const ast_base_t> ast_base_ptr_t;

//----------------------------------------------------------------------
struct ast_unit_base_t : public virtual ast_base_t {};
struct ast_stmt_base_t : public virtual ast_base_t {};
struct ast_call_base_t : public virtual ast_base_t {};
struct ast_argument_t :  public virtual ast_base_t {};

typedef std::shared_ptr<const ast_unit_base_t> ast_unit_ptr_t;
typedef std::shared_ptr<const ast_stmt_base_t> ast_stmt_ptr_t;
typedef std::shared_ptr<const ast_call_base_t> ast_call_ptr_t;
typedef std::shared_ptr<const ast_argument_t>  ast_argument_ptr_t;

//----------------------------------------------------------------------
template<typename T>
struct ast_list_t : public ast_base_t
{
    const immer::vector<std::shared_ptr<const T>> data;

    ast_list_t(const std::shared_ptr<const ast_list_t<T>> &_list,
               const std::shared_ptr<const T>             &_item)
      : data(_list ? _list->data.push_back(_item) : immer::vector({_item}))
    {}

    ast_list_t(const std::shared_ptr<const T> *list, size_t count)
      : data(list, list+count)
    {}

    void compile(compile_ctx_t &cctx) const override
    {
        for (auto &it : data)
        {
            it->compile(cctx);
        }
    }
};

typedef ast_list_t<ast_base_t>      ast_base_list_t;
typedef ast_list_t<ast_stmt_base_t> ast_stmt_list_t;
typedef ast_list_t<ast_argument_t>  ast_arg_list_t;

typedef std::shared_ptr<const ast_base_list_t> ast_base_list_ptr_t;
typedef std::shared_ptr<const ast_stmt_list_t> ast_stmt_list_ptr_t;
typedef std::shared_ptr<const ast_arg_list_t>  ast_arg_list_ptr_t;


//----------------------------------------------------------------------
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

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
struct ast_stmt_t : public ast_stmt_base_t
{
    const std::string var_name;
    const ast_call_ptr_t call;

    explicit ast_stmt_t(const std::string &var,
                        const ast_call_ptr_t &_call)
      : var_name(var),
        call(_call)
    {}

    void compile(compile_ctx_t &cctx) const override;
};

//----------------------------------------------------------------------
struct ast_call_t : public ast_call_base_t
{
    const std::string fun_name;
    const ast_arg_list_ptr_t arg_list;

    explicit ast_call_t(const std::string &fun,
                        const ast_arg_list_ptr_t &_arg_list)
      : fun_name(fun),
        arg_list(_arg_list)
    {}

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
struct ast_arg_identifier_t : public ast_argument_t
{
    const std::string name;

    explicit ast_arg_identifier_t(const std::string &_name)
      : name(_name)
    {}

    void compile(compile_ctx_t &cctx) const override;
};

//----------------------------------------------------------------------
struct ast_arg_integer_t : public ast_argument_t
{
    const intptr_t number;

    explicit ast_arg_integer_t(intptr_t _number)
      : number(_number)
    {}

    void compile(compile_ctx_t &cctx) const override;
};

//----------------------------------------------------------------------
struct ast_arg_string_t : public ast_argument_t
{
    const std::string string;

    explicit ast_arg_string_t(const std::string &_string);     //- Sic!

    void compile(compile_ctx_t &cctx) const override;
};

//----------------------------------------------------------------------
struct ast_arg_char_t : public ast_argument_t
{
    const char32_t c;

    explicit ast_arg_char_t(char32_t _c)
      : c(_c)
    {}

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
//- Generics...
//----------------------------------------------------------------------
extern "C"
{

struct ast_generic_vtable
{
    void (*destroy)(void *object);

    void (*compile)(const void *object, compile_ctx_t *pcctx);
};

//----------------------------------------------------------------------
}   //- extern "C"


//----------------------------------------------------------------------
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

    void compile(compile_ctx_t &cctx) const override
    {
        vtable->compile(object, &cctx);
    }

    const ast_generic_vtable * const vtable;

    const void * const &object = _object;

protected:
    explicit ast_generic_t(const std::shared_ptr<const ast_generic_t> &other)
      : vtable(other->vtable),
        _object(other->_object)
    {}

private:
    void * const _object;
};

typedef std::shared_ptr<const ast_generic_t>  ast_generic_ptr_t;

//----------------------------------------------------------------------
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


//----------------------------------------------------------------------
//- ...
//----------------------------------------------------------------------
void v_ast_static_initialize(void);
void v_ast_static_terminate(void);


#endif  //- VOIDC_AST_H
