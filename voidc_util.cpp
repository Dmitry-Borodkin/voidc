//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_util.h"

#include <cassert>


//---------------------------------------------------------------------
v_util_function_dict_t v_util_initialize_dict;
v_util_function_dict_t v_util_reset_dict;
v_util_function_dict_t v_util_copy_dict;
v_util_function_dict_t v_util_move_dict;
v_util_function_dict_t v_util_kind_dict;
v_util_function_dict_t v_util_std_any_get_value_dict;
v_util_function_dict_t v_util_std_any_get_pointer_dict;
v_util_function_dict_t v_util_std_any_set_value_dict;
v_util_function_dict_t v_util_std_any_set_pointer_dict;


//---------------------------------------------------------------------
namespace utility
{

//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_init_term_helper(compile_ctx_t &cctx,
                        const ast_arg_list_ptr_t &args,
                        const v_util_function_dict_t &dict
                       )
{
    assert(args);
    assert(args->data.size() >= 1);
    assert(args->data.size() <= 2);

    assert(cctx.arg_types.empty());

    args->compile(cctx);

    auto type = LLVMGetElementType(LLVMTypeOf(cctx.args[0]));

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx.find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    if (args->data.size() == 1)
    {
        auto n = LLVMConstInt(cctx.int_type, 1, false);

        cctx.args.push_back(n);
    }

    auto v = LLVMBuildCall(cctx.builder, f, cctx.args.data(), cctx.args.size(), cctx.ret_name);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_initialize(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    v_init_term_helper(*cctx, *args, v_util_initialize_dict);
}

//---------------------------------------------------------------------
static
void v_reset(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    v_init_term_helper(*cctx, *args, v_util_reset_dict);
}


//---------------------------------------------------------------------
static
void v_copy_move_helper(compile_ctx_t &cctx,
                        const ast_arg_list_ptr_t &args,
                        const v_util_function_dict_t &dict
                       )
{
    assert(args);
    assert(args->data.size() >= 2);
    assert(args->data.size() <= 3);

    assert(cctx.arg_types.empty());

    args->compile(cctx);

    auto type = LLVMGetElementType(LLVMTypeOf(cctx.args[0]));

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx.find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    if (args->data.size() == 2)
    {
        auto n = LLVMConstInt(cctx.int_type, 1, false);

        cctx.args.push_back(n);
    }

    auto v = LLVMBuildCall(cctx.builder, f, cctx.args.data(), cctx.args.size(), cctx.ret_name);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_copy(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    v_copy_move_helper(*cctx, *args, v_util_copy_dict);
}

//---------------------------------------------------------------------
static
void v_move(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    v_copy_move_helper(*cctx, *args, v_util_move_dict);
}


//---------------------------------------------------------------------
static
void v_kind(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    assert(*args);
    assert((*args)->data.size() == 1);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto type = LLVMGetElementType(LLVMTypeOf(cctx->args[0]));

    const char *fun = v_util_kind_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx->find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    auto v = LLVMBuildCall(cctx->builder, f, cctx->args.data(), cctx->args.size(), cctx->ret_name);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->ret_value = v;
}


//---------------------------------------------------------------------
static
void v_std_any_get_helper(compile_ctx_t &cctx,
                          const ast_arg_list_ptr_t &args,
                          const v_util_function_dict_t &dict
                         )
{
    assert(args);
    assert(args->data.size() == 2);

    assert(cctx.arg_types.empty());

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*args->data[0]);

    auto type = cctx.find_type(ident.name.c_str());
    assert(type);

    const char *fun = dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx.find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    args->data[1]->compile(cctx);

    auto v = LLVMBuildCall(cctx.builder, f, cctx.args.data(), cctx.args.size(), cctx.ret_name);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_std_any_get_value(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    v_std_any_get_helper(*cctx, *args, v_util_std_any_get_value_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    v_std_any_get_helper(*cctx, *args, v_util_std_any_get_pointer_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    assert(*args);
    assert((*args)->data.size() == 2);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto type = LLVMTypeOf(cctx->args[1]);

    const char *fun = v_util_std_any_set_value_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx->find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    auto v = LLVMBuildCall(cctx->builder, f, cctx->args.data(), cctx->args.size(), cctx->ret_name);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->ret_value = v;
}


//---------------------------------------------------------------------
static
void v_std_any_set_pointer(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args)
{
    assert(*args);
    assert((*args)->data.size() == 2);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto type = LLVMGetElementType(LLVMTypeOf(cctx->args[1]));

    const char *fun = v_util_std_any_set_pointer_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx->find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    auto v = LLVMBuildCall(cctx->builder, f, cctx->args.data(), cctx->args.size(), cctx->ret_name);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->ret_value = v;
}


//---------------------------------------------------------------------
//- Static init/term ...
//---------------------------------------------------------------------
void static_initialize(void)
{
    compile_ctx_t::intrinsics["v_initialize"] = v_initialize;
    compile_ctx_t::intrinsics["v_reset"]      = v_reset;

    compile_ctx_t::intrinsics["v_copy"] = v_copy;
    compile_ctx_t::intrinsics["v_move"] = v_move;

    compile_ctx_t::intrinsics["v_kind"] = v_kind;

    compile_ctx_t::intrinsics["v_std_any_get_value"]   = v_std_any_get_value;
    compile_ctx_t::intrinsics["v_std_any_get_pointer"] = v_std_any_get_pointer;
    compile_ctx_t::intrinsics["v_std_any_set_value"]   = v_std_any_set_value;
    compile_ctx_t::intrinsics["v_std_any_set_pointer"] = v_std_any_set_pointer;

    //-----------------------------------------------------------------
    auto gctx = LLVMGetGlobalContext();

#define DEF(ctype, name) \
    static_assert((sizeof(ctype) % sizeof(intptr_t)) == 0); \
    auto name##_content_type = LLVMArrayType(compile_ctx_t::intptr_t_type, sizeof(ctype)/sizeof(intptr_t)); \
    auto opaque_##name##_type = LLVMStructCreateNamed(gctx, "struct.v_util_opaque_" #name); \
    LLVMStructSetBody(opaque_##name##_type, &name##_content_type, 1, false); \
    v_add_symbol("v_util_opaque_" #name, compile_ctx_t::LLVMOpaqueType_type, (void *)opaque_##name##_type);

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

void v_util_reset_function_dict_impl(v_util_function_dict_t *ptr, int count)
{
    for (int i=0; i<count; ++i) ptr[i].clear();
}

VOIDC_DEFINE_COPY_IMPL(v_util_function_dict_t, v_util_copy_function_dict_impl)
VOIDC_DEFINE_MOVE_IMPL(v_util_function_dict_t, v_util_move_function_dict_impl)

const char *v_util_function_dict_get(const v_util_function_dict_t *ptr, LLVMTypeRef type)
{
    try
    {
        return ptr->at(type).c_str();
    }
    catch(std::out_of_range) {}

    return nullptr;
}

void v_util_function_dict_set(v_util_function_dict_t *ptr, LLVMTypeRef type, const char *fun_name)
{
    (*ptr)[type] = fun_name;
}

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(std::any, v_util_initialize_std_any_impl)
VOIDC_DEFINE_RESET_IMPL(std::any, v_util_reset_std_any_impl)
VOIDC_DEFINE_COPY_IMPL(std::any, v_util_copy_std_any_impl)
VOIDC_DEFINE_MOVE_IMPL(std::any, v_util_move_std_any_impl)

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(std::string, v_util_initialize_std_string_impl)

void v_util_reset_std_string_impl(std::string *ptr, int count)
{
    for (int i=0; i<count; ++i)
    {
        ptr[i].clear();
        ptr[i].shrink_to_fit();
    }
}

VOIDC_DEFINE_COPY_IMPL(std::string, v_util_copy_std_string_impl)
VOIDC_DEFINE_MOVE_IMPL(std::string, v_util_move_std_string_impl)

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

DEF(bool,    bool)
DEF(int8_t,  int8_t)
DEF(int16_t, int16_t)
DEF(int32_t, int32_t)
DEF(int64_t, int64_t)

DEF_PTR(std::string, std_string)

#undef DEF
#undef DEF_PTR
#undef DEF_VAR




//---------------------------------------------------------------------
void voidc_add_intrinsic(const char *name, compile_ctx_t::intrinsic_t fun)
{
    compile_ctx_t::intrinsics[name] = fun;
}



VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"




