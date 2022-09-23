//---------------------------------------------------------------------
//- Copyright (C) 2020-2022 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TARGET_H
#define VOIDC_TARGET_H

#include "voidc_ast.h"
#include "voidc_types.h"
#include "voidc_util.h"

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
#include <llvm/IR/IRBuilder.h>


//---------------------------------------------------------------------
class base_local_ctx_t;

extern "C" typedef LLVMValueRef (*convert_to_type_t)(void *ctx, v_type_t *t0, LLVMValueRef v0, v_type_t *t1);

extern "C" typedef LLVMModuleRef (*obtain_module_t)(void *ctx);

extern "C" typedef void (*finish_module_t)(void *ctx, LLVMModuleRef module);


//---------------------------------------------------------------------
//- Base Compilation Context
//---------------------------------------------------------------------
class base_compile_ctx_t
{
public:
    base_compile_ctx_t() = default;
    virtual ~base_compile_ctx_t() = default;

public:
    using intrinsic_t = std::pair<void *, void *>;          //- I.e. (function, context)

    struct declarations_t
    {
        immer::map<std::string, std::string> aliases;
        immer::map<std::string, v_type_t *>  constants;
        immer::map<std::string, v_type_t *>  symbols;
        immer::map<std::string, intrinsic_t> intrinsics;

        bool empty(void) const
        {
            return (aliases.empty() && constants.empty() && symbols.empty() && intrinsics.empty());
        }

        void aliases_insert   (std::pair<std::string, std::string> v) { aliases    = aliases.insert(v); }
        void constants_insert (std::pair<std::string, v_type_t *>  v) { constants  = constants.insert(v); }
        void symbols_insert   (std::pair<std::string, v_type_t *>  v) { symbols    = symbols.insert(v); }
        void intrinsics_insert(std::pair<std::string, intrinsic_t> v) { intrinsics = intrinsics.insert(v); }

        void insert(const declarations_t &other);
    };

    declarations_t decls;

    using typenames_t = immer::map<v_type_t *, std::string>;

public:
    std::map<std::string, LLVMValueRef> constant_values;

public:
    virtual void add_symbol_value(const char *raw_name, void *value) = 0;

public:
    v_type_t *get_symbol_type(const char *raw_name) const
    {
        auto *pt = decls.symbols.find(raw_name);

        if (pt) return *pt;
        else    return nullptr;
    }

public:
    using cleaners_t = std::forward_list<std::pair<void (*)(void *), void *>>;

    void add_cleaner(void (*fun)(void *data), void *data)
    {
        cleaners.push_front({fun, data});
    }

protected:
    void run_cleaners(void)
    {
        for (auto &it: cleaners) it.first(it.second);

        cleaners.clear();       //- Sic!
    }

private:
    cleaners_t cleaners;
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
    LLVMBuilderRef builder;

    LLVMTargetDataRef data_layout = nullptr;

public:
    virtual void initialize_type(const char *raw_name, v_type_t *type);

    v_type_t * const char_ptr_type;
    v_type_t * const void_ptr_type;

public:
    static void verify_module(LLVMModuleRef module);

public:
    utility::function_dict_t function_dict;

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
    declarations_t *export_decls = nullptr;

    void export_alias(const char *name, const char *raw_name);
    void add_alias(const char *name, const char *raw_name);

    void export_constant(const char *raw_name, v_type_t *type, LLVMValueRef value);
    void add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value);

    void export_symbol(const char *raw_name, v_type_t *type, void *value);
    void add_symbol(const char *raw_name, v_type_t *type, void *value);

    void export_intrinsic(const char *fun_name, void *fun, void *aux=nullptr);
    void add_intrinsic(const char *fun_name, void *fun, void *aux=nullptr);

    virtual void export_type(const char *raw_name, v_type_t *type);
    virtual void add_type(const char *raw_name, v_type_t *type);

public:
    const std::string check_alias(const std::string &name);

public:
    v_type_t *find_type(const char *type_name);                     //- Alias checked


    bool find_constant(const char *raw_name, v_type_t * &type, LLVMValueRef &value);

    bool find_symbol(const char *raw_name, v_type_t * &type, void * &value);


    virtual void *find_symbol_value(const char *raw_name) = 0;      //- No alias check!

