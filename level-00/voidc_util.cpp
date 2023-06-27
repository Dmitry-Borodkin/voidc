//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_util.h"

#include "voidc_dllexport.h"
#include "voidc_types.h"
#include "voidc_target.h"
#include "voidc_visitor.h"

#include <cassert>
#include <string>
#include <map>
#include <set>

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
namespace utility
{

//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
static
bool lookup_function_dict(const visitor_t *vis, v_type_t *type, v_quark_t quark,
                          void *&void_fun, void *&aux,
                          LLVMValueRef &f, v_type_t *&ft)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    const v_quark_t *qname = nullptr;

    {   auto &props = type->properties;

        const std::any *any = nullptr;

        auto it = props.find(quark);

        if (it != props.end())  any = &it->second;
        else                    any = (*vis)->type_properties.find({type, quark});

        qname = std::any_cast<v_quark_t>(any);

        if (!qname)  return false;
    }

    for (auto &intrs : {lctx.decls.intrinsics, (*vis)->intrinsics})
    {
        if (auto p = intrs.find(*qname))
        {
            void_fun = p->first;
            aux      = p->second;

            return true;
        }
    }

    void_fun = nullptr;

    v_type_t *t;

    if (!lctx.obtain_identifier(*qname, t, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + v_quark_to_string(*qname));
    }

    ft = static_cast<v_type_pointer_t *>(t)->element_type();

    return true;
}

//---------------------------------------------------------------------
extern "C"
typedef void (*intrinsic_t)(const visitor_t *vis, void *aux, const ast_base_t *self,
                            v_type_t *type, LLVMValueRef value);


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_universal_intrinsic(const visitor_t *vis, void *void_quark, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto ttag = lctx.result_type;
    auto vtag = lctx.result_value;

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[0]);

    auto res_type  = lctx.result_type;
    auto res_value = lctx.result_value;

    auto type = static_cast<v_type_pointer_t *>(res_type)->element_type();

    void *void_fun;
    void *aux;

    LLVMValueRef f;
    v_type_t    *t;

    auto ok = lookup_function_dict(vis, type, quark, void_fun, aux, f, t);
    assert(ok);

    if (void_fun)       //- Compile-time intrinsic?
    {
        lctx.result_type  = ttag;
        lctx.result_value = vtag;

        reinterpret_cast<intrinsic_t>(void_fun)(vis, aux, self, res_type, res_value);

        return;
    }

    //- Function call

    auto ft = static_cast<v_type_function_t *>(t);

    auto par_count = ft->param_count();
    auto par_types = ft->param_types();

    auto arg_count = args->data.size();

    auto val_count = std::max(size_t(par_count), arg_count);

    auto values = std::make_unique<LLVMValueRef[]>(val_count);

    for (int i=0; i<val_count; ++i)
    {
        v_type_t    *ti = nullptr;
        LLVMValueRef vi = nullptr;

        if (i == 0)
        {
            ti = res_type;
            vi = res_value;
        }
        else if (arg_count <= i)
        {
            ti = gctx.size_t_type;                                      //- Sic!!!
            vi = LLVMConstInt(ti->llvm_type(), 1, false);               //- Sic!
        }

        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        lctx.result_value = 0;

        if (vi)
        {
            lctx.adopt_result(ti, vi);
        }
        else
        {
            voidc_visitor_data_t::visit(*vis, args->data[i]);
        }

        values[i] = lctx.result_value;
    }

    auto v = LLVMBuildCall2(gctx.builder, ft->llvm_type(), f, values.get(), val_count, "");

    lctx.result_type  = ttag;
    lctx.result_value = vtag;

    lctx.adopt_result(ft->return_type(), v);
}


