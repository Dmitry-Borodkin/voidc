//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"

#include <stdexcept>
#include <regex>
#include <cassert>

#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Support.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static void
v_alloca(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 1  ||  (*args)->data.size() > 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*(*args)->data[0]);

    auto type = lctx.find_type(ident.name.c_str());
    if (!type)
    {
        throw std::runtime_error("Type not found: " + ident.name);
    }

    LLVMValueRef v;

    if ((*args)->data.size() == 1)
    {
        v = LLVMBuildAlloca(gctx.builder, type, lctx.ret_name);
    }
    else
    {
        (*args)->data[1]->accept(*vis);         //- Количество...

        v = LLVMBuildArrayAlloca(gctx.builder, type, lctx.args[0], lctx.ret_name);

        lctx.args.clear();
    }

    lctx.ret_value = v;
}

//---------------------------------------------------------------------
static void
v_getelementptr(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(lctx.arg_types.empty());

    (*args)->accept(*vis);

    auto v = LLVMBuildGEP(gctx.builder, lctx.args[0], &lctx.args[1], lctx.args.size()-1, lctx.ret_name);

    lctx.args.clear();

    lctx.ret_value = v;
}

//---------------------------------------------------------------------
static void
v_store(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(lctx.arg_types.empty());

    (*args)->data[1]->accept(*vis);         //- Сначала "куда"

    lctx.arg_types.resize(2);

    lctx.arg_types[1] = LLVMGetElementType(LLVMTypeOf(lctx.args[0]));

    (*args)->data[0]->accept(*vis);         //- Теперь "что"

    auto v = LLVMBuildStore(gctx.builder, lctx.args[1], lctx.args[0]);

    lctx.args.clear();
    lctx.arg_types.clear();

    lctx.ret_value = v;
}

//---------------------------------------------------------------------
static void
v_load(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    (*args)->data[0]->accept(*vis);

    auto v = LLVMBuildLoad(gctx.builder, lctx.args[0], lctx.ret_name);

    lctx.args.clear();

    lctx.ret_value = v;
}

//---------------------------------------------------------------------
static void
v_cast(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 3)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    (*args)->data[0]->accept(*vis);         //- Opcode

    auto opcode = LLVMOpcode(LLVMConstIntGetZExtValue(lctx.args[0]));

    (*args)->data[1]->accept(*vis);         //- Value

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*(*args)->data[2]);

    auto type = lctx.find_type(ident.name.c_str());
    if (!type)
    {
        throw std::runtime_error("Type not found: " + ident.name);
    }

    LLVMValueRef v;

    v = LLVMBuildCast(gctx.builder, opcode, lctx.args[1], type, lctx.ret_name);

    lctx.args.clear();

    lctx.ret_value = v;
}


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
static LLVMTypeRef
mk_int_type(LLVMContextRef ctx, size_t sz)
{
    switch(sz)
    {
    case  1:    return LLVMInt8TypeInContext(ctx);
    case  2:    return LLVMInt16TypeInContext(ctx);
    case  4:    return LLVMInt32TypeInContext(ctx);
    case  8:    return LLVMInt64TypeInContext(ctx);
    case 16:    return LLVMInt128TypeInContext(ctx);

    default:
        throw std::runtime_error("Bad integer size: " + std::to_string(sz));
    }
}

