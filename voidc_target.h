//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TARGET_H
#define VOIDC_TARGET_H

#include "voidc_ast.h"
#include "voidc_types.h"

#include <string>
#include <vector>
#include <set>
#include <map>
#include <forward_list>
#include <utility>

#include <immer/map.hpp>

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>

#include <llvm/ExecutionEngine/Orc/Core.h>


//---------------------------------------------------------------------
class base_local_ctx_t;


//---------------------------------------------------------------------
//- Base Compilation Context
//---------------------------------------------------------------------
class base_compile_ctx_t
{
public:
    base_compile_ctx_t() = default;
    virtual ~base_compile_ctx_t() = default;

public:
    typedef void (*intrinsic_t)(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args);

    struct declarations_t
    {
        std::map<std::string, std::string> aliases;
        std::map<std::string, v_type_t *>  constants;
        std::map<std::string, v_type_t *>  symbols;
        std::map<std::string, intrinsic_t> intrinsics;

        void insert(const declarations_t &other)
        {
            aliases.insert(other.aliases.begin(), other.aliases.end());
            constants.insert(other.constants.begin(), other.constants.end());
            symbols.insert(other.symbols.begin(), other.symbols.end());
            intrinsics.insert(other.intrinsics.begin(), other.intrinsics.end());
        }
    };

    declarations_t decls;

public:
    std::map<std::string, LLVMValueRef> constant_values;

public:
    virtual void add_symbol_value(const char *raw_name, void *value) = 0;

public:
    v_type_t *get_symbol_type(const char *raw_name) const
    {
        auto it = decls.symbols.find(raw_name);

        if (it != decls.symbols.end())  return it->second;
        else                            return nullptr;
    }
};


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
class base_global_ctx_t : public base_compile_ctx_t, public voidc_types_ctx_t
{
public:
    base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size);
    ~base_global_ctx_t() override;

public:
    std::map<std::string, declarations_t> imported;

public:
    const LLVMBuilderRef builder;

    v_type_t * const char_ptr_type;
    v_type_t * const void_ptr_type;

public:
    static int debug_print_module;

    static void verify_module(LLVMModuleRef module);

public:
    base_local_ctx_t *local_ctx = nullptr;

protected:
    void initialize(void);
};


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
class base_local_ctx_t : public base_compile_ctx_t
{
public:
    explicit base_local_ctx_t(base_global_ctx_t &global);
    ~base_local_ctx_t() override;

public:
    std::string filename;

    base_global_ctx_t &global_ctx;

public:
    std::set<std::string> imports;

public:
    declarations_t *export_decls = nullptr;

    void add_alias(const char *name, const char *raw_name);
    void add_local_alias(const char *name, const char *raw_name);

    void add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value);
    void add_local_constant(const char *raw_name, v_type_t *type, LLVMValueRef value);

    void add_symbol(const char *raw_name, v_type_t *type, void *value);
    void add_local_symbol(const char *raw_name, v_type_t *type, void *value);

    void add_intrinsic(const char *fun_name, intrinsic_t fun);
    void add_local_intrinsic(const char *fun_name, intrinsic_t fun);

public:
    const std::string check_alias(const std::string &name);

public:
    v_type_t *find_type(const char *type_name);                     //- Alias checked

    v_type_t *obtain_type(const ast_expr_sptr_t &expr);

    virtual void *find_symbol_value(const char *raw_name) = 0;      //- No alias check!

public:
    LLVMModuleRef module = nullptr;

    bool obtain_identifier(const std::string &name, v_type_t * &type, LLVMValueRef &value);

public:
    LLVMValueRef prepare_function(const char *name, v_type_t *type);
    void finish_function(void);

    LLVMBasicBlockRef function_leave_b = nullptr;

public:
    typedef immer::map<std::string, std::pair<v_type_t *, LLVMValueRef>> variables_t;

    variables_t vars;

    std::forward_list<variables_t> vars_stack;

public:
    v_type_t    *result_type  = nullptr;
    LLVMValueRef result_value = nullptr;

    void adopt_result(v_type_t *type, LLVMValueRef value);

public:
    LLVMValueRef make_temporary(v_type_t *type, LLVMValueRef value);

    void push_temporaries(void);
    void pop_temporaries(void);

public:
    bool has_parent(void) const { return bool(parent_ctx); }

private:
    std::forward_list<LLVMValueRef> temporaries_stack;

    friend class voidc_local_ctx_t;

    base_local_ctx_t * const parent_ctx = nullptr;
};

//---------------------------------------------------------------------
extern "C"
{
VOIDC_DLLEXPORT_BEGIN_VARIABLE

    extern LLVMValueRef (*v_convert_to_type)(v_type_t *t0, LLVMValueRef v0, v_type_t *t1);

VOIDC_DLLEXPORT_END
}


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
class voidc_global_ctx_t : public base_global_ctx_t
{
public:
    voidc_global_ctx_t();
    ~voidc_global_ctx_t() = default;

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    static voidc_global_ctx_t * const & voidc;
    static base_global_ctx_t  *         target;

public:
    static LLVMOrcLLJITRef      jit;
    static LLVMOrcJITDylibRef   main_jd;

    static LLVMTargetMachineRef target_machine;
    static LLVMPassManagerRef   pass_manager;

public:
    static void prepare_module_for_jit(LLVMModuleRef module);

    static void add_module_to_jit(LLVMModuleRef module);

public:
    v_type_t * const opaque_type_type;

public:
    void add_symbol_value(const char *raw_name, void *value) override;

public:
    llvm::orc::SymbolMap unit_symbols;

    void flush_unit_symbols(void);

private:
    friend class voidc_local_ctx_t;

    int local_jd_hash = 0;
};

//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
class voidc_local_ctx_t : public base_local_ctx_t
{
public:
    explicit voidc_local_ctx_t(voidc_global_ctx_t &global);
    ~voidc_local_ctx_t() override;

public:
    void add_symbol_value(const char *raw_name, void *value) override;

public:
    void *find_symbol_value(const char *raw_name) override;         //- No check alias!

public:
    LLVMOrcJITDylibRef local_jd = nullptr;

public:
    void add_module_to_jit(LLVMModuleRef module);

public:
    void prepare_unit_action(int line, int column);
    void finish_unit_action(void);
    void run_unit_action(void);

    LLVMMemoryBufferRef unit_buffer = nullptr;

public:
    llvm::orc::SymbolMap unit_symbols;

    void flush_unit_symbols(void);

private:
    void setup_link_order(void);
};


//---------------------------------------------------------------------
//- Target Global Context
//---------------------------------------------------------------------
class target_global_ctx_t : public base_global_ctx_t
{
public:
    target_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size);
    ~target_global_ctx_t();

public:
    void add_symbol_value(const char *raw_name, void *value) override;

private:
    friend class target_local_ctx_t;

    std::map<std::string, void *> symbol_values;
};

//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
class target_local_ctx_t : public base_local_ctx_t
{
public:
    explicit target_local_ctx_t(base_global_ctx_t &global);
    ~target_local_ctx_t() = default;

public:
    void add_symbol_value(const char *raw_name, void *value) override;

public:
    void *find_symbol_value(const char *raw_name) override;         //- No check alias!

private:
    std::map<std::string, void *> symbol_values;
};


#endif  //- VOIDC_TARGET_H
