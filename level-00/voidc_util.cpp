//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_util.h"

#include "voidc_dllexport.h"
#include "voidc_types.h"
#include "voidc_target.h"

#include <cassert>
#include <string>
#include <map>
#include <set>

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
using v_util_function_dict_t = std::map<v_type_t *, std::string>;

extern "C"
{

VOIDC_DLLEXPORT_BEGIN_VARIABLE

v_util_function_dict_t v_util_initialize_dict;
v_util_function_dict_t v_util_terminate_dict;

v_util_function_dict_t v_util_copy_dict;
v_util_function_dict_t v_util_move_dict;

v_util_function_dict_t v_util_empty_dict;

v_util_function_dict_t v_util_kind_dict;

v_util_function_dict_t v_util_std_any_get_value_dict;
v_util_function_dict_t v_util_std_any_get_pointer_dict;
v_util_function_dict_t v_util_std_any_set_value_dict;
v_util_function_dict_t v_util_std_any_set_pointer_dict;

v_util_function_dict_t v_util_make_list_nil_dict;
v_util_function_dict_t v_util_make_list_dict;
v_util_function_dict_t v_util_list_append_dict;
v_util_function_dict_t v_util_list_get_size_dict;
v_util_function_dict_t v_util_list_get_items_dict;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
namespace utility
{

//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_init_term_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                           const ast_expr_list_sptr_t *args, int count
                          )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMValueRef values[2];

    values[0] = lctx.result_value;

    if ((*args)->data.size() == 1)
    {
        values[1] = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);
    }
    else
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[1]->accept(*vis);

        values[1] = lctx.result_value;
    }

    LLVMBuildCall(gctx.builder, f, values, 2, "");
}

//---------------------------------------------------------------------
static
void v_copy_move_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                           const ast_expr_list_sptr_t *args, int count
                          )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMValueRef values[3];

    values[0] = lctx.result_value;

    for (int i=1; i<3; ++i)
    {
        if ((*args)->data.size() <= i)
        {
            values[i] = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);
        }
        else
        {
            lctx.result_type = UNREFERENCE_TAG;

            (*args)->data[i]->accept(*vis);

            values[i] = lctx.result_value;
        }
    }

    LLVMBuildCall(gctx.builder, f, values, 3, "");
}

//---------------------------------------------------------------------
static
void v_empty_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                       const ast_expr_list_sptr_t *args, int count
                      )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    auto v = LLVMBuildCall(gctx.builder, f, &lctx.result_value, 1, "");

    lctx.result_type = tt;

    lctx.adopt_result(gctx.bool_type, v);
}


//---------------------------------------------------------------------
static
void v_kind_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                      const ast_expr_list_sptr_t *args, int count
                     )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    auto v = LLVMBuildCall(gctx.builder, f, &lctx.result_value, 1, "");

    lctx.result_type = tt;

    lctx.adopt_result(gctx.int_type, v);
}


//---------------------------------------------------------------------
static
void v_std_any_get_value_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                                   const ast_expr_list_sptr_t *args, int count
                                  )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto tt = lctx.result_type;

    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[0]->accept(*vis);            //- Type

    assert(lctx.result_value == nullptr);

    auto type = lctx.result_type;

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[1]->accept(*vis);

    auto v = LLVMBuildCall(gctx.builder, f, &lctx.result_value, 1, "");

    lctx.result_type = tt;

    lctx.adopt_result(type, v);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                                     const ast_expr_list_sptr_t *args, int count
                                    )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto tt = lctx.result_type;

    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[0]->accept(*vis);            //- Type

    assert(lctx.result_value == nullptr);

    auto type = lctx.result_type;

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[1]->accept(*vis);

    auto v = LLVMBuildCall(gctx.builder, f, &lctx.result_value, 1, "");

    auto adsp = LLVMGetPointerAddressSpace(LLVMTypeOf(v));

    type = gctx.make_pointer_type(type, adsp);

    lctx.result_type = tt;

    lctx.adopt_result(type, v);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                                   const ast_expr_list_sptr_t *args, int count
                                  )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    LLVMValueRef values[2];

    for (int i=0; i<2; ++i)
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    const char *fun = dict.at(lctx.result_type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMBuildCall(gctx.builder, f, values, 2, "");
}


//---------------------------------------------------------------------
static
void v_std_any_set_pointer_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                                     const ast_expr_list_sptr_t *args, int count
                                    )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    LLVMValueRef values[2];

    for (int i=0; i<2; ++i)
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMBuildCall(gctx.builder, f, values, 2, "");
}


