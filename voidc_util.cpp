//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_util.h"

#include "voidc_dllexport.h"
#include "voidc_types.h"
#include "voidc_target.h"

#include <cassert>
#include <string>
#include <map>

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
void v_init_term_helper(const visitor_ptr_t *vis, void *aux,
                        const ast_arg_list_ptr_t &args,
                        const v_util_function_dict_t &dict
                       )
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(args);
    if (args->data.size() < 1  ||  args->data.size() > 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string(args->data.size()));
    }

    assert(lctx.arg_types.empty());

    args->accept(*vis, aux);

    auto type = static_cast<v_type_pointer_t *>(lctx.arg_types[0])->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    if (args->data.size() == 1)
    {
        auto n = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);

        lctx.args.push_back(n);
    }

    LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();
}

//---------------------------------------------------------------------
static
void v_initialize(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    v_init_term_helper(vis, aux, *args, v_util_initialize_dict);
}

//---------------------------------------------------------------------
static
void v_terminate(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    v_init_term_helper(vis, aux, *args, v_util_terminate_dict);
}


//---------------------------------------------------------------------
static
void v_copy_move_helper(const visitor_ptr_t *vis, void *aux,
                        const ast_arg_list_ptr_t &args,
                        const v_util_function_dict_t &dict
                       )
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(args);
    if (args->data.size() < 2  ||  args->data.size() > 3)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string(args->data.size()));
    }

    assert(lctx.arg_types.empty());

    args->accept(*vis, aux);

    auto type = static_cast<v_type_pointer_t *>(lctx.arg_types[0])->element_type();

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    if (args->data.size() == 2)
    {
        auto n = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);

        lctx.args.push_back(n);
    }

    LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();
}

//---------------------------------------------------------------------
static
void v_copy(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    v_copy_move_helper(vis, aux, *args, v_util_copy_dict);
}

//---------------------------------------------------------------------
static
void v_move(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    v_copy_move_helper(vis, aux, *args, v_util_move_dict);
}


//---------------------------------------------------------------------
static
void v_empty(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(lctx.arg_types.empty());

    (*args)->accept(*vis, aux);

    auto type = static_cast<v_type_pointer_t *>(lctx.arg_types[0])->element_type();

    const char *fun = v_util_empty_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    auto v = LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();

    lctx.ret_type  = gctx.bool_type;
    lctx.ret_value = v;
}


//---------------------------------------------------------------------
static
void v_kind(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(lctx.arg_types.empty());

    (*args)->accept(*vis, aux);

    auto type = static_cast<v_type_pointer_t *>(lctx.arg_types[0])->element_type();

    const char *fun = v_util_kind_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    auto v = LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();

    lctx.ret_type  = gctx.int_type;     //- ?
    lctx.ret_value = v;
}


//---------------------------------------------------------------------
static
void v_std_any_get_helper(const visitor_ptr_t *vis, void *aux,
                          const ast_arg_list_ptr_t &args,
                          const v_util_function_dict_t &dict
                         )
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(args);
    if (args->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string(args->data.size()));
    }

    assert(lctx.arg_types.empty());

    auto type = lctx.lookup_type(args->data[0]);
    assert(type);

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    args->data[1]->accept(*vis, aux);

    auto v = LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();

    lctx.ret_value = v;

    if (&dict == &v_util_std_any_get_value_dict)
    {
        lctx.ret_type = type;
    }
    else
    {
        auto adsp = LLVMGetPointerAddressSpace(LLVMTypeOf(v));

        lctx.ret_type = gctx.make_pointer_type(type, adsp);
    }
}

//---------------------------------------------------------------------
static
void v_std_any_get_value(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    v_std_any_get_helper(vis, aux, *args, v_util_std_any_get_value_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    v_std_any_get_helper(vis, aux, *args, v_util_std_any_get_pointer_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(lctx.arg_types.empty());

    (*args)->accept(*vis, aux);

    auto type = lctx.arg_types[1];

    const char *fun = v_util_std_any_set_value_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();
}


//---------------------------------------------------------------------
static
void v_std_any_set_pointer(const visitor_ptr_t *vis, void *aux, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(lctx.arg_types.empty());

    (*args)->accept(*vis, aux);

    auto type = static_cast<v_type_pointer_t *>(lctx.arg_types[1])->element_type();

    const char *fun = v_util_std_any_set_pointer_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    v_type_t    *ft = nullptr;

    if (!lctx.obtain_function(fun, ft, f))
    {
        throw std::runtime_error(std::string("Intrinsic function not found: ") + fun);
    }

    LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();
}


//---------------------------------------------------------------------
//- Static init/term ...
//---------------------------------------------------------------------
void static_initialize(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

#define DEF(name) \
    gctx.intrinsics[#name] = name;

    DEF(v_initialize)
    DEF(v_terminate)

    DEF(v_copy)
    DEF(v_move)

    DEF(v_empty)

    DEF(v_kind)

    DEF(v_std_any_get_value)
    DEF(v_std_any_get_pointer)
    DEF(v_std_any_set_value)
    DEF(v_std_any_set_pointer)

#undef DEF

    //-----------------------------------------------------------------
#define DEF(ctype, name) \
    static_assert((sizeof(ctype) % sizeof(intptr_t)) == 0); \
    v_type_t *name##_content_type = gctx.make_array_type(gctx.intptr_t_type, sizeof(ctype)/sizeof(intptr_t)); \
    auto opaque_##name##_type = gctx.make_struct_type("struct.v_util_opaque_" #name); \
    opaque_##name##_type->set_body(&name##_content_type, 1, false); \
    gctx.add_symbol("v_util_opaque_" #name, gctx.opaque_type_type, (void *)opaque_##name##_type);

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




