//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
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
    struct typeprops_hash_t
    {
        std::size_t operator ()(const std::pair<v_type_t *, v_quark_t> &k) const noexcept
        {
            auto h0 = std::hash<v_type_t *>{}(k.first);
            auto h1 = std::hash<v_quark_t>{}(k.second);
            return  h1 ^ (h0 << 1);
        }
    };

public:
    using void_methods_map_t = immer::map<v_quark_t, std::pair<void *, void *>>;

    using properties_map_t   = immer::map<v_quark_t, std::any>;

    using type_properties_t  = immer::map<std::pair<v_type_t *, v_quark_t>, std::any, typeprops_hash_t>;

public:
    voidc_visitor_data_t()  = default;
    ~voidc_visitor_data_t() = default;

public:
    voidc_visitor_data_t(const voidc_visitor_data_t &vis)
      : _void_methods(vis.void_methods),
        _properties(vis.properties),
        _type_properties(vis.type_properties)
    {}

    voidc_visitor_data_t &operator=(const voidc_visitor_data_t &vis)
    {
        _void_methods    = vis.void_methods;
        _properties      = vis.properties;
        _type_properties = vis.type_properties;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    voidc_visitor_data_t set_void_method(v_quark_t q, void *void_fun, void *aux=nullptr) const
    {
        return  voidc_visitor_data_t(void_methods.set(q, {void_fun, aux}), properties, type_properties);
    }

    voidc_visitor_data_t set_property(v_quark_t name, const std::any &prop) const
    {
        return  voidc_visitor_data_t(void_methods, properties.set(name, prop), type_properties);
    }

    voidc_visitor_data_t set_type_property(v_type_t *type, v_quark_t quark, const std::any &prop) const
    {
        return  voidc_visitor_data_t(void_methods, properties, type_properties.set({type, quark}, prop));
    }

public:
    const void_methods_map_t &void_methods    = _void_methods;
    const properties_map_t   &properties      = _properties;
    const type_properties_t  &type_properties = _type_properties;

public:
    static
    void visit(const visitor_t &vis, const ast_base_t &obj)
    {
        auto q = obj->method_tag();

//      printf("visit: %p, %s\n", &vis, v_quark_to_string(q));

        auto &[void_fun, aux] = vis->void_methods.at(q);

        typedef void (*FunT)(const visitor_t *, void *, const ast_base_t *);

        reinterpret_cast<FunT>(void_fun)(&vis, aux, &obj);
    }

private:
    void_methods_map_t _void_methods;
    properties_map_t   _properties;
    type_properties_t  _type_properties;

private:
    explicit voidc_visitor_data_t(const void_methods_map_t &vm,
                                  const properties_map_t   &pm,
                                  const type_properties_t  &tp)
      : _void_methods(vm),
        _properties(pm),
        _type_properties(tp)
    {}
};


#endif  //- VOIDC_VISITOR_H
