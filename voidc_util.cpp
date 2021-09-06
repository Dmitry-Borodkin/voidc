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
void v_init_term_helper(const visitor_sptr_t *vis,
                        const ast_expr_list_sptr_t &args,
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

    lctx.result_type = UNREFERENCE_TAG;

    args->data[0]->accept(*vis);

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

    if (args->data.size() == 1)
    {
        values[1] = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);
    }
    else
    {
        lctx.result_type = UNREFERENCE_TAG;

        args->data[1]->accept(*vis);

        values[1] = lctx.result_value;
    }

    LLVMBuildCall(gctx.builder, f, values, 2, "");
}

//---------------------------------------------------------------------
static
void v_initialize(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_init_term_helper(vis, *args, v_util_initialize_dict);
}

//---------------------------------------------------------------------
static
void v_terminate(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_init_term_helper(vis, *args, v_util_terminate_dict);
}


//---------------------------------------------------------------------
static
void v_copy_move_helper(const visitor_sptr_t *vis,
                        const ast_expr_list_sptr_t &args,
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

    lctx.result_type = UNREFERENCE_TAG;

    args->data[0]->accept(*vis);

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
        if (args->data.size() <= i)
        {
            values[i] = LLVMConstInt(gctx.int_type->llvm_type(), 1, false);
        }
        else
        {
            lctx.result_type = UNREFERENCE_TAG;

            args->data[i]->accept(*vis);

            values[i] = lctx.result_value;
        }
    }

    LLVMBuildCall(gctx.builder, f, values, 3, "");
}

//---------------------------------------------------------------------
static
void v_copy(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_copy_move_helper(vis, *args, v_util_copy_dict);
}

//---------------------------------------------------------------------
static
void v_move(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_copy_move_helper(vis, *args, v_util_move_dict);
}


//---------------------------------------------------------------------
static
void v_empty(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = v_util_empty_dict.at(type).c_str();

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
void v_kind(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = v_util_kind_dict.at(type).c_str();

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
void v_std_any_get_helper(const visitor_sptr_t *vis,
                          const ast_expr_list_sptr_t &args,
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

    auto tt = lctx.result_type;


    lctx.result_type = INVIOLABLE_TAG;

    args->data[0]->accept(*vis);            //- Type

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

    args->data[1]->accept(*vis);

    auto v = LLVMBuildCall(gctx.builder, f, &lctx.result_value, 1, "");

    if (&dict == &v_util_std_any_get_pointer_dict)
    {
        auto adsp = LLVMGetPointerAddressSpace(LLVMTypeOf(v));

        type = gctx.make_pointer_type(type, adsp);
    }

    lctx.result_type = tt;

    lctx.adopt_result(type, v);
}

//---------------------------------------------------------------------
static
void v_std_any_get_value(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_std_any_get_helper(vis, *args, v_util_std_any_get_value_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_std_any_get_helper(vis, *args, v_util_std_any_get_pointer_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    LLVMValueRef values[2];

    for (int i=0; i<2; ++i)
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    const char *fun = v_util_std_any_set_value_dict.at(lctx.result_type).c_str();

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
void v_std_any_set_pointer(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    LLVMValueRef values[2];

    for (int i=0; i<2; ++i)
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = v_util_std_any_set_pointer_dict.at(type).c_str();

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
void v_make_list_helper(const visitor_sptr_t *vis,
                        const ast_expr_list_sptr_t &args,
                        const v_util_function_dict_t &dict
                       )
{

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    if (args->data.size() < 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string(args->data.size()));
    }

    lctx.result_type = UNREFERENCE_TAG;

    args->data[0]->accept(*vis);

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

    auto arg_count = args->data.size();


    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    values[0] = lctx.result_value;

    for (int i=1; i<arg_count; ++i)
    {
        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        args->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    LLVMBuildCall(gctx.builder, f, values.get(), arg_count, "");
}

//---------------------------------------------------------------------
static
void v_make_list_nil(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_make_list_helper(vis, *args, v_util_make_list_nil_dict);
}

//---------------------------------------------------------------------
static
void v_make_list(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    v_make_list_helper(vis, *args, v_util_make_list_dict);
}


//---------------------------------------------------------------------
static
void v_list_append(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 3  ||  (*args)->data.size() > 4)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = v_util_list_append_dict.at(type).c_str();

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
void v_list_get_size(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = v_util_list_get_size_dict.at(type).c_str();

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
void v_list_get_items(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 3  ||  (*args)->data.size() > 4)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis);

    auto type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    const char *fun = v_util_list_get_items_dict.at(type).c_str();

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

#define DEF(name) \
    gctx.decls.intrinsics.insert({#name, name});

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

    DEF(v_make_list_nil)
    DEF(v_make_list)
    DEF(v_list_append)
    DEF(v_list_get_size)
    DEF(v_list_get_items)

#undef DEF

    //-----------------------------------------------------------------
    auto add_type = [&gctx](const char *raw_name, v_type_t *type)
    {
        gctx.decls.constants.insert({raw_name, type});

        gctx.decls.symbols.insert({raw_name, gctx.opaque_type_type});

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




