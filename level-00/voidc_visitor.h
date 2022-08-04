//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_quark.h"

#include <utility>
#include <string>

#include <immer/map.hpp>


//---------------------------------------------------------------------
class voidc_visitor_data_t;

typedef std::shared_ptr<const voidc_visitor_data_t> visitor_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_data_t : public std::enable_shared_from_this<voidc_visitor_data_t>
{
public:
    using void_methods_map_t = immer::map<v_quark_t, std::pair<void *, void *>>;

    using intrinsics_map_t = immer::map<std::string, std::pair<void *, void *>>;

public:
    voidc_visitor_data_t()  = default;
    ~voidc_visitor_data_t() = default;

public:
    voidc_visitor_data_t(const voidc_visitor_data_t &vis)
      : _void_methods(vis.void_methods),
        _intrinsics(vis.intrinsics)
    {}

    voidc_visitor_data_t &operator=(const voidc_visitor_data_t &vis)
    {
        _void_methods = vis.void_methods;
        _intrinsics   = vis.intrinsics;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    voidc_visitor_data_t set_void_method(v_quark_t q, void *void_fun, void *aux=nullptr) const
    {
        return  voidc_visitor_data_t(void_methods.set(q, {void_fun, aux}), intrinsics);
    }

    voidc_visitor_data_t set_intrinsic(const std::string &name, void *void_fun, void *aux=nullptr) const
    {
        return  voidc_visitor_data_t(void_methods, intrinsics.set(name, {void_fun, aux}));
    }

public:
    const void_methods_map_t &void_methods = _void_methods;
    const intrinsics_map_t   &intrinsics   = _intrinsics;

public:
    template<typename FunT, typename... Targs>
    void visit(v_quark_t q, Targs... args) const
    {
//      printf("visit: %s\n", v_quark_to_string(q));

        auto &[void_fun, aux] = void_methods.at(q);

        auto self = shared_from_this();

        reinterpret_cast<FunT>(void_fun)(&self, aux, args...);
    }

private:
    void_methods_map_t _void_methods;
    intrinsics_map_t   _intrinsics;

private:
    explicit voidc_visitor_data_t(const void_methods_map_t &vm, const intrinsics_map_t &im)
      : _void_methods(vm),
        _intrinsics(im)
    {}
};


#endif  //- VOIDC_VISITOR_H