base_global_ctx_t::base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : llvm_ctx(ctx),
    builder(LLVMCreateBuilderInContext(ctx)),
    void_type     (LLVMVoidTypeInContext(ctx)),
    bool_type     (LLVMInt1TypeInContext(ctx)),
    char_type     (LLVMInt8TypeInContext(ctx)),
    short_type    (LLVMInt16TypeInContext(ctx)),
    int_type      (mk_int_type(ctx, int_size)),
    long_type     (mk_int_type(ctx, long_size)),
    long_long_type(LLVMInt64TypeInContext(ctx)),
    intptr_t_type (mk_int_type(ctx, ptr_size)),
    size_t_type   (mk_int_type(ctx, ptr_size)),
    char32_t_type (LLVMInt32TypeInContext(ctx)),
    opaque_void_type(LLVMStructCreateNamed(ctx, "struct.v_target_opaque_void")),
    void_ptr_type   (LLVMPointerType(opaque_void_type, 0))
{
#define DEF(name) \
    intrinsics[#name] = name;

    DEF(v_alloca)
    DEF(v_getelementptr)
    DEF(v_store)
    DEF(v_load)
    DEF(v_cast)

#undef DEF
}

base_global_ctx_t::~base_global_ctx_t()
{
    LLVMDisposeBuilder(builder);
}


//---------------------------------------------------------------------
int base_global_ctx_t::debug_print_module = 0;

void
base_global_ctx_t::verify_module(LLVMModuleRef module)
{
    char *msg = nullptr;

    auto err = LLVMVerifyModule(module, LLVMReturnStatusAction, &msg);

    if (err || debug_print_module)
    {
        char *txt = LLVMPrintModuleToString(module);

        printf("\n%s\n", txt);

        LLVMDisposeMessage(txt);

        printf("\n%s\n", msg);

        if (debug_print_module) debug_print_module -= 1;
    }

    LLVMDisposeMessage(msg);

    if (err)  exit(1);          //- Sic !!!
}


//---------------------------------------------------------------------
void
base_global_ctx_t::initialize(void)
{
    assert(voidc_global_ctx_t::voidc);

    auto opaque_type = voidc_global_ctx_t::voidc->LLVMOpaqueType_type;

#define DEF(name) \
    add_symbol(#name, opaque_type, (void *)name##_type);

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

    add_symbol("v_target_opaque_void", opaque_type, opaque_void_type);      //- ?
    DEF(void_ptr)

#undef DEF
}


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
base_local_ctx_t::base_local_ctx_t(const std::string _filename, base_global_ctx_t &_global)
  : filename(_filename),
    global_ctx(_global),
    parent_ctx(_global.local_ctx)
{
    global_ctx.local_ctx = this;
}

base_local_ctx_t::~base_local_ctx_t()
{
    global_ctx.local_ctx = parent_ctx;
}

//---------------------------------------------------------------------
const std::string
base_local_ctx_t::check_alias(const std::string &name)
{
    {   auto it = aliases.find(name);

        if (it != aliases.end())  return it->second;
    }

    {   auto it = global_ctx.aliases.find(name);

        if (it != global_ctx.aliases.end())  return it->second;
    }

    return  name;
}

//---------------------------------------------------------------------
bool
base_local_ctx_t::obtain_function(const std::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value)
{
    fun_type  = nullptr;
    fun_value = nullptr;

    if (auto p = vars.find(fun_name))
    {
        fun_value = *p;
    }
    else
    {
        auto raw_name = check_alias(fun_name);

        auto cname = raw_name.c_str();

        fun_value = LLVMGetNamedFunction(module, cname);

        if (!fun_value)
        {
            fun_type = find_symbol_type(cname);

            if (fun_type)
            {
                fun_value = LLVMAddFunction(module, cname, fun_type);
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
base_local_ctx_t::obtain_identifier(const std::string &name)
{
    LLVMValueRef value = nullptr;

    if (auto p = vars.find(name))
    {
        value = *p;
    }
    else
    {
        auto raw_name = check_alias(name);

        {   auto it = constants.find(raw_name);

            if (it != constants.end())  value = it->second;
        }

        if (!value)
        {
            auto it = global_ctx.constants.find(raw_name);

            if (it != global_ctx.constants.end())  value = it->second;
        }

        if (!value)
        {
            auto cname = raw_name.c_str();

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


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
static
voidc_global_ctx_t *voidc_global_ctx = nullptr;

voidc_global_ctx_t * const & voidc_global_ctx_t::voidc = voidc_global_ctx;
base_global_ctx_t  *         voidc_global_ctx_t::target;

LLVMTargetMachineRef voidc_global_ctx_t::target_machine;
LLVMOrcJITStackRef   voidc_global_ctx_t::jit;
LLVMPassManagerRef   voidc_global_ctx_t::pass_manager;

//---------------------------------------------------------------------
extern "C" LLVMTypeRef v_struct_create_named(const char *name);

voidc_global_ctx_t::voidc_global_ctx_t()
  : base_global_ctx_t(LLVMGetGlobalContext(), sizeof(int), sizeof(long), sizeof(intptr_t)),
    LLVMOpaqueType_type   (v_struct_create_named("struct.LLVMOpaqueType")),
    LLVMTypeRef_type      (LLVMPointerType(LLVMOpaqueType_type, 0)),
    LLVMOpaqueContext_type(v_struct_create_named("struct.LLVMOpaqueContext")),
    LLVMContextRef_type   (LLVMPointerType(LLVMOpaqueContext_type, 0))
{
    assert(voidc_global_ctx == nullptr);

    voidc_global_ctx = this;

    initialize();

#define DEF(name) \
    add_symbol(#name, LLVMOpaqueType_type, (void *)name##_type);

    DEF(LLVMOpaqueType)
    DEF(LLVMTypeRef)
    DEF(LLVMOpaqueContext)
    DEF(LLVMContextRef)

#undef DEF

    {   auto char_ptr_type = LLVMPointerType(char_type, 0);

        LLVMTypeRef args[] =
        {
            LLVMTypeRef_type,
            LLVMPointerType(LLVMTypeRef_type, 0),
            int_type,
            int_type
        };

#define DEF(name, ret, num) \
        add_symbol_type(#name, LLVMFunctionType(ret, args, num, false));

        DEF(LLVMFunctionType, LLVMTypeRef_type, 4)

        args[0] = char_ptr_type;
        args[1] = LLVMTypeRef_type;

        DEF(v_add_symbol_type, void_type, 2)

        DEF(v_struct_create_named, LLVMTypeRef_type, 1)

#undef DEF
    }
}


//---------------------------------------------------------------------
static char *voidc_triple = nullptr;
static LLVMTargetDataRef voidc_target_data_layout = nullptr;

//---------------------------------------------------------------------
void
voidc_global_ctx_t::static_initialize(void)
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

    voidc_target_data_layout = LLVMCreateTargetDataLayout(target_machine);
    voidc_triple             = LLVMGetTargetMachineTriple(target_machine);

    //-------------------------------------------------------------
    pass_manager = LLVMCreatePassManager();

    {   auto pm_builder = LLVMPassManagerBuilderCreate();

        LLVMPassManagerBuilderSetOptLevel(pm_builder, 3);       //- -O3
        LLVMPassManagerBuilderSetSizeLevel(pm_builder, 2);      //- -Oz

        LLVMPassManagerBuilderPopulateModulePassManager(pm_builder, pass_manager);

        LLVMPassManagerBuilderDispose(pm_builder);
    }

    //-------------------------------------------------------------
    target = new voidc_global_ctx_t();      //- Sic!

    //-------------------------------------------------------------
    voidc->add_symbol_value("voidc_resolver", (void *)voidc_local_ctx_t::resolver);

#define DEF(name) \
    voidc->add_symbol_value("voidc_" #name, (void *)name);

    DEF(target_machine)
    DEF(jit)
    DEF(pass_manager)

#undef DEF

    //-------------------------------------------------------------
    LLVMLoadLibraryPermanently(nullptr);        //- Sic!!!

    voidc->add_symbol_value("stdin",  (void *)stdin);       //- WTF?
    voidc->add_symbol_value("stdout", (void *)stdout);      //- WTF?
    voidc->add_symbol_value("stderr", (void *)stderr);      //- WTF?

    //-------------------------------------------------------------
#ifndef NDEBUG

//  LLVMEnablePrettyStackTrace();

#endif
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::static_terminate(void)
{
    LLVMDisposePassManager(pass_manager);

    LLVMDisposeMessage(voidc_triple);
    LLVMDisposeTargetData(voidc_target_data_layout);

    LLVMOrcDisposeInstance(jit);

    delete voidc;

    LLVMShutdown();
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::prepare_module_for_jit(LLVMModuleRef module)
{
    assert(target == voidc);    //- Sic!

    verify_module(module);

    //-------------------------------------------------------------
    LLVMSetModuleDataLayout(module, voidc_target_data_layout);
    LLVMSetTarget(module, voidc_triple);

    LLVMRunPassManager(pass_manager, module);

//  verify_module(module);
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_symbol_type(const char *raw_name, LLVMTypeRef type)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(jit, &m_name, raw_name);

    symbol_types[m_name] = type;

    LLVMOrcDisposeMangledSymbol(m_name);
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(jit, &m_name, raw_name);

    LLVMAddSymbol(m_name, value);

    LLVMOrcDisposeMangledSymbol(m_name);
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(jit, &m_name, raw_name);

    symbol_types[m_name] = type;

    LLVMAddSymbol(m_name, value);

    LLVMOrcDisposeMangledSymbol(m_name);
}


//---------------------------------------------------------------------
LLVMTypeRef
voidc_global_ctx_t::get_symbol_type(const char *raw_name)
{
    LLVMTypeRef type = nullptr;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(jit, &m_name, raw_name);

    {   auto it = symbol_types.find(m_name);

        if (it != symbol_types.end())  type = it->second;
    }

    LLVMOrcDisposeMangledSymbol(m_name);

    return type;
}

//---------------------------------------------------------------------
void *
voidc_global_ctx_t::get_symbol_value(const char *raw_name)
{
    void *value = nullptr;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(jit, &m_name, raw_name);

    value = LLVMSearchForAddressOfSymbol(m_name);

    LLVMOrcDisposeMangledSymbol(m_name);

    return value;
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::get_symbol(const char *raw_name, LLVMTypeRef &type, void * &value)
{
    type  = nullptr;
    value = nullptr;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(jit, &m_name, raw_name);

    {   auto it = symbol_types.find(m_name);

        if (it != symbol_types.end())  type = it->second;
    }

    value = LLVMSearchForAddressOfSymbol(m_name);

    LLVMOrcDisposeMangledSymbol(m_name);
}


//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
voidc_local_ctx_t::voidc_local_ctx_t(const std::string filename, base_global_ctx_t &global)
  : base_local_ctx_t(filename, global)
{}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(voidc_global_ctx_t::jit, &m_name, raw_name);

    symbols[m_name] = {type, value};

    LLVMOrcDisposeMangledSymbol(m_name);
}

//---------------------------------------------------------------------
LLVMTypeRef
voidc_local_ctx_t::find_type(const char *type_name)
{
    LLVMTypeRef tt = nullptr;

    void *tv = nullptr;

    char *m_name = nullptr;

    auto raw_name = check_alias(type_name);

    LLVMOrcGetMangledSymbol(voidc_global_ctx_t::jit, &m_name, raw_name.c_str());

    {   auto it = symbols.find(m_name);

        if (it != symbols.end())
        {
            auto &[t,v] = it->second;

            tt = t;
            tv = v;
        }
    }

    if (!tv)
    {
        auto it = voidc_global_ctx_t::voidc->symbol_types.find(m_name);

        if (it != voidc_global_ctx_t::voidc->symbol_types.end())
        {
            tt = it->second;
        }

        tv = LLVMSearchForAddressOfSymbol(m_name);
    }

    LLVMOrcDisposeMangledSymbol(m_name);

    if (tt != voidc_global_ctx_t::voidc->LLVMOpaqueType_type)
    {
        tv = nullptr;
    }

    return (LLVMTypeRef)tv;
}

//---------------------------------------------------------------------
LLVMTypeRef
voidc_local_ctx_t::find_symbol_type(const char *raw_name)
{
    LLVMTypeRef type = nullptr;

    char *m_name = nullptr;

    LLVMOrcGetMangledSymbol(voidc_global_ctx_t::jit, &m_name, raw_name);

    {   auto it = symbols.find(m_name);

        if (it != symbols.end())
        {
            type = it->second.first;
        }
    }

    if (!type)
    {
        auto it = voidc_global_ctx_t::voidc->symbol_types.find(m_name);

        if (it != voidc_global_ctx_t::voidc->symbol_types.end())
        {
            type = it->second;
        }
    }

    LLVMOrcDisposeMangledSymbol(m_name);

    return type;
}


//---------------------------------------------------------------------
uint64_t
voidc_local_ctx_t::resolver(const char *m_name, void *)
{
    auto &lctx = static_cast<voidc_local_ctx_t &>(*voidc_global_ctx->local_ctx);        //- voidc!

    {   auto it = lctx.symbols.find(m_name);

        if (it != lctx.symbols.end())
        {
            return  (uint64_t)it->second.second;
        }
    }

    return  (uint64_t)LLVMSearchForAddressOfSymbol(m_name);
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::prepare_unit_action(int line, int column)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

    auto &builder = global_ctx.builder;

    std::string hdr = "unit_" + std::to_string(line) + "_" + std::to_string(column);

    std::string mod_name = hdr + "_module";
    std::string fun_name = hdr + "_action";

    module = LLVMModuleCreateWithNameInContext(mod_name.c_str(), global_ctx.llvm_ctx);

    LLVMSetSourceFileName(module, filename.c_str(), filename.size());

    LLVMTypeRef  unit_ft = LLVMFunctionType(LLVMVoidType(), nullptr, 0, false);
    LLVMValueRef unit_f  = LLVMAddFunction(module, fun_name.c_str(), unit_ft);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(unit_f, "entry");

    LLVMPositionBuilderAtEnd(builder, entry);
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::finish_unit_action(void)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

    auto &builder = global_ctx.builder;

    LLVMBuildRetVoid(builder);

    LLVMClearInsertionPosition(builder);

    //-------------------------------------------------------------
    voidc_global_ctx_t::prepare_module_for_jit(module);

    //-------------------------------------------------------------
    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   module,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &unit_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(unit_buffer);

    //-------------------------------------------------------------
    LLVMDisposeModule(module);

    vars = variables_t();
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::run_unit_action(void)
{
    if (!unit_buffer) return;

    auto &jit = voidc_global_ctx_t::jit;

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

    if (!addr)
    {
        fflush(stdout);

        throw std::runtime_error("Symbol not found: " + fun_name);
    }

    void (*unit_action)() = (void (*)())addr;

    unit_action();

    LLVMOrcRemoveModule(jit, H);

    fflush(stdout);     //- WTF?
    fflush(stderr);     //- WTF?
}


//---------------------------------------------------------------------
//- Target Global Context
//---------------------------------------------------------------------
target_global_ctx_t::target_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size)
  : base_global_ctx_t(LLVMContextCreate(), int_size, long_size, ptr_size)
{
    initialize();
}

target_global_ctx_t::~target_global_ctx_t()
{
    LLVMContextDispose(llvm_ctx);
}


//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol_type(const char *raw_name, LLVMTypeRef type)
{
    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        it->second.first = type;
    }
    else
    {
        symbols[raw_name] = { type, nullptr };
    }
}

//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        it->second.second = value;
    }
    else
    {
        symbols[raw_name] = { nullptr, value };
    }
}

//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    symbols[raw_name] = { type, value };
}


//---------------------------------------------------------------------
LLVMTypeRef
target_global_ctx_t::get_symbol_type(const char *raw_name)
{
    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        return it->second.first;
    }

    return nullptr;
}

//---------------------------------------------------------------------
void *
target_global_ctx_t::get_symbol_value(const char *raw_name)
{
    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        return it->second.second;
    }

    return nullptr;
}

//---------------------------------------------------------------------
void
target_global_ctx_t::get_symbol(const char *raw_name, LLVMTypeRef &type, void * &value)
{
    type  = nullptr;
    value = nullptr;

    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        auto &[t,v] = it->second;

        type  = t;
        value = v;
    }
}


//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
target_local_ctx_t::target_local_ctx_t(const std::string filename, base_global_ctx_t &global)
  : base_local_ctx_t(filename, global)
{
}

//---------------------------------------------------------------------
void
target_local_ctx_t::add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    symbols[raw_name] = {type, value};
}

//---------------------------------------------------------------------
LLVMTypeRef
target_local_ctx_t::find_type(const char *type_name)
{
    LLVMTypeRef tt = nullptr;

    void *tv = nullptr;

    auto raw_name = check_alias(type_name);

    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        auto &[t,v] = it->second;

        tt = t;
        tv = v;
    }
    else
    {
        global_ctx.get_symbol(raw_name.c_str(), tt, tv);
    }

    if (tt != voidc_global_ctx_t::voidc->LLVMOpaqueType_type)
    {
        tv = nullptr;
    }

    return (LLVMTypeRef)tv;
}

//---------------------------------------------------------------------
LLVMTypeRef
target_local_ctx_t::find_symbol_type(const char *raw_name)
{
    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        return  it->second.first;
    }

    return global_ctx.get_symbol_type(raw_name);
}


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
LLVMTypeRef
v_struct_create_named(const char *name)
{
    auto voidc  = voidc_global_ctx_t::voidc;
    auto target = voidc_global_ctx_t::target;

    if (voidc  &&  target != voidc)
    {
        return LLVMStructCreateNamed(target->llvm_ctx, name);
    }

    //- Working for voidc...

    static std::map<v_quark_t, LLVMTypeRef> structs;

    auto quark = v_quark_from_string(name);

    auto it = structs.find(quark);

    if (it != structs.end())  return it->second;

    auto ret = LLVMStructCreateNamed(LLVMGetGlobalContext(), name);

    structs[quark] = ret;

    return ret;
}


//---------------------------------------------------------------------
void
v_add_symbol_type(const char *raw_name, LLVMTypeRef type)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol_type(raw_name, type);
}

void
v_add_symbol_value(const char *raw_name, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol_value(raw_name, value);
}

void
v_add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol(raw_name, type, value);
}

void
v_add_constant(const char *name, LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.constants[name] = val;
}

//---------------------------------------------------------------------
void
v_add_local_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_symbol(raw_name, type, value);
}

