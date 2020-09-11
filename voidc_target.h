//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TARGET_H
#define VOIDC_TARGET_H

#include "voidc_ast.h"

#include <string>
#include <vector>
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
    std::map<std::string, LLVMValueRef> constants;

    std::map<std::string, std::string> aliases;

public:
    virtual void add_symbol(const char *raw_name, LLVMTypeRef type, void *value) = 0;
};


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
class base_global_ctx_t : public base_compile_ctx_t
{
public:
    base_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size);
    virtual ~base_global_ctx_t() = default;

public:
    const LLVMTypeRef void_type;
    const LLVMTypeRef bool_type;
    const LLVMTypeRef char_type;
    const LLVMTypeRef short_type;
    const LLVMTypeRef int_type;
    const LLVMTypeRef long_type;
    const LLVMTypeRef long_long_type;
    const LLVMTypeRef intptr_t_type;
    const LLVMTypeRef size_t_type;
    const LLVMTypeRef char32_t_type;

    const LLVMTypeRef voidc_opaque_void_type;
    const LLVMTypeRef void_ptr_type;

public:
    virtual void add_symbol_type(const char *raw_name, LLVMTypeRef type) = 0;
    virtual void add_symbol_value(const char *raw_name, void *value) = 0;

    virtual LLVMTypeRef get_symbol_type(const char *raw_name) = 0;
    virtual void *      get_symbol_value(const char *raw_name) = 0;
    virtual void        get_symbol(const char *raw_name, LLVMTypeRef &type, void * &value) = 0;

public:
    typedef void (*intrinsic_t)(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args);

    std::map<std::string, intrinsic_t> intrinsics;

public:
    base_local_ctx_t *current_ctx = nullptr;

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

    base_global_ctx_t &global;

public:
    const std::string check_alias(const std::string &name);

public:
    virtual LLVMTypeRef find_type(const char *type_name) = 0;           //- Alias checked

    virtual LLVMTypeRef find_symbol_type(const char *raw_name) = 0;     //- No alias check!

public:
    LLVMModuleRef module = nullptr;

    bool find_function(const std::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value);

    LLVMValueRef find_identifier(const std::string &name);

public:
    typedef immer::map<std::string, LLVMValueRef> variables_t;

    variables_t vars;

    std::forward_list<variables_t> vars_stack;

public:
    std::vector<LLVMTypeRef>  arg_types;
    std::vector<LLVMValueRef> args;

    const char  *ret_name;
    LLVMValueRef ret_value;

private:
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
    static LLVMBuilderRef       builder;
    static LLVMPassManagerRef   pass_manager;

public:
    static bool debug_print_module;

    static void prepare_module_for_jit(LLVMModuleRef module);

public:
    const LLVMTypeRef LLVMOpaqueType_type;
    const LLVMTypeRef LLVMTypeRef_type;
    const LLVMTypeRef LLVMOpaqueContext_type;
    const LLVMTypeRef LLVMContextRef_type;

public:
    void add_symbol_type(const char *raw_name, LLVMTypeRef type) override;
    void add_symbol_value(const char *raw_name, void *value) override;
    void add_symbol(const char *raw_name, LLVMTypeRef type, void *value) override;

    LLVMTypeRef get_symbol_type(const char *raw_name) override;
    void *      get_symbol_value(const char *raw_name) override;
    void        get_symbol(const char *raw_name, LLVMTypeRef &type, void * &value) override;

private:
    friend class voidc_local_ctx_t;

    std::map<std::string, LLVMTypeRef> symbol_types;        //- Mangled names!
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
    void add_symbol(const char *name, LLVMTypeRef type, void *value) override;

public:
    LLVMTypeRef find_type(const char *type_name) override;          //- Alias checked

    LLVMTypeRef find_symbol_type(const char *raw_name) override;    //- No check alias!

public:
    static uint64_t resolver(const char *name, void *);

    void prepare_unit_action(int line, int column);
    void finish_unit_action(void);
    void run_unit_action(void);

    LLVMMemoryBufferRef unit_buffer = nullptr;

private:
    std::map<std::string, std::pair<LLVMTypeRef, void *>> symbols;      //- Mangled names!
};


//---------------------------------------------------------------------
//- Target Global Context
//---------------------------------------------------------------------
class target_global_ctx_t : public base_global_ctx_t
{
public:
    target_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size);
    ~target_global_ctx_t() = default;

public:
    void add_symbol_type(const char *raw_name, LLVMTypeRef type) override;
    void add_symbol_value(const char *raw_name, void *value) override;
    void add_symbol(const char *raw_name, LLVMTypeRef type, void *value) override;

    LLVMTypeRef get_symbol_type(const char *raw_name) override;
    void *      get_symbol_value(const char *raw_name) override;
    void        get_symbol(const char *raw_name, LLVMTypeRef &type, void * &value) override;

private:
    std::map<std::string, std::pair<LLVMTypeRef, void *>> symbols;
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
    void add_symbol(const char *raw_name, LLVMTypeRef type, void *value) override;

public:
    LLVMTypeRef find_type(const char *type_name) override;          //- Alias checked

    LLVMTypeRef find_symbol_type(const char *raw_name) override;    //- No check alias!

private:
    std::map<std::string, std::pair<LLVMTypeRef, void *>> symbols;
};


//---------------------------------------------------------------------
//- Compiler (level 0) ...
//---------------------------------------------------------------------
visitor_ptr_t make_compile_visitor(void);


#endif  //- VOIDC_TARGET_H
