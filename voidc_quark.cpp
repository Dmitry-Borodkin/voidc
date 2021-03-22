//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_quark.h"

#include <cstdio>
#include <cassert>

#include <unordered_map>

#include <immer/vector_transient.hpp>


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
static std::unordered_map<std::string, v_quark_t> voidc_quark_from_string;

static immer::vector_transient<const char *> voidc_quark_to_string;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
v_quark_t
v_quark_from_string(const char *str)
{
    if (str == nullptr) return 0;

    {   auto it = voidc_quark_from_string.find(str);

        if (it != voidc_quark_from_string.end())
        {
            return  it->second + 1;
        }
    }

    assert(voidc_quark_from_string.size() == voidc_quark_to_string.size());

    auto ret = v_quark_t(voidc_quark_from_string.size());

    voidc_quark_from_string[str] = ret;

    voidc_quark_to_string.push_back( voidc_quark_from_string.find(str)->first.c_str() );

    return ret+1;       //- Sic!
}


//---------------------------------------------------------------------
const char *
v_quark_to_string(v_quark_t vq)
{
    if (vq == 0)  return nullptr;

    return  voidc_quark_to_string[vq-1];        //- Sic!
}