void
v_add_local_constant(const char *name, LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.constants[name] = val;
}

//---------------------------------------------------------------------
LLVMModuleRef
v_get_module(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.module;
}

void
v_set_module(LLVMModuleRef mod)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.module = mod;
}

//---------------------------------------------------------------------
void
v_debug_print_module(int n)
{
    base_global_ctx_t::debug_print_module = n;
}

void
v_verify_module(LLVMModuleRef module)
{
    base_global_ctx_t::verify_module(module);
}

void
voidc_prepare_module_for_jit(LLVMModuleRef module)
{
    voidc_global_ctx_t::prepare_module_for_jit(module);
}

//---------------------------------------------------------------------
void
voidc_prepare_unit_action(int line, int column)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.prepare_unit_action(line, column);
}

void
voidc_finish_unit_action(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.finish_unit_action();
}

//---------------------------------------------------------------------
void
v_add_variable(const char *name, LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = lctx.vars.set(name, val);
}

LLVMValueRef
v_get_variable(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *pv = lctx.vars.find(name))  return *pv;
    else                                  return nullptr;
}

//---------------------------------------------------------------------
void
v_clear_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = base_local_ctx_t::variables_t();
}

void
v_save_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars_stack.push_front(lctx.vars);
}

void
v_restore_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = lctx.vars_stack.front();

    lctx.vars_stack.pop_front();
}

