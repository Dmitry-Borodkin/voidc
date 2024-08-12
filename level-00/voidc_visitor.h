//---------------------------------------------------------------------
//- Copyright (C) 2020-2024 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_ast.h"

#include <utility>
#include <string>

#include <immer/map.hpp>


//---------------------------------------------------------------------
class voidc_visitor_data_t;

typedef std::shared_ptr<const voidc_visitor_data_t> visitor_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_data_t
{
public:
    using void_methods_map_t = immer::map<v_quark_t, std::pair<void *, void *>>;

    using properties_map_t   = immer::map<v_quark_t, std::any>;

public:
    voidc_visitor_data_t()  = default;
    ~voidc_visitor_data_t() = default;

public:
    voidc_visitor_data_t(const voidc_visitor_data_t &vis)
      : _void_methods(vis.void_methods),
        _properties(vis.properties)
    {}

    voidc_visitor_data_t &operator=(const voidc_visitor_data_t &vis)
    {
        _void_methods = vis.void_methods;
        _properties   = vis.properties;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    voidc_visitor_data_t set_void_method(v_quark_t q, void *void_fun, void *aux=nullptr) const
    {
        return  voidc_visitor_data_t(void_methods.set(q, {void_fun, aux}), properties);
    }

    voidc_visitor_data_t set_property(v_quark_t name, const std::any &prop) const
    {
        return  voidc_visitor_data_t(void_methods, properties.set(name, prop));
    }

public:
    const void_methods_map_t &void_methods = _void_methods;
    const properties_map_t   &properties   = _properties;

public:
    static
    void visit(const visitor_t &vis, const ast_base_t &obj)
    {
        auto q = obj->tag();

//      printf("visit: %p, %s\n", &vis, v_quark_to_string(q));

        auto &[void_fun, aux] = vis->void_methods.at(q);

//      printf("visit: %p, %s, %p, %p\n", &vis, v_quark_to_string(q), void_fun, aux);

        typedef void (*FunT)(void *, const visitor_t *, const ast_base_t *);

        reinterpret_cast<FunT>(void_fun)(aux, &vis, &obj);
    }

private:
    void_methods_map_t _void_methods;
    properties_map_t   _properties;

private:
    explicit voidc_visitor_data_t(const void_methods_map_t &vm,
                                  const properties_map_t   &pm)
      : _void_methods(vm),
        _properties(pm)
    {}
};


#endif  //- VOIDC_VISITOR_H
