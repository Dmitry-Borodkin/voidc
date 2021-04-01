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
#include <llvm-c/OrcBindings.h>


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
    std::set<std::string> imports;

public:
    std::map<std::string, std::pair<v_type_t *, LLVMValueRef>> constants;

    std::map<std::string, std::string> aliases;

public:
    virtual void add_symbol(const char *raw_name, v_type_t *type, void *value) = 0;
};


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
class base_global_ctx_t : public base_compile_ctx_t, public voidc_types_ctx_t
{
public:
    base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size);
    ~base_global_ctx_t();

public:
    const LLVMBuilderRef builder;

    v_type_t * const char_ptr_type;
    v_type_t * const void_ptr_type;

public:
    static int debug_print_module;

    static void verify_module(LLVMModuleRef module);

public:
    virtual void add_symbol_type(const char *raw_name, v_type_t *type) = 0;
    virtual void add_symbol_value(const char *raw_name, void *value) = 0;

    virtual v_type_t *get_symbol_type(const char *raw_name) = 0;
    virtual void *    get_symbol_value(const char *raw_name) = 0;
    virtual void      get_symbol(const char *raw_name, v_type_t * &type, void * &value) = 0;

public:
    typedef void (*intrinsic_t)(const visitor_sptr_t *vis, void *aux, const ast_arg_list_sptr_t *args);

    std::map<std::string, intrinsic_t> intrinsics;

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
    base_local_ctx_t(const std::string filename, base_global_ctx_t &global);
    ~base_local_ctx_t();

public:
    const std::string filename;

    base_global_ctx_t &global_ctx;

public:
    const std::string check_alias(const std::string &name);

public:
    virtual v_type_t *find_type(const char *type_name) = 0;             //- Alias checked

    virtual v_type_t *find_symbol_type(const char *raw_name) = 0;       //- No alias check!

    v_type_t *lookup_type(const ast_argument_sptr_t &arg);

public:
    LLVMModuleRef module = nullptr;

    bool obtain_function(const std::string &fun_name, v_type_t * &fun_type, LLVMValueRef &fun_value);

    bool obtain_identifier(const std::string &name, v_type_t * &type, LLVMValueRef &value);

public:
    typedef immer::map<std::string, std::pair<v_type_t *, LLVMValueRef>> variables_t;

    variables_t vars;

    std::forward_list<variables_t> vars_stack;

public:
    struct arguments_t
    {
        std::vector<v_type_t *>   types;
        std::vector<LLVMValueRef> values;

        v_type_t    *ret_type  = nullptr;
        LLVMValueRef ret_value = nullptr;
    };

    typedef std::unique_ptr<arguments_t> args_uptr_t;

    args_uptr_t args = std::make_unique<arguments_t>();

    std::forward_list<args_uptr_t> args_stack;

public:
    LLVMValueRef make_temporary(v_type_t *type, LLVMValueRef value);

    void push_temporaries(void);
    void pop_temporaries(void);

private:
    std::forward_list<LLVMValueRef> temporaries_stack;

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
    static void static_initialize(void);
    static void static_terminate(void);

public:
    static voidc_global_ctx_t * const & voidc;
    static base_global_ctx_t  *         target;

public:
    static LLVMTargetMachineRef target_machine;
    static LLVMOrcJITStackRef   jit;
    static LLVMPassManagerRef   pass_manager;

public:
    static void prepare_module_for_jit(LLVMModuleRef module);

public:
    v_type_t * const opaque_type_type;

public:
    void add_symbol_type(const char *raw_name, v_type_t *type) override;
    void add_symbol_value(const char *raw_name, void *value) override;
    void add_symbol(const char *raw_name, v_type_t *type, void *value) override;

    v_type_t *get_symbol_type(const char *raw_name) override;
    void *    get_symbol_value(const char *raw_name) override;
    void      get_symbol(const char *raw_name, v_type_t * &type, void * &value) override;

private:
    friend class voidc_local_ctx_t;

    std::map<std::string, v_type_t *> symbol_types;         //- Mangled names!
};

//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
class voidc_local_ctx_t : public base_local_ctx_t
{
public:
    voidc_local_ctx_t(const std::string filename, base_global_ctx_t &global);
    ~voidc_local_ctx_t() = default;

public:
    void add_symbol(const char *name, v_type_t *type, void *value) override;

public:
    v_type_t *find_type(const char *type_name) override;            //- Alias checked

    v_type_t *find_symbol_type(const char *raw_name) override;      //- No check alias!

public:
    static uint64_t resolver(const char *name, void *);

public:
    void prepare_unit_action(int line, int column);
    void finish_unit_action(void);
    void run_unit_action(void);

    LLVMMemoryBufferRef unit_buffer = nullptr;

private:
    std::map<std::string, std::pair<v_type_t *, void *>> symbols;       //- Mangled names!
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
    void add_symbol_type(const char *raw_name, v_type_t *type) override;
    void add_symbol_value(const char *raw_name, void *value) override;
    void add_symbol(const char *raw_name, v_type_t *type, void *value) override;

    v_type_t *get_symbol_type(const char *raw_name) override;
    void *    get_symbol_value(const char *raw_name) override;
    void      get_symbol(const char *raw_name, v_type_t * &type, void * &value) override;

private:
    std::map<std::string, std::pair<v_type_t *, void *>> symbols;
};

//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
class target_local_ctx_t : public base_local_ctx_t
{
public:
    target_local_ctx_t(const std::string filename, base_global_ctx_t &global);
    ~target_local_ctx_t() = default;

public:
    void add_symbol(const char *raw_name, v_type_t *type, void *value) override;

public:
    v_type_t *find_type(const char *type_name) override;            //- Alias checked

    v_type_t *find_symbol_type(const char *raw_name) override;      //- No check alias!

private:
    std::map<std::string, std::pair<v_type_t *, void *>> symbols;
};


#endif  //- VOIDC_TARGET_H