//---------------------------------------------------------------------
LLVMValueRef
v_get_argument(int num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.args[num];
}

void
v_clear_arguments(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.args.clear();
    lctx.arg_types.clear();
}

//---------------------------------------------------------------------
const char *
v_get_return_name(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.ret_name;
}

void
v_set_return_value(LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.ret_value = val;
}


//---------------------------------------------------------------------
LLVMTypeRef
v_find_symbol_type(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    LLVMTypeRef type = nullptr;

    auto raw_name = lctx.check_alias(name);

    {   LLVMValueRef value = nullptr;

        {   auto it = lctx.constants.find(raw_name);

            if (it != lctx.constants.end())  value = it->second;
        }

        if (!value)
        {
            auto it = gctx.constants.find(raw_name);

            if (it != gctx.constants.end())  value = it->second;
        }

        if (value)
        {
            type = LLVMTypeOf(value);
        }
    }

    if (!type)
    {
        type = lctx.find_symbol_type(raw_name.c_str());
    }

    return type;
}

//---------------------------------------------------------------------
LLVMTypeRef
v_find_type(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto ret = lctx.find_type(name);

#ifndef NDEBUG

    if (!ret)
    {
        printf("v_find_type: %s  not found!\n", name);
    }

#endif

    return  ret;
}

