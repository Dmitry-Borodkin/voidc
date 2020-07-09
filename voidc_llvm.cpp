//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_llvm.h"

#include <regex>
#include <iostream>
#include <cassert>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/OrcBindings.h>
#include <llvm-c/Support.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>


//---------------------------------------------------------------------
//- Compilation context
//---------------------------------------------------------------------
compile_ctx_t * const &compile_ctx_t::current_ctx = compile_ctx_t::private_current_ctx;
compile_ctx_t *compile_ctx_t::private_current_ctx = nullptr;

LLVMTargetMachineRef compile_ctx_t::target_machine;
LLVMOrcJITStackRef   compile_ctx_t::jit;
LLVMBuilderRef       compile_ctx_t::builder;
LLVMPassManagerRef   compile_ctx_t::pass_manager;

LLVMTypeRef compile_ctx_t::void_type;
LLVMTypeRef compile_ctx_t::bool_type;
LLVMTypeRef compile_ctx_t::char_type;
LLVMTypeRef compile_ctx_t::short_type;
LLVMTypeRef compile_ctx_t::int_type;
LLVMTypeRef compile_ctx_t::long_type;
LLVMTypeRef compile_ctx_t::long_long_type;
LLVMTypeRef compile_ctx_t::intptr_t_type;
LLVMTypeRef compile_ctx_t::size_t_type;
LLVMTypeRef compile_ctx_t::char32_t_type;
LLVMTypeRef compile_ctx_t::LLVMTypeRef_type;
LLVMTypeRef compile_ctx_t::LLVMOpaqueType_type;
LLVMTypeRef compile_ctx_t::LLVMValueRef_type;
LLVMTypeRef compile_ctx_t::LLVMOpaqueValue_type;
LLVMTypeRef compile_ctx_t::LLVMContextRef_type;
LLVMTypeRef compile_ctx_t::LLVMOpaqueContext_type;

std::map<std::string, LLVMTypeRef>  compile_ctx_t::symbol_types;
std::map<std::string, LLVMValueRef> compile_ctx_t::constants;
std::map<std::string, std::string>  compile_ctx_t::aliases;
std::map<std::string, compile_ctx_t::intrinsic_t> compile_ctx_t::intrinsics;


