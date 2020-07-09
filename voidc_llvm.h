//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_LLVM_H
#define VOIDC_LLVM_H

#include <string>
#include <vector>
#include <utility>

#include <llvm-c/Core.h>
#include <llvm-c/OrcBindings.h>

#include "voidc_ast.h"
#include "voidc_dllexport.h"



//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

void v_add_symbol_type(const char *name, LLVMTypeRef type);
void v_add_symbol_value(const char *name, void *value);
void v_add_symbol(const char *name, LLVMTypeRef type, void *value);
void v_add_constant(const char *name, LLVMValueRef val);

void v_add_local_symbol(const char *name, LLVMTypeRef type, void *value);
void v_add_local_constant(const char *name, LLVMValueRef value);

LLVMTypeRef v_find_symbol_type(const char *name);
void *v_find_symbol_value(const char *_name);

void v_add_alias(const char *name, const char *str);
void v_add_local_alias(const char *name, const char *str);

VOIDC_DLLEXPORT_END

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
    typedef void (*intrinsic_t)(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args);

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


//---------------------------------------------------------------------
//- Compiler (level 0) ...
//---------------------------------------------------------------------
visitor_ptr_t make_compile_visitor(compile_ctx_t &cctx);


//---------------------------------------------------------------------
extern v_quark_t voidc_compile_ctx_visitor_tag;


#endif  //- VOIDC_LLVM_H
