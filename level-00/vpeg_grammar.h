//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VPEG_GRAMMAR_H
#define VPEG_GRAMMAR_H

#include "voidc_quark.h"
#include "vpeg_parser.h"

#include <utility>

#include <immer/map.hpp>


//---------------------------------------------------------------------
namespace vpeg
{

extern "C" typedef void (*grammar_action_fun_t)(std::any *ret, const std::any *args, size_t count);


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class grammar_t
{
public:
    using parsers_map_t = immer::map<v_quark_t, std::pair<parser_sptr_t, bool>>;
    using actions_map_t = immer::map<v_quark_t, grammar_action_fun_t>;
    using values_map_t  = immer::map<v_quark_t, std::any>;

public:
    grammar_t()  = default;
    ~grammar_t() = default;

public:
    grammar_t(const grammar_t &gr)
      : _hash(gr.hash),
        _parsers(gr.parsers),
        _actions(gr.actions),
        _values(gr.values)
    {}

    grammar_t &operator=(const grammar_t &gr)
    {
        _hash    = gr.hash;
        _parsers = gr.parsers;
        _actions = gr.actions;
        _values  = gr.values;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    grammar_t set_parser(v_quark_t q_name, const parser_sptr_t &parser, bool leftrec=false) const
    {
        return  grammar_t(_parsers.set(q_name, {parser, leftrec}), actions, values);
    }

    grammar_t set_parser(const char *name, const parser_sptr_t &parser, bool leftrec=false) const
    {
        return  set_parser(v_quark_from_string(name), parser, leftrec);
    }

    grammar_t set_action(v_quark_t q_name, grammar_action_fun_t fun) const
    {
        return  grammar_t(parsers, _actions.set(q_name, fun), values);
    }

    grammar_t set_action(const char *name, grammar_action_fun_t fun) const
    {
        return  set_action(v_quark_from_string(name), fun);
    }

    grammar_t set_value(v_quark_t q_name, const std::any &val) const
    {
        return  grammar_t(parsers, actions, _values.set(q_name, val));
    }

    grammar_t set_value(const char *name, const std::any &val) const
    {
        return  set_value(v_quark_from_string(name), val);
    }

public:
    grammar_t erase_parser(v_quark_t q_name) const
    {
        return  grammar_t(_parsers.erase(q_name), actions, values);
    }

    grammar_t erase_parser(const char *name) const
    {
        return  erase_parser(v_quark_from_string(name));
    }

    grammar_t erase_action(v_quark_t q_name) const
    {
        return  grammar_t(parsers, _actions.erase(q_name), values);
    }

    grammar_t erase_action(const char *name) const
    {
        return  erase_action(v_quark_from_string(name));
    }

    grammar_t erase_value(v_quark_t q_name) const
    {
        return  grammar_t(parsers, actions, _values.erase(q_name));
    }

    grammar_t erase_value(const char *name) const
    {
        return  erase_value(v_quark_from_string(name));
    }

public:
    std::any parse(v_quark_t q_name, context_t &ctx) const;

public:
    const size_t &hash = _hash;

    void check_hash(void);

public:
    const parsers_map_t &parsers = _parsers;
    const actions_map_t &actions = _actions;
    const values_map_t  &values  = _values;

private:
    size_t _hash = size_t(-1);

private:
    parsers_map_t _parsers;
    actions_map_t _actions;
    values_map_t  _values;

private:
    explicit grammar_t(const parsers_map_t &p, const actions_map_t &a, const values_map_t &v)
      : _parsers(p),
        _actions(a),
        _values(v)
    {}
};

typedef std::shared_ptr<const grammar_t> grammar_sptr_t;


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_GRAMMAR_H

