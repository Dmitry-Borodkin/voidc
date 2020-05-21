#include "voidc_util.h"

#include "voidc_llvm.h"

#include <any>
#include <cassert>

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace utility
{

//---------------------------------------------------------------------
function_dict_t initialize_dict;
function_dict_t reset_dict;
function_dict_t copy_dict;
function_dict_t move_dict;
function_dict_t kind_dict;

LLVMTypeRef opaque_std_any_type;

function_dict_t std_any_get_value_dict;
function_dict_t std_any_get_pointer_dict;
function_dict_t std_any_set_value_dict;
function_dict_t std_any_set_pointer_dict;


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_init_term_helper(compile_ctx_t &cctx,
                        const std::shared_ptr<const ast_arg_list_t> &args,
                        const function_dict_t &dict
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

    cctx.stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_initialize(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    v_init_term_helper(*cctx, *args, initialize_dict);
}

//---------------------------------------------------------------------
static
void v_reset(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    v_init_term_helper(*cctx, *args, reset_dict);
}


//---------------------------------------------------------------------
static
void v_copy_move_helper(compile_ctx_t &cctx,
                        const std::shared_ptr<const ast_arg_list_t> &args,
                        const function_dict_t &dict
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

    cctx.stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_copy(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    v_copy_move_helper(*cctx, *args, copy_dict);
}

//---------------------------------------------------------------------
static
void v_move(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    v_copy_move_helper(*cctx, *args, move_dict);
}


//---------------------------------------------------------------------
static
void v_kind(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 1);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto type = LLVMGetElementType(LLVMTypeOf(cctx->args[0]));

    const char *fun = kind_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx->find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    auto v = LLVMBuildCall(cctx->builder, f, cctx->args.data(), cctx->args.size(), cctx->ret_name);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->stmts.push_front(v);
}


//---------------------------------------------------------------------
static
void v_std_any_get_helper(compile_ctx_t &cctx,
                          const std::shared_ptr<const ast_arg_list_t> &args,
                          const function_dict_t &dict
                         )
{
    assert(args);
    assert(args->data.size() == 2);

    assert(cctx.arg_types.empty());

    auto ident = std::dynamic_pointer_cast<const ast_arg_identifier_t>(args->data[0]);
    assert(ident);

    auto type = (LLVMTypeRef)cctx.resolver(ident->name.c_str(), &cctx);      //- Sic !!!
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

    cctx.stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_std_any_get_value(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    v_std_any_get_helper(*cctx, *args, std_any_get_value_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_get_pointer(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    v_std_any_get_helper(*cctx, *args, std_any_get_pointer_dict);
}

//---------------------------------------------------------------------
static
void v_std_any_set_value(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 2);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto type = LLVMTypeOf(cctx->args[1]);

    const char *fun = std_any_set_value_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx->find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    auto v = LLVMBuildCall(cctx->builder, f, cctx->args.data(), cctx->args.size(), cctx->ret_name);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->stmts.push_front(v);
}


//---------------------------------------------------------------------
static
void v_std_any_set_pointer(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 2);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto type = LLVMGetElementType(LLVMTypeOf(cctx->args[1]));

    const char *fun = std_any_set_pointer_dict.at(type).c_str();

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx->find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    auto v = LLVMBuildCall(cctx->builder, f, cctx->args.data(), cctx->args.size(), cctx->ret_name);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->stmts.push_front(v);
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
    auto char_ptr_type = LLVMPointerType(compile_ctx_t::char_type, 0);

    auto content_type = LLVMArrayType(char_ptr_type, sizeof(std::any)/sizeof(char *));

    auto gctx = LLVMGetGlobalContext();

    opaque_std_any_type = LLVMStructCreateNamed(gctx, "struct.v_util_opaque_std_any");
    LLVMStructSetBody(opaque_std_any_type, &content_type, 1, false);
    auto std_any_ref_type = LLVMPointerType(opaque_std_any_type, 0);

    compile_ctx_t::symbol_types["v_util_opaque_std_any"] = compile_ctx_t::LLVMOpaqueType_type;
    LLVMAddSymbol("v_util_opaque_std_any", (void *)opaque_std_any_type);

    compile_ctx_t::symbol_types["v_util_std_any_ref"] = compile_ctx_t::LLVMOpaqueType_type;
    LLVMAddSymbol("v_util_std_any_ref", (void *)std_any_ref_type);

    register_initialize_impl<std::any>(opaque_std_any_type, "v_util_initialize_std_any_impl");

    register_reset_impl<std::any>(opaque_std_any_type, "v_util_reset_std_any_impl");

    register_copy_impl<std::any>(opaque_std_any_type, "v_util_copy_std_any_impl");
    register_move_impl<std::any>(opaque_std_any_type, "v_util_move_std_any_impl");

    //-----------------------------------------------------------------
#define DEF(c_type, llvm_type) \
    register_std_any_get_value_impl<c_type>(llvm_type, "v_util_std_any_get_value_" #c_type "_impl"); \
    register_std_any_get_pointer_impl<c_type>(llvm_type, "v_util_std_any_get_pointer_" #c_type "_impl"); \
    register_std_any_set_value_impl<c_type>(llvm_type, "v_util_std_any_set_value_" #c_type "_impl"); \
    register_std_any_set_pointer_impl<c_type>(llvm_type, "v_util_std_any_set_pointer_" #c_type "_impl");

    DEF(bool,    LLVMInt1Type())
    DEF(int8_t,  LLVMInt8Type())
    DEF(int16_t, LLVMInt16Type())
    DEF(int32_t, LLVMInt32Type())
    DEF(int64_t, LLVMInt64Type())

#undef DEF





    // ...
}

//---------------------------------------------------------------------
void static_terminate(void)
{
}


//---------------------------------------------------------------------
}   //- namespace utility