//---------------------------------------------------------------------
static
const std::string check_alias(const std::string &name)
{
    try
    {
        try
        {
            return  compile_ctx_t::current_ctx->local_aliases.at(name);
        }
        catch(std::out_of_range)
        {
            return  compile_ctx_t::aliases.at(name);
        }
    }
    catch(std::out_of_range) {}

    return  name;
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
compile_ctx_t::compile_ctx_t(const std::string _filename)
  : filename(_filename),
    parent_ctx(current_ctx)
{
    private_current_ctx = this;
}

compile_ctx_t::~compile_ctx_t()
{
    private_current_ctx = parent_ctx;
}


//---------------------------------------------------------------------
uint64_t compile_ctx_t::resolver(const char *m_name, void *)
{
    auto &cctx = *compile_ctx_t::current_ctx;

    try
    {
        return  (uint64_t)cctx.local_symbols.at(m_name).second;
    }
    catch(std::out_of_range)
    {
        return  (uint64_t)LLVMSearchForAddressOfSymbol(m_name);
    }
}


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_alloca(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(*args);
    assert((*args)->data.size() >= 1);
    assert((*args)->data.size() <= 2);

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*(*args)->data[0]);

    auto type = cctx.find_type(ident.name.c_str());
    assert(type);

    LLVMValueRef v;

    if ((*args)->data.size() == 1)
    {
        v = LLVMBuildAlloca(cctx.builder, type, cctx.ret_name);
    }
    else
    {
        (*args)->data[1]->accept(*vis);         //- Количество...

        v = LLVMBuildArrayAlloca(cctx.builder, type, cctx.args[0], cctx.ret_name);

        cctx.args.clear();
    }

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_getelementptr(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(*args);
    assert((*args)->data.size() >= 2);

    assert(cctx.arg_types.empty());

    (*args)->accept(*vis);

    auto v = LLVMBuildGEP(cctx.builder, cctx.args[0], &cctx.args[1], cctx.args.size()-1, cctx.ret_name);

    cctx.args.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_store(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(*args);
    assert((*args)->data.size() == 2);

    assert(cctx.arg_types.empty());

    (*args)->data[1]->accept(*vis);         //- Сначала "куда"

    cctx.arg_types.resize(2);

    cctx.arg_types[1] = LLVMGetElementType(LLVMTypeOf(cctx.args[0]));

    (*args)->data[0]->accept(*vis);         //- Теперь "что"

    auto v = LLVMBuildStore(cctx.builder, cctx.args[1], cctx.args[0]);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_load(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(*args);
    assert((*args)->data.size() == 1);

    (*args)->data[0]->accept(*vis);

    auto v = LLVMBuildLoad(cctx.builder, cctx.args[0], cctx.ret_name);

    cctx.args.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_cast(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(*args);
    assert((*args)->data.size() == 3);

    (*args)->data[0]->accept(*vis);         //- Opcode

    auto opcode = LLVMOpcode(LLVMConstIntGetZExtValue(cctx.args[0]));

    (*args)->data[1]->accept(*vis);         //- Value

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*(*args)->data[2]);

    auto dest_type = cctx.find_type(ident.name.c_str());
    assert(dest_type);

    LLVMValueRef v;

    v = LLVMBuildCast(cctx.builder, opcode, cctx.args[1], dest_type, cctx.ret_name);

    cctx.args.clear();

    cctx.ret_value = v;
}


//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
void v_add_symbol_type(const char *name, LLVMTypeRef type)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name);

    compile_ctx_t::symbol_types[m_name] = type;

    LLVMOrcDisposeMangledSymbol(m_name);
}

void v_add_symbol_value(const char *name, void *value)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name);

    LLVMAddSymbol(m_name, value);

    LLVMOrcDisposeMangledSymbol(m_name);
}

void v_add_symbol(const char *name, LLVMTypeRef type, void *value)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name);

    compile_ctx_t::symbol_types[m_name] = type;

    LLVMAddSymbol(m_name, value);

    LLVMOrcDisposeMangledSymbol(m_name);
}

void v_add_constant(const char *name, LLVMValueRef val)
{
    compile_ctx_t::constants[name] = val;
}

//---------------------------------------------------------------------
void v_add_local_symbol(const char *name, LLVMTypeRef type, void *value)
{
    auto &cctx = *compile_ctx_t::current_ctx;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name);

    cctx.local_symbols[m_name] = {type, value};

    LLVMOrcDisposeMangledSymbol(m_name);
}

void v_add_local_constant(const char *name, LLVMValueRef value)
{
    auto &cctx = *compile_ctx_t::current_ctx;

    cctx.local_constants[name] = value;
}

LLVMTypeRef v_find_symbol_type(const char *_name)
{
    auto &cctx = *compile_ctx_t::current_ctx;

    LLVMTypeRef type = nullptr;

    auto name = check_alias(_name);

    try
    {
        LLVMValueRef value;

        try
        {
            value = cctx.local_constants.at(name);
        }
        catch(std::out_of_range)
        {
            value = cctx.constants.at(name);
        }

        type = LLVMTypeOf(value);
    }
    catch(std::out_of_range)
    {
        char *m_name = nullptr;

        LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name.c_str());

        try
        {
            type = cctx.local_symbols.at(m_name).first;
        }
        catch(std::out_of_range)
        {
            try
            {
                type = cctx.symbol_types.at(m_name);
            }
            catch(std::out_of_range) {}
        }

        LLVMOrcDisposeMangledSymbol(m_name);
    }

    return type;
}

