//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_quark.h"

#include <utility>

#include <immer/map.hpp>


//---------------------------------------------------------------------
class voidc_visitor_t;

typedef std::shared_ptr<const voidc_visitor_t> visitor_sptr_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_t : public std::enable_shared_from_this<voidc_visitor_t>
{
public:
    using void_methods_map_t = immer::map<v_quark_t, std::pair<void *, void *>>;

public:
    voidc_visitor_t()  = default;
    ~voidc_visitor_t() = default;

public:
    voidc_visitor_t(const voidc_visitor_t &vis)
      : _void_methods(vis.void_methods)
    {}

    voidc_visitor_t &operator=(const voidc_visitor_t &vis)
    {
        _void_methods = vis.void_methods;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    voidc_visitor_t set_void_method(v_quark_t q, void *void_fun, void *aux=nullptr) const
    {
        return  voidc_visitor_t(void_methods.set(q, {void_fun, aux}));
    }

public:
    const void_methods_map_t &void_methods = _void_methods;

public:
    template<typename FunT, typename... Targs>
    void visit(v_quark_t q, Targs... args) const
    {
//      printf("visit: %s\n", v_quark_to_string(q));

        auto self = this->shared_from_this();

        if (auto *void_m = void_methods.find(q))    //- First, try given quark
        {
            auto [void_fun, aux] = *void_m;

            reinterpret_cast<FunT>(void_fun)(&self, aux, args...);
        }
        else    //- Otherwise, try special case at quark 0
        {
            typedef void (*zf_t)(const visitor_sptr_t *, void *);

//          printf("visit: ZERO !!!\n");

            auto [void_zf, aux] = void_methods.at(0);

            reinterpret_cast<zf_t>(void_zf)(&self, aux);
        }
    }

private:
    void_methods_map_t _void_methods;

private:
    explicit voidc_visitor_t(const void_methods_map_t &vm)
      : _void_methods(vm)
    {}
};


#endif  //- VOIDC_VISITOR_H
