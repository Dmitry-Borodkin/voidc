//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_quark.h"

#include <immer/map.hpp>


//---------------------------------------------------------------------
class voidc_visitor_t;

typedef std::shared_ptr<const voidc_visitor_t> visitor_ptr_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_t
{
public:
    using void_methods_map_t = immer::map<v_quark_t, void *>;

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
    voidc_visitor_t set_void_method(v_quark_t q, void *void_method) const
    {
        return  voidc_visitor_t(void_methods.set(q, void_method));
    }

public:
    const void_methods_map_t &void_methods = _void_methods;

public:
    template<typename FunT, typename... Targs>
    void visit(v_quark_t q, const visitor_ptr_t &self, void *aux, Targs... args) const
    {
        if (auto *void_f = void_methods.find(q))    //- First, try given quark
        {
            reinterpret_cast<FunT>(*void_f)(&self, aux, args...);
        }
        else    //- Otherwise, try special case at quark 0
        {
            typedef void (*zf_t)(const visitor_ptr_t *, void *);

            reinterpret_cast<zf_t>(void_methods.at(0))(&self, aux);
        }
    }

private:
    void_methods_map_t _void_methods;

private:
    explicit voidc_visitor_t(const void_methods_map_t &vm)
      : _void_methods(vm)
    {}
};

//typedef std::shared_ptr<const voidc_visitor_t> visitor_ptr_t;


#endif  //- VOIDC_VISITOR_H