//---------------------------------------------------------------------
void
v_add_alias(const char *name, const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.aliases[name] = raw_name;
}

void
v_add_local_alias(const char *name, const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.aliases[name] = raw_name;
}


//---------------------------------------------------------------------
void
v_add_intrinsic(const char *name, base_global_ctx_t::intrinsic_t fun)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.intrinsics[name] = fun;
}


//---------------------------------------------------------------------
LLVMContextRef
v_target_get_voidc_llvm_ctx(void)
{
    return voidc_global_ctx_t::voidc->llvm_ctx;
}

LLVMContextRef
v_target_get_llvm_ctx(void)
{
    return voidc_global_ctx_t::target->llvm_ctx;
}

//---------------------------------------------------------------------
LLVMBuilderRef
v_target_get_voidc_builder(void)
{
    return voidc_global_ctx_t::voidc->builder;
}

LLVMBuilderRef
v_target_get_builder(void)
{
    return voidc_global_ctx_t::target->builder;
}


//---------------------------------------------------------------------
base_global_ctx_t *
v_target_get_voidc_global_ctx(void)
{
    return voidc_global_ctx_t::voidc;
}

base_global_ctx_t *
v_target_get_global_ctx(void)
{
    return voidc_global_ctx_t::target;
}

void
v_target_set_global_ctx(base_global_ctx_t *gctx)
{
    voidc_global_ctx_t::target = gctx;
}


//---------------------------------------------------------------------
base_global_ctx_t *
v_target_create_global_ctx(size_t int_size, size_t long_size, size_t ptr_size)
{
    return  new target_global_ctx_t(int_size, long_size, ptr_size);
}

void
v_target_destroy_global_ctx(base_global_ctx_t *gctx)
{
    delete gctx;
}

//---------------------------------------------------------------------
base_local_ctx_t *
v_target_create_local_ctx(const char *filename, base_global_ctx_t *gctx)
{
    return  new target_local_ctx_t(filename, *gctx);
}

void
v_target_destroy_local_ctx(base_local_ctx_t *lctx)
{
    delete lctx;
}


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


