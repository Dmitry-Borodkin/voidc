//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
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

extern visitor_t voidc_compiler;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- Compilers (level 0) ...
//---------------------------------------------------------------------
visitor_t make_voidc_compiler(void);


#endif  //- VOIDC_COMPILER_H
