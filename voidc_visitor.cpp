//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
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
voidc_visitor_t::static_initialize(void)
{
    static_assert((sizeof(visitor_sptr_t) % sizeof(intptr_t)) == 0);

    auto &gctx = *voidc_global_ctx_t::voidc;

    v_type_t *content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(visitor_sptr_t)/sizeof(intptr_t));

    auto visitor_sptr_type = gctx.make_struct_type("struct.voidc_opaque_visitor_sptr");

    visitor_sptr_type->set_body(&content_type, 1, false);
    gctx.add_symbol("voidc_opaque_visitor_sptr", gctx.opaque_type_type, (void *)visitor_sptr_type);


    voidc_compiler = make_voidc_compiler();
}

//-----------------------------------------------------------------
void
voidc_visitor_t::static_terminate(void)
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
VOIDC_DEFINE_INITIALIZE_IMPL(visitor_sptr_t, voidc_initialize_visitor_impl)
VOIDC_DEFINE_TERMINATE_IMPL(visitor_sptr_t, voidc_terminate_visitor_impl)
VOIDC_DEFINE_COPY_IMPL(visitor_sptr_t, voidc_copy_visitor_impl)
VOIDC_DEFINE_MOVE_IMPL(visitor_sptr_t, voidc_move_visitor_impl)
VOIDC_DEFINE_EMPTY_IMPL(visitor_sptr_t, voidc_empty_visitor_impl)
VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(visitor_sptr_t, voidc_std_any_get_pointer_visitor_impl)
VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(visitor_sptr_t, voidc_std_any_set_pointer_visitor_impl)


//---------------------------------------------------------------------
void
voidc_make_visitor(visitor_sptr_t *ret)
{
    *ret = std::make_shared<const voidc_visitor_t>();
}


//---------------------------------------------------------------------
void *
voidc_visitor_get_void_method(const visitor_sptr_t *ptr, v_quark_t quark)
{
    if (auto *vm = (*ptr)->void_methods.find(quark))
    {
        return *vm;
    }
    else
    {
        return nullptr;
    }
}

void
voidc_visitor_set_void_method(visitor_sptr_t *dst, const visitor_sptr_t *src, v_quark_t quark, void *void_method)
{
    auto visitor = (*src)->set_void_method(quark, void_method);

    *dst = std::make_shared<const voidc_visitor_t>(visitor);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


