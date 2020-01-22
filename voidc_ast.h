#ifndef VOIDC_AST_H
#define VOIDC_AST_H

#include "voidc.h"

#include <memory>
#include <string>
#include <forward_list>
#include <istream>
#include <map>


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
    const std::shared_ptr<const T>             head;
    const std::shared_ptr<const ast_list_t<T>> tail;

    explicit ast_list_t(const std::shared_ptr<const T>             &_head,
                        const std::shared_ptr<const ast_list_t<T>> &_tail)
      : head(_head),
        tail(_tail)
    {}

    void compile(compile_ctx_t &cctx) const override
    {
        for (auto t=this; t; t=t->tail.get())
        {
            t->head->compile(cctx);
        }
    }
};


//----------------------------------------------------------------------
struct ast_stmt_t : public ast_base_t
{
    const std::string var_name;
    const std::shared_ptr<const ast_call_t> call;

    explicit ast_stmt_t(const char *var,
                        const std::shared_ptr<const ast_call_t> &_call)
      : var_name(var),
        call(_call)
    {}

    void compile(compile_ctx_t &cctx) const override;
};

//----------------------------------------------------------------------
struct ast_call_t : public ast_base_t
{
    const std::string fun_name;
    const std::shared_ptr<const ast_arg_list_t> arg_list;

    explicit ast_call_t(const char *fun,
                        const std::shared_ptr<const ast_arg_list_t> &_arg_list)
      : fun_name(fun),
        arg_list(_arg_list)
    {}

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
struct ast_arg_identifier_t : public ast_argument_t
{
    const std::string name;

    explicit ast_arg_identifier_t(const char *_name)
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
    const std::string &string = string_private;

    explicit ast_arg_string_t(const char *_string);     //- Sic!

    void compile(compile_ctx_t &cctx) const override;

private:
    std::string string_private;
};

//----------------------------------------------------------------------
struct ast_arg_char_t : public ast_argument_t
{
    const char c;

    explicit ast_arg_char_t(char _c)
      : c(_c)
    {}

    void compile(compile_ctx_t &cctx) const override;
};


//----------------------------------------------------------------------
//- ...
//----------------------------------------------------------------------
struct value_opaque_t : public std::shared_ptr<const ast_base_t>
{
    explicit value_opaque_t(const std::shared_ptr<const ast_base_t> &v)
      : std::shared_ptr<const ast_base_t>(v)
    {}
};


//----------------------------------------------------------------------
//- AST builder ...
//----------------------------------------------------------------------
class auxil_opaque_t
{
    friend value_t mk_unit(auxil_t auxil, value_t stmt_list, int pos);

    friend value_t mk_stmt_list(auxil_t auxil, value_t stmt, value_t stmt_list);

    friend value_t mk_stmt(auxil_t auxil, value_t var, value_t call);

    friend value_t mk_call(auxil_t auxil, value_t fun, value_t arg_list);

    friend value_t mk_arg_list(auxil_t auxil, value_t arg, value_t arg_list);

    friend value_t mk_arg_identifier(auxil_t auxil, value_t ident);
    friend value_t mk_arg_integer(auxil_t auxil, value_t num);
    friend value_t mk_arg_string(auxil_t auxil, value_t str);
    friend value_t mk_arg_char(auxil_t auxil, value_t c);

    friend void mk_newline(auxil_t auxil, int pos);

    friend int voidc_getchar(auxil_t auxil);

public:
    explicit auxil_opaque_t(std::istream &_input)
      : input(_input)
    {}
    virtual ~auxil_opaque_t() = default;

public:
    const std::map<int, int> &newlines = _newlines;

public:
    void clear(void)
    {
        values.clear();

        unit_pos = input_pos;
    }

private:
    void do_newline(int pos)
    {
        auto upos = unit_pos + pos;

        _newlines[upos] = line_number++;
    }

    int do_getchar(void)
    {
        if (input.eof())  return -1;

        input_pos++;

        return  input.get();
    }

private:
    template<typename T, typename... Args>
    value_t do_util(Args&&... args)
    {
        auto v = std::make_shared<const T>(std::forward<Args>(args)...);

        values.push_front(value_opaque_t(v));

        return &values.front();
    }

private:
    std::forward_list<value_opaque_t> values;

private:
    std::istream &input;

private:
    std::map<int, int> _newlines = {{0, 0}};

    int line_number = 1;

    int input_pos = 0;
    int unit_pos  = 0;
};


#endif  //- VOIDC_AST_H