//---------------------------------------------------------------------
static
void v_std_any_get_value_intrinsic(const visitor_t *vis, void *void_quark, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto ttag = lctx.result_type;
    auto vtag = lctx.result_value;

    lctx.result_type = INVIOLABLE_TAG;
//  lctx.result_type  = voidc_global_ctx_t::voidc->static_type_type;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[0]);       //- Type

    assert(lctx.result_type == voidc_global_ctx_t::voidc->static_type_type);

    auto type = reinterpret_cast<v_type_t *>(lctx.result_value);

    void *void_fun;
    void *aux;

    LLVMValueRef f;
    v_type_t    *ft;

    auto ok = lookup_function_dict(vis, type, quark, void_fun, aux, f, ft);
    assert(ok);

    if (void_fun)       //- Compile-time intrinsic?
    {
        auto tr = lctx.result_type;
        auto vr = lctx.result_value;

        lctx.result_type  = ttag;
        lctx.result_value = vtag;

        reinterpret_cast<intrinsic_t>(void_fun)(vis, aux, self, tr, vr);

        return;
    }

    //- Function call

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[1]);

    auto v = LLVMBuildCall2(gctx.builder, ft->llvm_type(), f, &lctx.result_value, 1, "");

    lctx.result_type  = ttag;
    lctx.result_value = vtag;

    lctx.adopt_result(type, v);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer_intrinsic(const visitor_t *vis, void *void_quark, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto ttag = lctx.result_type;
    auto vtag = lctx.result_value;

    lctx.result_type = INVIOLABLE_TAG;
//  lctx.result_type  = voidc_global_ctx_t::voidc->static_type_type;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[0]);       //- Type

    assert(lctx.result_type == voidc_global_ctx_t::voidc->static_type_type);

    auto type = reinterpret_cast<v_type_t *>(lctx.result_value);

    void *void_fun;
    void *aux;

    LLVMValueRef f;
    v_type_t    *ft;

    auto ok = lookup_function_dict(vis, type, quark, void_fun, aux, f, ft);
    assert(ok);

    if (void_fun)       //- Compile-time intrinsic?
    {
        auto tr = lctx.result_type;
        auto vr = lctx.result_value;

        lctx.result_type  = ttag;
        lctx.result_value = vtag;

        reinterpret_cast<intrinsic_t>(void_fun)(vis, aux, self, tr, vr);

        return;
    }

    //- Function call

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[1]);

    auto v = LLVMBuildCall2(gctx.builder, ft->llvm_type(), f, &lctx.result_value, 1, "");

    auto adsp = LLVMGetPointerAddressSpace(LLVMTypeOf(v));

    type = gctx.make_pointer_type(type, adsp);

    lctx.result_type  = ttag;
    lctx.result_value = vtag;

    lctx.adopt_result(type, v);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value_intrinsic(const visitor_t *vis, void *void_quark, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[1]);       //- Second argument

    auto type = lctx.result_type;

    void *void_fun;
    void *aux;

    LLVMValueRef f;
    v_type_t    *ft;

    auto ok = lookup_function_dict(vis, type, quark, void_fun, aux, f, ft);
    assert(ok);

    if (void_fun)       //- Compile-time intrinsic?
    {
        reinterpret_cast<intrinsic_t>(void_fun)(vis, aux, self, lctx.result_type, lctx.result_value);

        return;
    }

    //- Function call

    LLVMValueRef values[2];

    values[1] = lctx.result_value;

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[0]);       //- First argument

    values[0] = lctx.result_value;

    LLVMBuildCall2(gctx.builder, ft->llvm_type(), f, values, 2, "");
}

//---------------------------------------------------------------------
static
void v_std_any_set_pointer_intrinsic(const visitor_t *vis, void *void_quark, const ast_base_t *self)
{
    auto &args = static_cast<const ast_expr_call_data_t &>(**self).arg_list;

    auto quark = v_quark_t(uintptr_t(void_quark));

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[1]);       //- Second argument

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    void *void_fun;
    void *aux;

    LLVMValueRef f;
    v_type_t    *ft;

    auto ok = lookup_function_dict(vis, type, quark, void_fun, aux, f, ft);
    assert(ok);

    if (void_fun)       //- Compile-time intrinsic?
    {
        reinterpret_cast<intrinsic_t>(void_fun)(vis, aux, self, lctx.result_type, lctx.result_value);

        return;
    }

    //- Function call

    LLVMValueRef values[2];

    values[1] = lctx.result_value;

    lctx.result_type  = UNREFERENCE_TAG;
    lctx.result_value = 0;

    voidc_visitor_data_t::visit(*vis, args->data[0]);       //- First argument

    values[0] = lctx.result_value;

    LLVMBuildCall2(gctx.builder, ft->llvm_type(), f, values, 2, "");
}