//---------------------------------------------------------------------
static
void v_make_list_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                           const ast_expr_list_sptr_t *args, int count
                          )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f = nullptr;
    v_type_t    *t = nullptr;

    if (!lctx.obtain_identifier(fun, t, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    if (auto pt = dynamic_cast<v_type_pointer_t *>(t))
    {
        t = pt->element_type();
    }

    auto ft = static_cast<v_type_function_t *>(t);

    auto par_count = ft->param_count();
    auto par_types = ft->param_types();

    auto arg_count = (*args)->data.size();


    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    values[0] = lctx.result_value;

    for (int i=1; i<arg_count; ++i)
    {
        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    LLVMBuildCall(gctx.builder, f, values.get(), arg_count, "");
}


//---------------------------------------------------------------------
static
void v_list_append_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                             const ast_expr_list_sptr_t *args, int count
                            )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMValueRef values[4];

    values[0] = lctx.result_value;

    for (int i=1; i<4; ++i)
    {
        if ((*args)->data.size() <= i)
        {
            values[i] = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);
        }
        else
        {
            lctx.result_type = UNREFERENCE_TAG;

            (*args)->data[i]->accept(*vis);

            values[i] = lctx.result_value;
        }
    }

    LLVMBuildCall(gctx.builder, f, values, 4, "");
}


//---------------------------------------------------------------------
static
void v_list_get_size_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                               const ast_expr_list_sptr_t *args, int count
                              )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    auto v = LLVMBuildCall(gctx.builder, f, &lctx.result_value, 1, "");

    lctx.result_type = tt;

    lctx.adopt_result(gctx.int_type, v);
}


//---------------------------------------------------------------------
static
void v_list_get_items_intrinsic(const visitor_sptr_t *vis, void *void_dict,
                                const ast_expr_list_sptr_t *args, int count
                               )
{
    auto &dict = *reinterpret_cast<v_util_function_dict_t *>(void_dict);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_identifier(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMValueRef values[4];

    values[0] = lctx.result_value;

    for (int i=1; i<4; ++i)
    {
        if ((*args)->data.size() <= i)
        {
            values[i] = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);
        }
        else
        {
            lctx.result_type = UNREFERENCE_TAG;

            (*args)->data[i]->accept(*vis);

            values[i] = lctx.result_value;
        }
    }

    LLVMBuildCall(gctx.builder, f, values, 4, "");
}


//---------------------------------------------------------------------
//- Static init/term ...
//---------------------------------------------------------------------
void static_initialize(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

#define DEF2(name, fname) \
    gctx.decls.intrinsics_insert({"v_" #name, {(void *)fname, &v_util_##name##_dict}});

#define DEF(name) DEF2(name, v_##name##_intrinsic)

    DEF2(initialize, v_init_term_intrinsic)
    DEF2(terminate,  v_init_term_intrinsic)

    DEF2(copy, v_copy_move_intrinsic)
    DEF2(move, v_copy_move_intrinsic)

    DEF(empty)

    DEF(kind)

    DEF(std_any_get_value)
    DEF(std_any_get_pointer)
    DEF(std_any_set_value)
    DEF(std_any_set_pointer)

    DEF2(make_list_nil, v_make_list_intrinsic)
    DEF2(make_list,     v_make_list_intrinsic)

    DEF(list_append)
    DEF(list_get_size)
    DEF(list_get_items)

#undef DEF
#undef DEF2

    //-----------------------------------------------------------------
    auto add_type = [&gctx](const char *raw_name, v_type_t *type)
    {
        gctx.decls.constants_insert({raw_name, type});

        gctx.decls.symbols_insert({raw_name, gctx.opaque_type_type});

        gctx.add_symbol_value(raw_name, type);
    };

#define DEF(ctype, name) \
    static_assert((sizeof(ctype) % sizeof(intptr_t)) == 0); \
    v_type_t *name##_content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(ctype)/sizeof(intptr_t)); \
    auto opaque_##name##_type = gctx.make_struct_type("struct.v_util_opaque_" #name); \
    opaque_##name##_type->set_body(&name##_content_type, 1, false); \
    add_type("v_util_opaque_" #name, opaque_##name##_type);

    DEF(std::any, std_any)
    DEF(std::string, std_string)
    DEF(v_util_function_dict_t, function_dict_t)

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
VOIDC_DEFINE_INITIALIZE_IMPL(v_util_function_dict_t, v_util_initialize_function_dict_impl)
VOIDC_DEFINE_TERMINATE_IMPL(v_util_function_dict_t, v_util_terminate_function_dict_impl)
VOIDC_DEFINE_COPY_IMPL(v_util_function_dict_t, v_util_copy_function_dict_impl)
VOIDC_DEFINE_MOVE_IMPL(v_util_function_dict_t, v_util_move_function_dict_impl)

bool v_util_empty_function_dict_impl(const v_util_function_dict_t *ptr)
{
    return ptr->empty();
}

const char *v_util_function_dict_get(const v_util_function_dict_t *ptr, v_type_t *type)
{
    auto it = ptr->find(type);

    if (it != ptr->end())
    {
        return it->second.c_str();
    }

    return nullptr;
}

void v_util_function_dict_set(v_util_function_dict_t *ptr, v_type_t *type, const char *fun_name)
{
    (*ptr)[type] = fun_name;
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

void v_std_string_set(std::string *ptr, const char *str)
{
    *ptr = str;
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



