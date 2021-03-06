//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_QUARK_H
#define VOIDC_QUARK_H

#include "voidc_dllexport.h"

#include <cstdint>


//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

using v_quark_t = uint32_t;


v_quark_t v_quark_from_string(const char *str);

const char *v_quark_to_string(v_quark_t vq);


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


#endif  //- VOIDC_QUARK_H