//---------------------------------------------------------------------
//- Static init/term ...
//---------------------------------------------------------------------
void static_initialize(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto q = v_quark_from_string;

#define DEF2(name, fname) \
    auto name##_q = q("v_" #name); \
    vctx.decls.intrinsics_insert({name##_q, {(void *)fname, (void *)uintptr_t(name##_q)}});

#define DEF_U(name) DEF2(name, v_universal_intrinsic)

#define DEF(name) DEF2(name, v_##name##_intrinsic)

    DEF_U(initialize)
    DEF_U(terminate)

    DEF_U(copy)
    DEF_U(move)

    DEF_U(empty)
    DEF_U(kind)

    DEF(std_any_get_value)
    DEF(std_any_get_pointer)
    DEF(std_any_set_value)
    DEF(std_any_set_pointer)

    DEF_U(make_list_nil)
    DEF_U(make_list)

    DEF_U(list_append)
    DEF_U(list_get_size)
    DEF_U(list_get_item)

    DEF_U(ast_make_generic)
    DEF_U(ast_generic_get_vtable)
    DEF_U(ast_generic_get_object)

#undef DEF
#undef DEF_U
#undef DEF2

    //-----------------------------------------------------------------
#define DEF(ctype, name) \
    static_assert((sizeof(ctype) % sizeof(intptr_t)) == 0); \
    v_type_t *name##_content_type = vctx.make_array_type(vctx.intptr_t_type, sizeof(ctype)/sizeof(intptr_t)); \
    auto name##_q = q("v_" #name "_t"); \
    auto name##_type = vctx.make_struct_type(name##_q); \
    name##_type->set_body(&name##_content_type, 1, false); \
    vctx.initialize_type(name##_q, name##_type);

    DEF(std::any, std_any)
    DEF(std::string, std_string)

#undef DEF
}

//---------------------------------------------------------------------
void static_terminate(void)
{
}


//---------------------------------------------------------------------
}   //- namespace utility



//---------------------------------------------------------------------
//- !!!
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
void v_util_function_dict_set(v_type_t *type, v_quark_t quark, v_quark_t qname)
{
    type->properties[quark] = qname;
}

bool v_util_lookup_function_dict(const visitor_t *vis, v_type_t *type, v_quark_t quark,
                                 void **void_fun, void **void_aux,
                                 v_type_t **ft, LLVMValueRef *fv)
{
    return utility::lookup_function_dict(vis, type, quark, *void_fun, *void_aux, *fv, *ft);
}

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(std::any, v_util_initialize_std_any_impl)
VOIDC_DEFINE_TERMINATE_IMPL(std::any, v_util_terminate_std_any_impl)
VOIDC_DEFINE_COPY_IMPL(std::any, v_util_copy_std_any_impl)
VOIDC_DEFINE_MOVE_IMPL(std::any, v_util_move_std_any_impl)

bool v_util_empty_std_any_impl(const std::any *ptr)
{
    return !ptr->has_value();
}

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(std::string, v_util_initialize_std_string_impl)
VOIDC_DEFINE_TERMINATE_IMPL(std::string, v_util_terminate_std_string_impl)
VOIDC_DEFINE_COPY_IMPL(std::string, v_util_copy_std_string_impl)
VOIDC_DEFINE_MOVE_IMPL(std::string, v_util_move_std_string_impl)

bool v_util_empty_std_string_impl(const std::string *ptr)
{
    return ptr->empty();
}

char *v_std_string_get(std::string *ptr)
{
    return ptr->data();
}

size_t v_std_string_get_size(std::string *ptr)
{
    return ptr->size();
}

char *v_std_string_get_data(std::string *ptr, size_t *len)
{
    *len = ptr->size();

    return ptr->data();
}

void v_std_string_set(std::string *ptr, const char *str)
{
    *ptr = str;
}

void v_std_string_set_data(std::string *ptr, const char *str, size_t len)
{
    *ptr = {str, len};
}

void v_std_string_append(std::string *ptr, const char *str)
{
    ptr->append(str);
}

void v_std_string_append_char(std::string *ptr, char32_t c)
{
    char d[5] = { 0, 0, 0, 0, 0 };

    int r;

    if (c <= 0x7FF)
    {
        if (c <= 0x7F)  r = 0;
        else            r = 1;
    }
    else
    {
        if (c <= 0xFFFF)  r = 2;
        else              r = 3;
    }

    if (r == 0)
    {
        d[0] = c & 0x7F;
    }
    else
    {
        for (int j = 0; j < r; ++j)
        {
            d[r-j] = 0x80 | (c & 0x3F);

            c >>= 6;
        }

        d[0] = (0x1E << (6-r)) | (c & (0x3F >> r));
    }

    ptr->append(d);
}

void v_std_string_append_number(std::string *ptr, intptr_t n)
{
    *ptr += std::to_string(n);
}

//---------------------------------------------------------------------
#define DEF_VAR(c_type, type_tag) \
VOIDC_DEFINE_STD_ANY_GET_VALUE_IMPL(c_type, v_util_std_any_get_value_##type_tag##_impl) \
VOIDC_DEFINE_STD_ANY_SET_VALUE_IMPL(c_type, v_util_std_any_set_value_##type_tag##_impl)

#define DEF_PTR(c_type, type_tag) \
VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(c_type, v_util_std_any_get_pointer_##type_tag##_impl) \
VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(c_type, v_util_std_any_set_pointer_##type_tag##_impl)

#define DEF(c_type, type_tag) \
DEF_VAR(c_type, type_tag) \
DEF_PTR(c_type, type_tag)

DEF(bool,     bool)
DEF(int8_t,   int8_t)
DEF(uint8_t,  uint8_t)
DEF(int16_t,  int16_t)
DEF(uint16_t, uint16_t)
DEF(int32_t,  int32_t)
DEF(uint32_t, uint32_t)
DEF(int64_t,  int64_t)
DEF(uint64_t, uint64_t)

DEF_PTR(std::string, std_string)

#undef DEF
#undef DEF_PTR
#undef DEF_VAR


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- Own tsearch etc. "implementation"...
//---------------------------------------------------------------------
extern "C"
{
    typedef int (*compare_fun_t)(const void *, const void *);
}

struct tree_impl_t
{
    struct compare_t
    {
        explicit compare_t(compare_fun_t &p_fun)
          : compare(p_fun)
        {}

        compare_fun_t &compare;

        bool operator()(const void *lhs, const void *rhs) const
        {
            if (compare)  return  (compare(lhs, rhs) < 0);
            else          return  (uintptr_t(lhs) < uintptr_t(rhs));
        }
    };

    compare_fun_t compare_fun = 0;

    typedef std::set<const void *, compare_t> tree_t;

    tree_t tree = tree_t(compare_t(compare_fun));
};

//---------------------------------------------------------------------
extern "C"
{
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
void *
v_tsearch(const void *key, void **rootp, compare_fun_t compar)
{
    if (!rootp) return nullptr;

    if (!*rootp)  *rootp = new tree_impl_t;

    auto root = reinterpret_cast<tree_impl_t *>(*rootp);

    root->compare_fun = compar;     //- WTF ?!?!?

    auto [it,ok] = root->tree.insert(key);

    return  (void *)&*it;           //- Sic!(?)
}

//---------------------------------------------------------------------
void *
v_tfind(const void *key, void **rootp, compare_fun_t compar)
{
    if (!rootp) return nullptr;

    if (!*rootp)  return nullptr;

    auto root = reinterpret_cast<tree_impl_t *>(*rootp);

    root->compare_fun = compar;     //- WTF ?!?!?

    auto it = root->tree.find(key);

    if (it == root->tree.end()) return nullptr;

    return  (void *)&*it;           //- Sic!(?)
}


//---------------------------------------------------------------------
void
v_tdestroy(void *root_, void (*free_node)(void *nodep))
{
    if (!root_) return;

    auto root = reinterpret_cast<tree_impl_t *>(root_);

    if (free_node)
    {
        for (auto &it: root->tree)   free_node((void *)it);     //- ?
    }

    delete root;
}


//---------------------------------------------------------------------

VOIDC_DLLEXPORT_END
}   //- extern "C"



