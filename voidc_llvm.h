#ifndef VOIDC_LLVM_H
#define VOIDC_LLVM_H

#include "voidc_ast.h"

#include <string>
#include <vector>
#include <utility>

#include <llvm-c/OrcBindings.h>


//---------------------------------------------------------------------
//- Compilation context
//---------------------------------------------------------------------
class compile_ctx_t
{
public:
    explicit compile_ctx_t(const vpeg::string filename);
    ~compile_ctx_t();

public:
    static uint64_t resolver(const char *name, void *void_cctx);

public:
    static void initialize(void);
    static void terminate(void);

public:
    static LLVMTargetMachineRef target_machine;
    static LLVMOrcJITStackRef   jit;

    LLVMBuilderRef builder;

    LLVMModuleRef module;

    const vpeg::string filename;

public:
    LLVMMemoryBufferRef unit_buffer = nullptr;

    void run_unit_action(void);

public:
    static LLVMTypeRef void_type;

    static LLVMTypeRef char_type;
    static LLVMTypeRef short_type;
    static LLVMTypeRef int_type;
    static LLVMTypeRef long_type;
    static LLVMTypeRef long_long_type;
    static LLVMTypeRef intptr_t_type;
    static LLVMTypeRef size_t_type;

    static LLVMTypeRef LLVMOpaqueType_type;
    static LLVMTypeRef LLVMTypeRef_type;
    static LLVMTypeRef LLVMOpaqueValue_type;
    static LLVMTypeRef LLVMValueRef_type;
    static LLVMTypeRef LLVMOpaqueContext_type;
    static LLVMTypeRef LLVMContextRef_type;

public:
    static std::map<vpeg::string, LLVMTypeRef> symbol_types;

    std::map<vpeg::string, std::pair<LLVMTypeRef, void *>> local_symbols;

    static std::map<vpeg::string, LLVMValueRef> constants;

    std::map<vpeg::string, LLVMValueRef> local_constants;

public:
    bool find_function(const vpeg::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value);

    LLVMValueRef find_identifier(const vpeg::string &name);

public:
    typedef void (*intrinsic_t)(compile_ctx_t &cctx, const std::shared_ptr<const ast_arg_list_t> &args);

    static std::map<vpeg::string, intrinsic_t> intrinsics;

    void call_intrinsic_helper(const char *helper, const std::shared_ptr<const ast_arg_list_t> &args);

public:
    std::forward_list<LLVMValueRef> stmts;

    std::map<vpeg::string, LLVMValueRef> vars;

    std::vector<LLVMTypeRef>  arg_types;
    std::vector<LLVMValueRef> args;

    const char *ret_name;
};


#endif  //- VOIDC_LLVM_H
