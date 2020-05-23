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
LLVMTypeRef opaque_std_string_type;

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
static
void v_reset_std_string_impl(std::string *ptr, int count)
{
    for (int i=0; i<count; ++i)
    {
        ptr[i].clear();
        ptr[i].shrink_to_fit();
    }
}

static
char *v_std_string_get(std::string *ptr)
{
    return ptr->data();
}

static
void v_std_string_set(std::string *ptr, const char *str)
{
    *ptr = str;
}

static
void v_std_string_append(std::string *ptr, const char *str)
{
    ptr->append(str);
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

    auto gctx = LLVMGetGlobalContext();

    LLVMTypeRef args[2];

#define DEF(name) \
    v_add_symbol("v_util_" #name, compile_ctx_t::LLVMOpaqueType_type, (void *)name##_type);

    //-----------------------------------------------------------------
    auto std_any_content_type = LLVMArrayType(char_ptr_type, sizeof(std::any)/sizeof(char *));

    opaque_std_any_type = LLVMStructCreateNamed(gctx, "struct.v_util_opaque_std_any");
    LLVMStructSetBody(opaque_std_any_type, &std_any_content_type, 1, false);
    auto std_any_ref_type = LLVMPointerType(opaque_std_any_type, 0);

    DEF(opaque_std_any)
    DEF(std_any_ref)

    register_initialize_impl<std::any>(opaque_std_any_type, "v_util_initialize_std_any_impl");

    register_reset_impl<std::any>(opaque_std_any_type, "v_util_reset_std_any_impl");

    register_copy_impl<std::any>(opaque_std_any_type, "v_util_copy_std_any_impl");
    register_move_impl<std::any>(opaque_std_any_type, "v_util_move_std_any_impl");

    //-----------------------------------------------------------------
    auto std_string_content_type = LLVMArrayType(char_ptr_type, sizeof(std::string)/sizeof(char *));

    opaque_std_string_type = LLVMStructCreateNamed(gctx, "struct.v_util_opaque_std_string");
    LLVMStructSetBody(opaque_std_string_type, &std_string_content_type, 1, false);
    auto std_string_ref_type = LLVMPointerType(opaque_std_string_type, 0);

    DEF(opaque_std_string)
    DEF(std_string_ref)

    register_initialize_impl<std::string>(opaque_std_string_type, "v_util_initialize_std_string_impl");

    register_init_reset_impl_helper<std::string>(opaque_std_string_type, "v_util_reset_std_string_impl",
                                                 (void *)v_reset_std_string_impl, reset_dict
                                                );

    register_copy_impl<std::string>(opaque_std_string_type, "v_util_copy_std_string_impl");
    register_move_impl<std::string>(opaque_std_string_type, "v_util_move_std_string_impl");

#undef DEF

#define DEF(name, ret, num) \
    v_add_symbol(#name, LLVMFunctionType(ret, args, num, false), (void *)name);

    args[0] = std_string_ref_type;
    args[1] = char_ptr_type;

    DEF(v_std_string_get, char_ptr_type, 1);
    DEF(v_std_string_set, compile_ctx_t::void_type, 2);
    DEF(v_std_string_append, compile_ctx_t::void_type, 2);

#undef DEF

    //-----------------------------------------------------------------
#define DEF_VAR(c_type, llvm_type) \
    register_std_any_get_value_impl<c_type>(llvm_type, "v_util_std_any_get_value_" #c_type "_impl"); \
    register_std_any_set_value_impl<c_type>(llvm_type, "v_util_std_any_set_value_" #c_type "_impl");

#define DEF_PTR(c_type, llvm_type) \
    register_std_any_get_pointer_impl<c_type>(llvm_type, "v_util_std_any_get_pointer_" #c_type "_impl"); \
    register_std_any_set_pointer_impl<c_type>(llvm_type, "v_util_std_any_set_pointer_" #c_type "_impl");

#define DEF(c_type, llvm_type) \
    DEF_VAR(c_type, llvm_type) \
    DEF_PTR(c_type, llvm_type)

    DEF(bool,    LLVMInt1Type())
    DEF(int8_t,  LLVMInt8Type())
    DEF(int16_t, LLVMInt16Type())
    DEF(int32_t, LLVMInt32Type())
    DEF(int64_t, LLVMInt64Type())

    {   using std_string = std::string;

        DEF_PTR(std_string, opaque_std_string_type)
    }

#undef DEF
#undef DEF_PTR
#undef DEF_VAR




    // ...
}

//---------------------------------------------------------------------
void static_terminate(void)
{
}


//---------------------------------------------------------------------
}   //- namespace utility


