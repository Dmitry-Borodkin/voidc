//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_COMPILER_H
#define VOIDC_COMPILER_H

#include "voidc_visitor.h"
#include "voidc_dllexport.h"


//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_VARIABLE

extern visitor_sptr_t voidc_compiler;
extern visitor_sptr_t voidc_type_calc;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- Compilers (level 0) ...
//---------------------------------------------------------------------
visitor_sptr_t make_voidc_compiler(void);
visitor_sptr_t make_voidc_type_calc(void);


#endif  //- VOIDC_COMPILER_H
