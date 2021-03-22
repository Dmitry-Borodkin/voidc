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

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- Compiler (level 0) ...
//---------------------------------------------------------------------
visitor_sptr_t make_voidc_compiler(void);


#endif  //- VOIDC_COMPILER_H
