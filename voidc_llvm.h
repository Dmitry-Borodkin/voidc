#ifndef VOIDC_LLVM_H
#define VOIDC_LLVM_H

#include <string>
#include <vector>
#include <utility>

#include <llvm-c/Core.h>
#include <llvm-c/OrcBindings.h>

#include "voidc_ast.h"


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

[[gnu::dllexport]] void v_add_symbol_type(const char *name, LLVMTypeRef type);
[[gnu::dllexport]] void v_add_symbol_value(const char *name, void *value);
[[gnu::dllexport]] void v_add_symbol(const char *name, LLVMTypeRef type, void *value);
[[gnu::dllexport]] void v_add_constant(const char *name, LLVMValueRef val);

[[gnu::dllexport]] void v_add_local_symbol(const char *name, LLVMTypeRef type, void *value);
[[gnu::dllexport]] void v_add_local_constant(const char *name, LLVMValueRef value);

[[gnu::dllexport]] LLVMTypeRef v_find_symbol_type(const char *name);

[[gnu::dllexport]] void v_add_alias(const char *name, const char *str);
[[gnu::dllexport]] void v_add_local_alias(const char *name, const char *str);


//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- Compilation context
//---------------------------------------------------------------------
class compile_ctx_t
{
public:
    explicit compile_ctx_t(const std::string filename);
    ~compile_ctx_t();

public:
    static uint64_t resolver(const char *name, void *void_cctx);

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    static compile_ctx_t * const &current_ctx;

public:
    static LLVMTargetMachineRef target_machine;
    static LLVMOrcJITStackRef   jit;
    static LLVMBuilderRef       builder;
    static LLVMPassManagerRef   pass_manager;

    LLVMModuleRef module;

    const std::string filename;

public:
    LLVMMemoryBufferRef unit_buffer = nullptr;

    void run_unit_action(void);

public:
    static LLVMTypeRef void_type;
    static LLVMTypeRef bool_type;
    static LLVMTypeRef char_type;
    static LLVMTypeRef short_type;
    static LLVMTypeRef int_type;
    static LLVMTypeRef long_type;
    static LLVMTypeRef long_long_type;
    static LLVMTypeRef intptr_t_type;
    static LLVMTypeRef size_t_type;
    static LLVMTypeRef char32_t_type;

    static LLVMTypeRef LLVMOpaqueType_type;
    static LLVMTypeRef LLVMTypeRef_type;
    static LLVMTypeRef LLVMOpaqueValue_type;
    static LLVMTypeRef LLVMValueRef_type;
    static LLVMTypeRef LLVMOpaqueContext_type;
    static LLVMTypeRef LLVMContextRef_type;

public:
    static std::map<std::string, LLVMTypeRef> symbol_types;                 //- Mangled names!

    std::map<std::string, std::pair<LLVMTypeRef, void *>> local_symbols;    //- Mangled names!

    static std::map<std::string, LLVMValueRef> constants;

    std::map<std::string, LLVMValueRef> local_constants;

public:
    static std::map<std::string, std::string> aliases;

    std::map<std::string, std::string> local_aliases;

public:
    LLVMTypeRef find_type(const char *type_name);

    LLVMTypeRef find_symbol_type(const char *name);

    bool find_function(const std::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value);

    LLVMValueRef find_identifier(const std::string &name);

public:
    typedef void (*intrinsic_t)(compile_ctx_t *cctx, const ast_arg_list_ptr_t *args);

    static std::map<std::string, intrinsic_t> intrinsics;

public:
    std::map<std::string, LLVMValueRef> vars;

    std::vector<LLVMTypeRef>  arg_types;
    std::vector<LLVMValueRef> args;

    const char  *ret_name;
    LLVMValueRef ret_value;

private:
    static compile_ctx_t *private_current_ctx;

    compile_ctx_t * const parent_ctx;
};



#endif  //- VOIDC_LLVM_H