void *v_find_symbol_value(const char *_name)
{
    auto &cctx = *compile_ctx_t::current_ctx;

    void *value = nullptr;

    auto name = check_alias(_name);

    try
    {
        try
        {
            value = (void *)cctx.local_constants.at(name);     //- Sic!
        }
        catch(std::out_of_range)
        {
            value = (void *)cctx.constants.at(name);           //- Sic!
        }
    }
    catch(std::out_of_range)
    {
        char *m_name = nullptr;

        LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name.c_str());

        try
        {
            value = cctx.local_symbols.at(m_name).second;
        }
        catch(std::out_of_range)
        {
            value = LLVMSearchForAddressOfSymbol(m_name);
        }

        LLVMOrcDisposeMangledSymbol(m_name);
    }

    return value;
}

void v_add_alias(const char *name, const char *str)
{
    compile_ctx_t::aliases[name] = str;
}

void v_add_local_alias(const char *name, const char *str)
{
    auto &cctx = *compile_ctx_t::current_ctx;

    cctx.local_aliases[name] = str;
}


//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void compile_ctx_t::static_initialize(void)
{
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();

    //-------------------------------------------------------------
    {   char *triple = LLVMGetDefaultTargetTriple();

        LLVMTargetRef tr;

        char *errmsg = nullptr;

        int err = LLVMGetTargetFromTriple(triple, &tr, &errmsg);

        if (errmsg)
        {
            fprintf(stderr, "LLVMGetTargetFromTriple: %s\n", errmsg);

            LLVMDisposeMessage(errmsg);

            errmsg = nullptr;
        }

        assert(err == 0);

        char *cpu_name     = LLVMGetHostCPUName();
        char *cpu_features = LLVMGetHostCPUFeatures();

        target_machine =
            LLVMCreateTargetMachine
            (
                tr,
                triple,
                cpu_name,
                cpu_features,
                LLVMCodeGenLevelAggressive,
                LLVMRelocDefault,
                LLVMCodeModelJITDefault
            );

        jit = LLVMOrcCreateInstance(target_machine);

        LLVMDisposeMessage(cpu_features);
        LLVMDisposeMessage(cpu_name);
        LLVMDisposeMessage(triple);
    }

    //-------------------------------------------------------------
    builder = LLVMCreateBuilder();

    //-------------------------------------------------------------
    pass_manager = LLVMCreatePassManager();

    {   auto pm_builder = LLVMPassManagerBuilderCreate();

        LLVMPassManagerBuilderSetOptLevel(pm_builder, 3);       //- -O3
        LLVMPassManagerBuilderSetSizeLevel(pm_builder, 2);      //- -Oz

        LLVMPassManagerBuilderPopulateModulePassManager(pm_builder, pass_manager);

        LLVMPassManagerBuilderDispose(pm_builder);
    }

    //-------------------------------------------------------------
#define DEF(name) \
    v_add_symbol_value("voidc_" #name, (void *)name);

    DEF(resolver)
    DEF(target_machine)
    DEF(jit)
    DEF(builder)
    DEF(pass_manager)

#undef DEF

    //-------------------------------------------------------------
    void_type = LLVMVoidType();
    bool_type = LLVMInt1Type();

    auto mk_type = [](size_t sz)
    {
        switch(sz)
        {
        case  1:    return LLVMInt8Type();
        case  2:    return LLVMInt16Type();
        case  4:    return LLVMInt32Type();
        case  8:    return LLVMInt64Type();
        case 16:    return LLVMInt128Type();

        default:
            assert(false && "mk_type");
        }

        return (LLVMTypeRef)nullptr;        //- WTF ?!?!?
    };

    char_type      = mk_type(sizeof(char));
    short_type     = mk_type(sizeof(short));
    int_type       = mk_type(sizeof(int));
    long_type      = mk_type(sizeof(long));
    long_long_type = mk_type(sizeof(long long));
    intptr_t_type  = mk_type(sizeof(intptr_t));
    size_t_type    = mk_type(sizeof(size_t));
    char32_t_type  = mk_type(sizeof(char32_t));

    {   auto gctx = LLVMGetGlobalContext();

        LLVMOpaqueType_type = LLVMStructCreateNamed(gctx, "struct.LLVMOpaqueType");

        LLVMTypeRef_type = LLVMPointerType(LLVMOpaqueType_type, 0);

        LLVMOpaqueValue_type = LLVMStructCreateNamed(gctx, "struct.LLVMOpaqueValue");

        LLVMValueRef_type = LLVMPointerType(LLVMOpaqueValue_type, 0);

        LLVMOpaqueContext_type = LLVMStructCreateNamed(gctx, "struct.LLVMOpaqueContext");

        LLVMContextRef_type = LLVMPointerType(LLVMOpaqueContext_type, 0);

#define DEF(name) \
        v_add_symbol(#name, LLVMOpaqueType_type, (void *)name##_type);

        DEF(void)
        DEF(bool)
        DEF(char)
        DEF(short)
        DEF(int)
        DEF(long)
        DEF(long_long)
        DEF(intptr_t)
        DEF(size_t)
        DEF(char32_t)

        DEF(LLVMOpaqueType)
        DEF(LLVMTypeRef)
        DEF(LLVMOpaqueValue)
        DEF(LLVMValueRef)
        DEF(LLVMOpaqueContext)
        DEF(LLVMContextRef)

#undef DEF
    }

    intrinsics["v_alloca"]        = v_alloca;
    intrinsics["v_getelementptr"] = v_getelementptr;
    intrinsics["v_store"]         = v_store;
    intrinsics["v_load"]          = v_load;
    intrinsics["v_cast"]          = v_cast;

    {   LLVMTypeRef args[] =
        {
            LLVMTypeRef_type,
            LLVMPointerType(LLVMTypeRef_type, 0),
            int_type,
            int_type
        };

        v_add_symbol_type("LLVMFunctionType", LLVMFunctionType(LLVMTypeRef_type, args, 4, false));

        auto char_ptr_type = LLVMPointerType(char_type, 0);
        auto void_ptr_type = LLVMPointerType(void_type, 0);

        args[0] = char_ptr_type;
        args[1] = LLVMTypeRef_type;

#define DEF(name, ret, num) \
        v_add_symbol_type(#name, LLVMFunctionType(ret, args, num, false));

        DEF(v_add_symbol_type, void_type, 2)

        args[1] = void_ptr_type;

        DEF(v_add_symbol_value, void_type, 2)

        args[1] = LLVMTypeRef_type;
        args[2] = void_ptr_type;

        DEF(v_add_symbol, void_type, 3)

        args[1] = LLVMValueRef_type;

        DEF(v_add_constant, void_type, 2)

        args[0] = char_ptr_type;
        args[1] = LLVMTypeRef_type;
        args[2] = void_ptr_type;

        DEF(v_add_local_symbol, void_type, 3)

        args[1] = LLVMValueRef_type;

        DEF(v_add_local_constant, void_type, 2)

        DEF(v_find_symbol_type, LLVMTypeRef_type, 1)
        DEF(v_find_symbol_value, void_ptr_type, 1)

        args[1] = char_ptr_type;

        DEF(v_add_alias, void_type, 2)
        DEF(v_add_local_alias, void_type, 2)

#undef DEF
    }

    LLVMLoadLibraryPermanently(nullptr);        //- Sic!!!
}

//---------------------------------------------------------------------
void compile_ctx_t::static_terminate(void)
{
    LLVMDisposePassManager(pass_manager);

    LLVMDisposeBuilder(builder);

    LLVMOrcDisposeInstance(jit);

    LLVMShutdown();
}


//---------------------------------------------------------------------
void compile_ctx_t::run_unit_action(void)
{
    if (!unit_buffer) return;

    char *msg;

    std::string fun_name;

    {   auto bref = LLVMCreateBinary(unit_buffer, nullptr, &msg);

        assert(bref);

        auto it = LLVMObjectFileCopySymbolIterator(bref);

        static std::regex sym_regex("unit_[0-9]+_[0-9]+_action");

        while(!LLVMObjectFileIsSymbolIteratorAtEnd(bref, it))
        {
            auto sym = LLVMGetSymbolName(it);

            if (std::regex_match(sym, sym_regex))
            {
                fun_name = sym;
            }

            LLVMMoveToNextSymbol(it);
        }

        LLVMDisposeSymbolIterator(it);

        LLVMDisposeBinary(bref);
    }

    LLVMOrcModuleHandle H;

    auto lerr = LLVMOrcAddObjectFile(jit, &H, unit_buffer, resolver, nullptr);

    if (lerr)
    {
        msg = LLVMGetErrorMessage(lerr);

        printf("\n%s\n", msg);

        LLVMDisposeErrorMessage(msg);

        puts(LLVMOrcGetErrorMsg(jit));
    }

    unit_buffer = nullptr;

    LLVMOrcTargetAddress addr = 0;

    lerr = LLVMOrcGetSymbolAddressIn(jit, &addr, H, fun_name.c_str());

    if (lerr)
    {
        msg = LLVMGetErrorMessage(lerr);

        printf("\n%s\n", msg);

        LLVMDisposeErrorMessage(msg);

        puts(LLVMOrcGetErrorMsg(jit));
    }

    fflush(stdout);     //- WTF ?

    assert(addr);

    void (*unit_action)() = (void (*)())addr;

    unit_action();

    LLVMOrcRemoveModule(jit, H);
}


//---------------------------------------------------------------------
LLVMTypeRef
compile_ctx_t::find_type(const char *type_name)
{
    LLVMTypeRef tt = nullptr;                           //- ...

    void *tv = nullptr;

    char *m_name = nullptr;

    auto name = check_alias(type_name);

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name.c_str());

    try
    {
        auto &[t,v] = local_symbols.at(m_name);

        tt = t;                                         //- ...
        tv = v;
    }
    catch(std::out_of_range)
    {
        try                                             //- ...
        {                                               //- ...
            tt = symbol_types.at(m_name);               //- ...
            tv = LLVMSearchForAddressOfSymbol(m_name);
        }                                               //- ...
        catch(std::out_of_range) {}                     //- ...
    }

    LLVMOrcDisposeMangledSymbol(m_name);

    assert(tt == LLVMOpaqueType_type);                  //- ...

    return (LLVMTypeRef)tv;
}

//---------------------------------------------------------------------
LLVMTypeRef
compile_ctx_t::find_symbol_type(const char *_name)
{
    auto name = check_alias(_name);

    LLVMTypeRef type = nullptr;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name.c_str());

    try
    {
        type = local_symbols.at(m_name).first;
    }
    catch(std::out_of_range)
    {
        try
        {
            type = symbol_types.at(m_name);
        }
        catch(std::out_of_range) {}
    }

    LLVMOrcDisposeMangledSymbol(m_name);

    return type;
}

//---------------------------------------------------------------------
bool
compile_ctx_t::find_function(const std::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value)
{
    fun_type  = nullptr;
    fun_value = nullptr;

    try
    {
        fun_value = vars.at(fun_name);
    }
    catch(std::out_of_range)
    {
        auto f_name = check_alias(fun_name);

        auto fname = f_name.c_str();

        fun_value = LLVMGetNamedFunction(module, fname);

        if (!fun_value)
        {
            fun_type = find_symbol_type(fname);

            if (fun_type)
            {
                fun_value = LLVMAddFunction(module, fname, fun_type);
            }
        }
    }

    if (!fun_value) return  false;

    if (!fun_type)
    {
        fun_type = LLVMTypeOf(fun_value);
    }

    if (LLVMGetTypeKind(fun_type) == LLVMPointerTypeKind)
    {
        fun_type = LLVMGetElementType(fun_type);    //- ?!?!?!?
    }

    if (LLVMGetTypeKind(fun_type) != LLVMFunctionTypeKind)  return false;

    return true;
}

//---------------------------------------------------------------------
LLVMValueRef
compile_ctx_t::find_identifier(const std::string &_name)
{
    LLVMValueRef value = nullptr;

    try
    {
        value = vars.at(_name);
    }
    catch(std::out_of_range)
    {
        auto name = check_alias(_name);

        try
        {
            try
            {
                value = local_constants.at(name);
            }
            catch(std::out_of_range)
            {
                value = constants.at(name);
            }
        }
        catch(std::out_of_range)
        {
            auto cname = name.c_str();

            value = LLVMGetNamedGlobal(module, cname);

            if (!value)
            {
                LLVMTypeRef type = find_symbol_type(cname);

                if (type)
                {
                    value = LLVMAddGlobal(module, type, cname);
                }
            }
        }
    }

    return value;
}


//=====================================================================
//- AST Visitor - Compiler (level 0) ...
//=====================================================================
v_quark_t voidc_compile_ctx_visitor_tag;

//---------------------------------------------------------------------
static void compile_ast_base_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}
static void compile_ast_stmt_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}
static void compile_ast_arg_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_ptr_t *vis, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(cctx.args.empty());

    if (!*stmt_list)  return;

    std::string hdr = "unit_" + std::to_string(line) + "_" + std::to_string(column);

    std::string mod_name = hdr + "_module";
    std::string fun_name = hdr + "_action";

    cctx.module = LLVMModuleCreateWithName(mod_name.c_str());

    LLVMSetSourceFileName(cctx.module, cctx.filename.c_str(), cctx.filename.size());

    {   auto dl = LLVMCreateTargetDataLayout(cctx.target_machine);
        auto tr = LLVMGetTargetMachineTriple(cctx.target_machine);

        LLVMSetModuleDataLayout(cctx.module, dl);
        LLVMSetTarget(cctx.module, tr);

        LLVMDisposeMessage(tr);
        LLVMDisposeTargetData(dl);
    }

    LLVMTypeRef  unit_ft = LLVMFunctionType(LLVMVoidType(), nullptr, 0, false);
    LLVMValueRef unit_f  = LLVMAddFunction(cctx.module, fun_name.c_str(), unit_ft);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(unit_f, "entry");

    LLVMPositionBuilderAtEnd(cctx.builder, entry);

    (*stmt_list)->accept(*vis);

    LLVMBuildRetVoid(cctx.builder);

    LLVMClearInsertionPosition(cctx.builder);

    //-------------------------------------------------------------
    LLVMRunPassManager(cctx.pass_manager, cctx.module);

    //-------------------------------------------------------------
    char *msg = nullptr;

    auto err = LLVMVerifyModule(cctx.module, LLVMReturnStatusAction, &msg);
    if (err)
    {
        char *txt = LLVMPrintModuleToString(cctx.module);

        printf("\n%s\n", txt);

        LLVMDisposeMessage(txt);

        printf("\n%s\n", msg);
    }

    LLVMDisposeMessage(msg);

    if (err)  exit(1);          //- Sic !!!

    //-------------------------------------------------------------
    err = LLVMTargetMachineEmitToMemoryBuffer(cctx.target_machine,
                                              cctx.module,
                                              LLVMObjectFile,
                                              &msg,
                                              &cctx.unit_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(cctx.unit_buffer);


//    {   LLVMMemoryBufferRef asm_buffer = nullptr;
//
//        LLVMTargetMachineEmitToMemoryBuffer(cctx.target_machine,
//                                            cctx.module,
//                                            LLVMAssemblyFile,
//                                            &msg,
//                                            &asm_buffer);
//
//        assert(asm_buffer);
//
//        printf("\n%s\n", LLVMGetBufferStart(asm_buffer));
//
//        LLVMDisposeMemoryBuffer(asm_buffer);
//    }


    LLVMDisposeModule(cctx.module);

    cctx.vars.clear();
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_ast_stmt_t(const visitor_ptr_t *vis, const std::string *vname, const ast_call_ptr_t *call)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    auto &var_name = *vname;

    cctx.ret_name  = var_name.c_str();
    cctx.ret_value = nullptr;

    (*call)->accept(*vis);

    if (cctx.ret_name[0])   cctx.vars[var_name] = cctx.ret_value;
}


