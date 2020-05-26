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
std::map<std::string, compile_ctx_t::intrinsic_t> compile_ctx_t::intrinsics;

//---------------------------------------------------------------------
compile_ctx_t::compile_ctx_t(const std::string _filename)
  : filename(_filename)
{
    add_local_symbol("voidc_intrinsic_compilation_context", void_type, this);
}


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
uint64_t compile_ctx_t::resolver(const char *name, void *void_cctx)
{
    auto cctx = (compile_ctx_t *)void_cctx;

    if (cctx->local_symbols.count(name))
    {
        return  (uint64_t)cctx->local_symbols[name].second;
    }

    return (uint64_t)LLVMSearchForAddressOfSymbol(name);
}


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_alloca(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 1);

    auto ident = std::dynamic_pointer_cast<const ast_arg_identifier_t>((*args)->data[0]);
    assert(ident);

    auto type = (LLVMTypeRef)cctx->resolver(ident->name.c_str(), cctx);     //- Sic !!!
    assert(type);

    auto v = LLVMBuildAlloca(cctx->builder, type, cctx->ret_name);

    cctx->stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_array_alloca(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 2);

    auto ident = std::dynamic_pointer_cast<const ast_arg_identifier_t>((*args)->data[0]);
    assert(ident);

    auto type = (LLVMTypeRef)cctx->resolver(ident->name.c_str(), cctx);     //- Sic !!!
    assert(type);

    (*args)->data[1]->compile(*cctx);       //- Количество...

    auto v = LLVMBuildArrayAlloca(cctx->builder, type, cctx->args[0], cctx->ret_name);

    cctx->args.clear();

    cctx->stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_getelementptr(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() >= 2);

    assert(cctx->arg_types.empty());

    (*args)->compile(*cctx);

    auto v = LLVMBuildGEP(cctx->builder, cctx->args[0], &cctx->args[1], cctx->args.size()-1, cctx->ret_name);

    cctx->args.clear();

    cctx->stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_store(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 2);

    assert(cctx->arg_types.empty());

    (*args)->data[1]->compile(*cctx);       //- Сначала "куда"

    cctx->arg_types.resize(2);

    cctx->arg_types[1] = LLVMGetElementType(LLVMTypeOf(cctx->args[0]));

    (*args)->data[0]->compile(*cctx);       //- Теперь "что"

    auto v = LLVMBuildStore(cctx->builder, cctx->args[1], cctx->args[0]);

    cctx->args.clear();
    cctx->arg_types.clear();

    cctx->stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_load(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    assert(*args);
    assert((*args)->data.size() == 1);

    (*args)->data[0]->compile(*cctx);

    auto v = LLVMBuildLoad(cctx->builder, cctx->args[0], cctx->ret_name);

    cctx->args.clear();

    cctx->stmts.push_front(v);
}


//---------------------------------------------------------------------
void
compile_ctx_t::build_intrinsic_call(const char *fun, const std::shared_ptr<const ast_arg_list_t> &_args)
{
    auto id_cctx = std::make_shared<const ast_arg_identifier_t>("voidc_intrinsic_compilation_context");

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = find_function(fun, ft, f);

    assert(ok && "intrinsic function not found");

    arg_types.resize(LLVMCountParamTypes(ft));

    LLVMGetParamTypes(ft, arg_types.data());

    assert(args.empty());

    id_cctx->compile(*this);

    _args->compile(*this);

    auto v = LLVMBuildCall(builder, f, args.data(), args.size(), ret_name);

    args.clear();
    arg_types.clear();

    stmts.push_front(v);
}

//---------------------------------------------------------------------
static
void v_add_local_symbol(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    cctx->build_intrinsic_call("voidc_intrinsic_add_local_symbol", *args);
}

//---------------------------------------------------------------------
static
void v_add_local_constant(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    cctx->build_intrinsic_call("voidc_intrinsic_add_local_constant", *args);
}

