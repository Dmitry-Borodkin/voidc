//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_visitor.h"

#include "voidc_util.h"
#include "voidc_types.h"
#include "voidc_target.h"
#include "voidc_compiler.h"

#include <cstdio>
#include <cassert>


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
void
voidc_visitor_data_t::static_initialize(void)
{
    static_assert((sizeof(visitor_t) % sizeof(intptr_t)) == 0);

    auto &vctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(visitor_t)/sizeof(intptr_t));

    auto visitor_type = vctx.make_struct_type("voidc_visitor_t");

    visitor_type->set_body(&content_type, 1, false);

    vctx.initialize_type("voidc_visitor_t", visitor_type);


    voidc_compiler = make_voidc_compiler();
}

//-----------------------------------------------------------------
void
voidc_visitor_data_t::static_terminate(void)
{
    voidc_compiler.reset();
}


//---------------------------------------------------------------------
//- !!!
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(visitor_t, voidc_initialize_visitor_impl)
VOIDC_DEFINE_TERMINATE_IMPL(visitor_t, voidc_terminate_visitor_impl)
VOIDC_DEFINE_COPY_IMPL(visitor_t, voidc_copy_visitor_impl)
VOIDC_DEFINE_MOVE_IMPL(visitor_t, voidc_move_visitor_impl)
VOIDC_DEFINE_EMPTY_IMPL(visitor_t, voidc_empty_visitor_impl)
VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(visitor_t, voidc_std_any_get_pointer_visitor_impl)
VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(visitor_t, voidc_std_any_set_pointer_visitor_impl)


//---------------------------------------------------------------------
void
voidc_make_visitor(visitor_t *ret)
{
    *ret = std::make_shared<const voidc_visitor_data_t>();
}


//---------------------------------------------------------------------
void *
voidc_visitor_get_void_method(const visitor_t *ptr, v_quark_t quark, void **aux_ptr)
{
    if (auto *vm = (*ptr)->void_methods.find(quark))
    {
        auto [void_fun, aux] = *vm;

        if (aux_ptr)  *aux_ptr = aux;

        return void_fun;
    }
    else
    {
        return nullptr;
    }
}

void
voidc_visitor_set_void_method(visitor_t *dst, const visitor_t *src, v_quark_t quark, void *void_fun, void *aux)
{
    auto visitor = (*src)->set_void_method(quark, void_fun, aux);

    *dst = std::make_shared<const voidc_visitor_data_t>(visitor);
}


//---------------------------------------------------------------------
void *
voidc_visitor_get_intrinsic(const visitor_t *ptr, const char *name, void **aux_ptr)
{
    if (auto *vm = (*ptr)->intrinsics.find(name))
    {
        auto [void_fun, aux] = *vm;

        if (aux_ptr)  *aux_ptr = aux;

        return void_fun;
    }
    else
    {
        return nullptr;
    }
}

void
voidc_visitor_set_intrinsic(visitor_t *dst, const visitor_t *src, const char *name, void *void_fun, void *aux)
{
    auto visitor = (*src)->set_intrinsic(name, void_fun, aux);

    *dst = std::make_shared<const voidc_visitor_data_t>(visitor);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