//---------------------------------------------------------------------
//- call
//---------------------------------------------------------------------
static
void compile_ast_call_t(const visitor_ptr_t *vis, const std::string *fname, const ast_arg_list_ptr_t *args)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    assert(cctx.args.empty());

    auto &fun_name = *fname;

    if (cctx.intrinsics.count(fun_name))
    {
        cctx.intrinsics[fun_name](vis, args);

        return;
    }

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx.find_function(fun_name, ft, f);

    if (!ok)  puts(fun_name.c_str());

    assert(ok && "function not found");

    cctx.arg_types.resize(LLVMCountParamTypes(ft));

    LLVMGetParamTypes(ft, cctx.arg_types.data());

    if (*args) (*args)->accept(*vis);

    auto v = LLVMBuildCall(cctx.builder, f, cctx.args.data(), cctx.args.size(), cctx.ret_name);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}


//---------------------------------------------------------------------
//- arg_identifier
//---------------------------------------------------------------------
static
void compile_ast_arg_identifier_t(const visitor_ptr_t *vis, const std::string *name)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    LLVMValueRef v = cctx.find_identifier(*name);

    if (!v)  puts(name->c_str());

    assert(v && "identifier not found");

    auto idx = cctx.args.size();

    if (idx < cctx.arg_types.size())
    {
        auto void_ptr_type = LLVMPointerType(cctx.void_type, 0);

        auto at = cctx.arg_types[idx];
        auto vt = LLVMTypeOf(v);

        if (at != vt  &&
            at == void_ptr_type  &&
            LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
        {
            v = LLVMBuildPointerCast(cctx.builder, v, void_ptr_type, name->c_str());
        }
    }

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_integer
//---------------------------------------------------------------------
static
void compile_ast_arg_integer_t(const visitor_ptr_t *vis, intptr_t num)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    auto idx = cctx.args.size();

    LLVMTypeRef t = cctx.int_type;

    if (idx < cctx.arg_types.size())
    {
        t = cctx.arg_types[idx];
    }

    LLVMValueRef v;

    if (LLVMGetTypeKind(t) == LLVMPointerTypeKind  &&  num == 0)
    {
        v = LLVMConstPointerNull(t);
    }
    else
    {
        v = LLVMConstInt(t, num, false);     //- ?
    }

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_string
//---------------------------------------------------------------------
static
void compile_ast_arg_string_t(const visitor_ptr_t *vis, const std::string *str)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    auto v = LLVMBuildGlobalStringPtr(cctx.builder, str->c_str(), "str");

    auto idx = cctx.args.size();

    if (idx < cctx.arg_types.size())
    {
        auto void_ptr_type = LLVMPointerType(cctx.void_type, 0);

        auto at = cctx.arg_types[idx];

        if (at == void_ptr_type)
        {
            v = LLVMBuildPointerCast(cctx.builder, v, void_ptr_type, "void_str");
        }
    }

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_char
//---------------------------------------------------------------------
static
void compile_ast_arg_char_t(const visitor_ptr_t *vis, char32_t c)
{
    auto &cctx = *(compile_ctx_t *)(*vis)->void_methods[voidc_compile_ctx_visitor_tag];

    auto v = LLVMConstInt(cctx.int_type, c, false);      //- ?

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- Compiler visitor
//---------------------------------------------------------------------
static
visitor_ptr_t compile_visitor_level_zero;

visitor_ptr_t
make_compile_visitor(compile_ctx_t &cctx)
{
    voidc_visitor_t vis;

    if (!compile_visitor_level_zero)
    {
        voidc_compile_ctx_visitor_tag = v_quark_from_string("compile_ctx_t");

#define DEF(type) \
        auto set_##type = [&vis](type::visitor_method_t method) \
        { \
            vis = vis.set_void_method(v_##type##_visitor_method_tag, (void *)method); \
        }; \
        set_##type(compile_##type);

        DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF

        compile_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }
    else
    {
        vis = *compile_visitor_level_zero;      //- Copy!
    }

    vis = vis.set_void_method(voidc_compile_ctx_visitor_tag, (void *)&cctx);

    return  std::make_shared<const voidc_visitor_t>(vis);
}


