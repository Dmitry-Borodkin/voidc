#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include <memory>
#include <forward_list>
#include <istream>
#include <map>
#include <cstdio>

#include <immer/vector.hpp>

#include "vpeg_parser.h"


//----------------------------------------------------------------------
//- AST classes
//----------------------------------------------------------------------
class compile_ctx_t;

struct ast_base_t
{
    virtual ~ast_base_t() = default;

    virtual void compile(compile_ctx_t &cctx) const = 0;
};

template<typename T> struct ast_list_t;

struct ast_stmt_t;
struct ast_call_t;

struct ast_argument_t : public ast_base_t {};

typedef ast_list_t<ast_stmt_t>     ast_stmt_list_t;
typedef ast_list_t<ast_argument_t> ast_arg_list_t;


//----------------------------------------------------------------------
struct ast_unit_t : public ast_base_t
{
    const std::shared_ptr<const ast_stmt_list_t> stmt_list;

    const int line;
    const int column;

    explicit ast_unit_t(const std::shared_ptr<const ast_stmt_list_t> &_stmt_list,
                        int _line, int _column
                        )
      : stmt_list(_stmt_list),
        line(_line),
        column(_column)
    {}

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
template<typename T>
struct ast_list_t : public ast_base_t
{
    const immer::vector<std::shared_ptr<const T>> data;

    ast_list_t(const std::shared_ptr<const ast_list_t<T>> &_list,
               const std::shared_ptr<const T>             &_item)
      : data(_list ? _list->data.push_back(_item) : immer::vector({_item}))
    {}

    void compile(compile_ctx_t &cctx) const override
    {
        for (auto &it : data)
        {
            it->compile(cctx);
        }
    }
};


//----------------------------------------------------------------------
struct ast_stmt_t : public ast_base_t
{
    const vpeg::string var_name;
    const std::shared_ptr<const ast_call_t> call;

    explicit ast_stmt_t(const vpeg::string &var,
                        const std::shared_ptr<const ast_call_t> &_call)
      : var_name(var),
        call(_call)
    {}

    void compile(compile_ctx_t &cctx) const override;
};

//----------------------------------------------------------------------
struct ast_call_t : public ast_base_t
{
    const vpeg::string fun_name;
    const std::shared_ptr<const ast_arg_list_t> arg_list;

    explicit ast_call_t(const vpeg::string &fun,
                        const std::shared_ptr<const ast_arg_list_t> &_arg_list)
      : fun_name(fun),
        arg_list(_arg_list)
    {}

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
struct ast_arg_identifier_t : public ast_argument_t
{
    const vpeg::string name;

    explicit ast_arg_identifier_t(const vpeg::string &_name)
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
    const vpeg::string &string = string_private;

    explicit ast_arg_string_t(const vpeg::string &_string);     //- Sic!

    void compile(compile_ctx_t &cctx) const override;

private:
    vpeg::string string_private;
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


#endif  //- VOIDC_AST_H
