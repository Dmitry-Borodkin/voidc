//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_COMPILER_H
#define VOIDC_COMPILER_H

#include "voidc_visitor.h"


//---------------------------------------------------------------------
//- Compilers (level 0) ...
//---------------------------------------------------------------------
visitor_t make_voidc_compiler(void);
visitor_t make_target_compiler(void);


#endif  //- VOIDC_COMPILER_H