static
void v_find_symbol_type(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    cctx->build_intrinsic_call("voidc_intrinsic_find_symbol_type", *args);
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
void voidc_intrinsic_add_local_symbol(void *void_cctx, const char *name, LLVMTypeRef type, void *value)
{
    auto *cctx = (compile_ctx_t *)void_cctx;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name);

    cctx->local_symbols[m_name] = {type, value};

    LLVMOrcDisposeMangledSymbol(m_name);
}

void voidc_intrinsic_add_local_constant(void *void_cctx, const char *name, LLVMValueRef value)
{
    auto *cctx = (compile_ctx_t *)void_cctx;

    cctx->local_constants[name] = value;
}

LLVMTypeRef voidc_intrinsic_find_symbol_type(void *void_cctx, const char *name)
{
    auto *cctx = (compile_ctx_t *)void_cctx;

    LLVMTypeRef type = nullptr;

    if (cctx->local_constants.count(name)  ||  cctx->constants.count(name))
    {
        LLVMValueRef value;

        if (cctx->local_constants.count(name))  value = cctx->local_constants[name];
        else                                    value = cctx->constants[name];

        type = LLVMTypeOf(value);
    }
    else
    {
        char *m_name = nullptr;

        LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name);

        if (cctx->local_symbols.count(m_name))      type = cctx->local_symbols[m_name].first;
        else if (cctx->symbol_types.count(m_name))  type = cctx->symbol_types[m_name];

        LLVMOrcDisposeMangledSymbol(m_name);
    }

    return type;
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

    intrinsics["v_alloca"]             = v_alloca;
    intrinsics["v_array_alloca"]       = v_array_alloca;
    intrinsics["v_getelementptr"]      = v_getelementptr;
    intrinsics["v_store"]              = v_store;
    intrinsics["v_load"]               = v_load;
    intrinsics["v_add_local_symbol"]   = v_add_local_symbol;
    intrinsics["v_add_local_constant"] = v_add_local_constant;
    intrinsics["v_find_symbol_type"]   = v_find_symbol_type;

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
        v_add_symbol(#name, LLVMFunctionType(ret, args, num, false), (void *)name);

        DEF(v_add_symbol_type, void_type, 2)

        args[1] = void_ptr_type;

        DEF(v_add_symbol_value, void_type, 2)

        args[1] = LLVMTypeRef_type;
        args[2] = void_ptr_type;

        DEF(v_add_symbol, void_type, 3)

        args[1] = LLVMValueRef_type;

        DEF(v_add_constant, void_type, 2)

        args[0] = void_ptr_type;
        args[1] = char_ptr_type;
        args[2] = LLVMTypeRef_type;
        args[3] = void_ptr_type;

        DEF(voidc_intrinsic_add_local_symbol, void_type, 4)

        args[2] = LLVMValueRef_type;

        DEF(voidc_intrinsic_add_local_constant, void_type, 3)

        DEF(voidc_intrinsic_find_symbol_type, LLVMTypeRef_type, 2)

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

    auto lerr = LLVMOrcAddObjectFile(jit, &H, unit_buffer, resolver, this);

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
bool
compile_ctx_t::find_function(const std::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value)
{
    fun_type  = nullptr;
    fun_value = nullptr;

    if (vars.count(fun_name))
    {
        fun_value = vars[fun_name];
    }
    else
    {
        char *m_name = nullptr;

        LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, fun_name.c_str());

        if (local_symbols.count(m_name)  ||  symbol_types.count(m_name))
        {
            fun_value = LLVMGetNamedFunction(module, fun_name.c_str());

            if (!fun_value)
            {
                if (local_symbols.count(m_name))  fun_type = local_symbols[m_name].first;
                else                              fun_type = symbol_types[m_name];

                fun_value = LLVMAddFunction(module, fun_name.c_str(), fun_type);
            }
        }

        LLVMOrcDisposeMangledSymbol(m_name);
    }

    if (!fun_value) return false;

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
compile_ctx_t::find_identifier(const std::string &name)
{
    LLVMValueRef value = nullptr;

    if (vars.count(name))
    {
        value = vars[name];
    }
    else if (local_constants.count(name))
    {
        value = local_constants[name];
    }
    else if (constants.count(name))
    {
        value = constants[name];
    }
    else
    {
        char *m_name = nullptr;

        LLVMOrcGetMangledSymbol(compile_ctx_t::jit, &m_name, name.c_str());

        if (local_symbols.count(m_name)  ||  symbol_types.count(m_name))
        {
            value = LLVMGetNamedGlobal(module, name.c_str());

            if (!value)
            {
                LLVMTypeRef type;

                if (local_symbols.count(m_name))  type = local_symbols[m_name].first;
                else                              type = symbol_types[m_name];

                value = LLVMAddGlobal(module, type, name.c_str());
            }
        }

        LLVMOrcDisposeMangledSymbol(m_name);
    }

    return  value;
}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
void ast_unit_t::compile(compile_ctx_t &cctx) const
{
    if (!stmt_list) return;

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

    assert(cctx.stmts.empty());
    assert(cctx.vars.empty());

    stmt_list->compile(cctx);

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

    cctx.stmts.clear();
    cctx.vars.clear();
}


//----------------------------------------------------------------------
//- stmt
//----------------------------------------------------------------------
void ast_stmt_t::compile(compile_ctx_t &cctx) const
{
    cctx.ret_name = var_name.c_str();

    call->compile(cctx);

    if (cctx.ret_name[0])   cctx.vars[var_name] = cctx.stmts.front();
}


//----------------------------------------------------------------------
//- call
//----------------------------------------------------------------------
void ast_call_t::compile(compile_ctx_t &cctx) const
{
    if (cctx.intrinsics.count(fun_name))
    {
        cctx.intrinsics[fun_name](&cctx, &arg_list);

        return;
    }

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx.find_function(fun_name, ft, f);

    if (!ok)  puts(fun_name.c_str());

    assert(ok && "function not found");

    cctx.arg_types.resize(LLVMCountParamTypes(ft));

    LLVMGetParamTypes(ft, cctx.arg_types.data());

    assert(cctx.args.empty());

    if (arg_list) arg_list->compile(cctx);

    auto v = LLVMBuildCall(cctx.builder, f, cctx.args.data(), cctx.args.size(), cctx.ret_name);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.stmts.push_front(v);
}


//----------------------------------------------------------------------
//- arg_identifier
//----------------------------------------------------------------------
void ast_arg_identifier_t::compile(compile_ctx_t &cctx) const
{
    LLVMValueRef v = cctx.find_identifier(name);

    if (!v)  puts(name.c_str());

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
            v = LLVMBuildPointerCast(cctx.builder, v, void_ptr_type, name.c_str());
        }
    }

    cctx.args.push_back(v);
}


//----------------------------------------------------------------------
//- arg_integer
//----------------------------------------------------------------------
void ast_arg_integer_t::compile(compile_ctx_t &cctx) const
{
    auto idx = cctx.args.size();

    LLVMTypeRef t = cctx.int_type;

    if (idx < cctx.arg_types.size())
    {
        t = cctx.arg_types[idx];
    }

    LLVMValueRef v;

    if (LLVMGetTypeKind(t) == LLVMPointerTypeKind  &&  number == 0)
    {
        v = LLVMConstPointerNull(t);
    }
    else
    {
        v = LLVMConstInt(t, number, false);     //- ?
    }

    cctx.args.push_back(v);
}


//----------------------------------------------------------------------
//- arg_string
//----------------------------------------------------------------------
void ast_arg_string_t::compile(compile_ctx_t &cctx) const
{
    auto v = LLVMBuildGlobalStringPtr(cctx.builder, string.c_str(), "str");

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


//----------------------------------------------------------------------
//- arg_char
//----------------------------------------------------------------------
void ast_arg_char_t::compile(compile_ctx_t &cctx) const
{
    auto v = LLVMConstInt(cctx.int_type, c, false);      //- ?

    cctx.args.push_back(v);
}