public:
    LLVMModuleRef module = nullptr;

    obtain_module_t obtain_module_fun = nullptr;
    void           *obtain_module_ctx = nullptr;

    bool obtain_identifier(const std::string &name, v_type_t * &type, LLVMValueRef &value);

    finish_module_t finish_module_fun = nullptr;
    void           *finish_module_ctx = nullptr;

    void finish_module(LLVMModuleRef mod)
    {
        if (finish_module_fun)  finish_module_fun(finish_module_ctx, mod);
    }

public:
    void push_builder_ip(void);
    void pop_builder_ip(void);

private:
    std::forward_list<llvm::IRBuilderBase::InsertPoint> builder_ip_stack;

public:
    LLVMValueRef prepare_function(const char *name, v_type_t *type);
    void finish_function(void);

    LLVMBasicBlockRef function_leave_b = nullptr;

public:
    typedef immer::map<std::string, std::pair<v_type_t *, LLVMValueRef>> variables_t;

    variables_t vars;

public:
    void push_variables(void);
    void pop_variables(void);

private:
    std::forward_list<std::pair<declarations_t, variables_t>> vars_stack;       //- Sic!

public:
    v_type_t    *result_type  = nullptr;
    LLVMValueRef result_value = nullptr;

    virtual void adopt_result(v_type_t *type, LLVMValueRef value);

public:
    LLVMValueRef convert_to_type(v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
    {
        return convert_to_type_fun(convert_to_type_ctx, t0, v0, t1);
    }

    convert_to_type_t convert_to_type_fun;
    void             *convert_to_type_ctx;

public:
    LLVMValueRef make_temporary(v_type_t *type, LLVMValueRef value);

    void add_temporary_cleaner(void (*fun)(void *data), void *data);

    void push_temporaries(void);
    void pop_temporaries(void);

    LLVMValueRef get_temporaries_front(void)  { return temporaries_stack.front().first; }

public:
    bool has_parent(void) const { return bool(parent_ctx); }

private:
    std::forward_list<std::pair<LLVMValueRef, cleaners_t>> temporaries_stack;

    friend class voidc_local_ctx_t;

    base_local_ctx_t * const parent_ctx = nullptr;
};


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
class voidc_global_ctx_t : public base_global_ctx_t
{
public:
    voidc_global_ctx_t();
    ~voidc_global_ctx_t() = default;

public:
    void initialize_type(const char *raw_name, v_type_t *type) override;

    typenames_t typenames;

    std::map<std::string, typenames_t> imported_typenames;

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    VOIDC_DLLEXPORT static voidc_global_ctx_t * const & voidc;
    VOIDC_DLLEXPORT static base_global_ctx_t  *         target;

public:
    VOIDC_DLLEXPORT static LLVMOrcLLJITRef      jit;
    VOIDC_DLLEXPORT static LLVMOrcJITDylibRef   main_jd;

    VOIDC_DLLEXPORT static LLVMTargetMachineRef target_machine;
    VOIDC_DLLEXPORT static LLVMPassManagerRef   pass_manager;

public:
    static void prepare_module_for_jit(LLVMModuleRef module);

    static void add_module_to_jit(LLVMModuleRef module);

public:
    v_type_t * const static_type_type;
    v_type_t * const type_type;

    v_type_t * const type_ptr_type;

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
    std::set<std::string> imports;

public:
    typenames_t *export_typenames = nullptr;
    typenames_t  typenames;

    void export_type(const char *raw_name, v_type_t *type) override;
    void add_type(const char *raw_name, v_type_t *type) override;

public:
    void add_symbol_value(const char *raw_name, void *value) override;

public:
    void *find_symbol_value(const char *raw_name) override;         //- No check alias!

public:
    void adopt_result(v_type_t *type, LLVMValueRef value) override;

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
    ~target_global_ctx_t() override;

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
    ~target_local_ctx_t() override;

public:
    void add_symbol_value(const char *raw_name, void *value) override;

public:
    void *find_symbol_value(const char *raw_name) override;         //- No check alias!

private:
    std::map<std::string, void *> symbol_values;
};


#endif  //- VOIDC_TARGET_H
