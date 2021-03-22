//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_UTIL_H
#define VOIDC_UTIL_H

#include <memory>
#include <algorithm>
#include <any>


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

#define VOIDC_DEFINE_TERMINATE_IMPL(val_t, fun_name) \
void fun_name(val_t *ptr, int count) \
{ \
    std::destroy_n(ptr, size_t(count)); \
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

#define VOIDC_DEFINE_EMPTY_IMPL(val_t, fun_name) \
bool fun_name(const val_t *ptr) \
{ \
    return  !bool(*ptr); \
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
