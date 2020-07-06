//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_quark.h"

#include <immer/map.hpp>


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_t
{
public:
    using methods_map_t = immer::map<v_quark_t, void *>;

public:
    voidc_visitor_t()  = default;
    ~voidc_visitor_t() = default;

public:
    voidc_visitor_t(const voidc_visitor_t &vis)
      : _methods(vis.methods)
    {}

    voidc_visitor_t &operator=(const voidc_visitor_t &vis)
    {
        _methods = vis.methods;

        return *this;
    }

public:
    voidc_visitor_t set_method(v_quark_t q, void *void_method)
    {
        return  voidc_visitor_t(_methods.set(q, void_method));
    }

public:
    const methods_map_t &methods = _methods;

private:
    methods_map_t _methods;

private:
    explicit voidc_visitor_t(const methods_map_t &m)
      : _methods(m)
    {}
};


#endif  //- VOIDC_VISITOR_H
