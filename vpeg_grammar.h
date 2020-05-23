#ifndef VPEG_GRAMMAR_H
#define VPEG_GRAMMAR_H

#include "vpeg_parser.h"

#include <utility>

#include <immer/map.hpp>
#include <llvm-c/Types.h>


//---------------------------------------------------------------------
namespace vpeg
{

extern LLVMTypeRef opaque_parser_ptr_type;
extern LLVMTypeRef parser_ref_type;

extern LLVMTypeRef opaque_action_ptr_type;
extern LLVMTypeRef action_ref_type;

extern LLVMTypeRef opaque_argument_ptr_type;
extern LLVMTypeRef argument_ref_type;

extern LLVMTypeRef opaque_grammar_ptr_type;
extern LLVMTypeRef grammar_ref_type;


extern "C" typedef void (*grammar_action_fun_t)(std::any *ret, const std::any *args, size_t count);


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class grammar_t
{
public:
    using parsers_map_t = immer::map<std::string, std::pair<parser_ptr_t, bool>>;
    using actions_map_t = immer::map<std::string, grammar_action_fun_t>;

public:
    grammar_t()  = default;
    ~grammar_t() = default;

public:
    grammar_t(const grammar_t &gr)
      : _hash(gr.hash),
        _parsers(gr.parsers),
        _actions(gr.actions)
    {}

    grammar_t &operator=(const grammar_t &gr)
    {
        _hash    = gr.hash;
        _parsers = gr.parsers;
        _actions = gr.actions;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    grammar_t set_parser(const std::string &name, const parser_ptr_t &parser, bool leftrec=false) const
    {
        return  grammar_t(_parsers.set(name, {parser, leftrec}), actions);
    }

    grammar_t set_action(const std::string &name, grammar_action_fun_t fun) const
    {
        return  grammar_t(parsers, _actions.set(name, fun));
    }

public:
    std::any parse(const std::string &name, context_t &ctx) const;

public:
    const size_t &hash = _hash;

    void check_hash(void);

public:
    const parsers_map_t &parsers = _parsers;
    const actions_map_t &actions = _actions;

private:
    size_t _hash = size_t(-1);

private:
    parsers_map_t _parsers;
    actions_map_t _actions;

private:
    explicit grammar_t(const parsers_map_t &p, const actions_map_t &a)
      : _parsers(p),
        _actions(a)
    {}
};

typedef std::shared_ptr<const grammar_t> grammar_ptr_t;


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_GRAMMAR_H
