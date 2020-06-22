//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_UTIL_H
#define VOIDC_UTIL_H

#include "voidc_llvm.h"

#include <string>
#include <map>
#include <any>

#include <llvm-c/Core.h>

#include "voidc_dllexport.h"


//---------------------------------------------------------------------
using v_util_function_dict_t = std::map<LLVMTypeRef, std::string>;

extern "C"
{

VOIDC_DLLEXPORT_BEGIN_VARIABLE

extern v_util_function_dict_t v_util_initialize_dict;
extern v_util_function_dict_t v_util_reset_dict;
extern v_util_function_dict_t v_util_copy_dict;
extern v_util_function_dict_t v_util_move_dict;
extern v_util_function_dict_t v_util_kind_dict;
extern v_util_function_dict_t v_util_std_any_get_value_dict;
extern v_util_function_dict_t v_util_std_any_get_pointer_dict;
extern v_util_function_dict_t v_util_std_any_set_value_dict;
extern v_util_function_dict_t v_util_std_any_set_pointer_dict;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
namespace utility
{

void static_initialize(void);
void static_terminate(void);

//---------------------------------------------------------------------
}   //- namespace utility



//-----------------------------------------------------------------
//- !!!
//-----------------------------------------------------------------
#define VOIDC_DEFINE_INITIALIZE_IMPL(val_t, fun_name) \
void fun_name(val_t *ptr, int count) \
{ \
    std::uninitialized_default_construct_n(ptr, size_t(count)); \
}

#define VOIDC_DEFINE_RESET_IMPL(val_t, fun_name) \
void fun_name(val_t *ptr, int count) \
{ \
    for (int i=0; i<count; ++i) ptr[i].reset(); \
}

#define VOIDC_DEFINE_COPY_IMPL(val_t, fun_name) \
void fun_name(val_t *dst, const val_t *src, int count) \
{ \
    std::copy(src, src+count, dst); \
}

#define VOIDC_DEFINE_MOVE_IMPL(val_t, fun_name) \
void fun_name(val_t *dst, val_t *src, int count) \
{ \
    std::move(src, src+count, dst); \
}


//-----------------------------------------------------------------
#define VOIDC_DEFINE_STD_ANY_GET_VALUE_IMPL(val_t, fun_name) \
val_t fun_name(const std::any *src) \
{ \
    return std::any_cast<val_t>(*src); \
}

#define VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(val_t, fun_name) \
val_t *fun_name(std::any *src) \
{ \
    return std::any_cast<val_t>(src); \
}

#define VOIDC_DEFINE_STD_ANY_SET_VALUE_IMPL(val_t, fun_name) \
void fun_name(std::any *dst, val_t v) \
{ \
    *dst = v; \
}

#define VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(val_t, fun_name) \
void fun_name(std::any *dst, val_t *p) \
{ \
    *dst = *p; \
}




#endif  //- VOIDC_UTIL_H
