//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"

#include <stdexcept>
#include <regex>
#include <cassert>

#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Object.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>
#include <llvm-c/Transforms/IPO.h>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/CBindingWrapping.h>

#include "voidc_compiler.h"


//---------------------------------------------------------------------
using namespace llvm;
using namespace llvm::orc;

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(JITDylib, LLVMOrcJITDylibRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LLJIT, LLVMOrcLLJITRef)


//---------------------------------------------------------------------
//- Base Compilation Context
//---------------------------------------------------------------------
void
base_compile_ctx_t::declarations_t::insert(const declarations_t &other)
{
    if (other.empty())  return;

    for (auto it : other.aliases)     aliases_insert(it);
    for (auto it : other.constants)   constants_insert(it);
    for (auto it : other.symbols)     symbols_insert(it);
    for (auto it : other.intrinsics)  intrinsics_insert(it);
}


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
base_global_ctx_t::base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : voidc_types_ctx_t(ctx, int_size, long_size, ptr_size),
    builder(LLVMCreateBuilderInContext(ctx)),
    char_ptr_type(make_pointer_type(char_type, 0)),         //- address space 0 !?
    void_ptr_type(make_pointer_type(void_type, 0))          //- address space 0 !?
{}

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

    //-----------------------------------------------------------------
    auto opaque_type = voidc_global_ctx_t::voidc->opaque_type_type;

    auto add_type = [this, opaque_type](const char *raw_name, v_type_t *type)
    {
        decls.constants_insert({raw_name, type});

        decls.symbols_insert({raw_name, opaque_type});

        add_symbol_value(raw_name, type);
    };

#define DEF(name) \
    add_type(#name, name##_type);

    DEF(void)

    DEF(bool)
    DEF(char)
    DEF(short)
    DEF(int)
    DEF(unsigned)
    DEF(long)
    DEF(long_long)
    DEF(intptr_t)
    DEF(size_t)
    DEF(char32_t)
    DEF(uint64_t)

#undef DEF

    //-----------------------------------------------------------------
    auto add_bool_const = [this](const char *raw_name, bool value)
    {
        decls.constants_insert({raw_name, bool_type});

        constant_values.insert({raw_name, LLVMConstInt(bool_type->llvm_type(), value, false)});
    };

    add_bool_const("false", false);
    add_bool_const("true",  true);

    //-----------------------------------------------------------------
    v_type_t *i8_ptr_type = make_pointer_type(make_int_type(8), 0);

    auto stacksave_ft    = make_function_type(i8_ptr_type, nullptr, 0, false);
    auto stackrestore_ft = make_function_type(void_type, &i8_ptr_type, 1, false);

    decls.symbols_insert({"llvm.stacksave",    stacksave_ft});
    decls.symbols_insert({"llvm.stackrestore", stackrestore_ft});
}


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
base_local_ctx_t::base_local_ctx_t(base_global_ctx_t &_global)
  : global_ctx(_global),
    parent_ctx(_global.local_ctx)
{
    global_ctx.local_ctx = this;

    decls.insert(global_ctx.decls);
}

base_local_ctx_t::~base_local_ctx_t()
{
    global_ctx.local_ctx = parent_ctx;
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_alias(const char *name, const char *raw_name)
{
    if (export_decls)   export_decls->aliases_insert({name, raw_name});

    decls.aliases_insert({name, raw_name});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_alias(const char *name, const char *raw_name)
{
    decls.aliases_insert({name, raw_name});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    if (export_decls)   export_decls->constants_insert({raw_name, type});

    decls.constants_insert({raw_name, type});

    if (value)  global_ctx.constant_values.insert({raw_name, value});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    decls.constants_insert({raw_name, type});

    if (value)  constant_values.insert({raw_name, value});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_symbol(const char *raw_name, v_type_t *type, void *value)
{
    if (type)
    {
        if (export_decls)   export_decls->symbols_insert({raw_name, type});

        decls.symbols_insert({raw_name, type});
    }

    if (value)  global_ctx.add_symbol_value(raw_name, value);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    if (type)   decls.symbols_insert({raw_name, type});

    if (value)  add_symbol_value(raw_name, value);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_intrinsic(const char *fun_name, void *fun, void *aux)
{
    if (export_decls)   export_decls->intrinsics_insert({fun_name, {fun, aux}});

    decls.intrinsics_insert({fun_name, {fun, aux}});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_intrinsic(const char *fun_name, void *fun, void *aux)
{
    decls.intrinsics_insert({fun_name, {fun, aux}});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_type(const char *raw_name, v_type_t *type)
{
    export_constant(raw_name, type, nullptr);

    export_symbol(raw_name, voidc_global_ctx_t::voidc->opaque_type_type, type);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_type(const char *raw_name, v_type_t *type)
{
    add_constant(raw_name, type, nullptr);

    add_symbol(raw_name, voidc_global_ctx_t::voidc->opaque_type_type, type);
}


//---------------------------------------------------------------------
const std::string
base_local_ctx_t::check_alias(const std::string &name)
{
    if (auto p = decls.aliases.find(name))  return *p;

    return  name;
}

//---------------------------------------------------------------------
v_type_t *
base_local_ctx_t::find_type(const char *type_name)
{
    if (auto *p = vars.find(type_name))
    {
        if (p->second == nullptr)   return p->first;    //- Sic!
    }

    auto raw_name = check_alias(type_name);

    auto cname = raw_name.c_str();

    {   v_type_t    *t = nullptr;
        LLVMValueRef v = nullptr;

        if (find_constant(cname, t, v)  &&  v == nullptr)
        {
            return t;
        }
    }

    return nullptr;
}


//---------------------------------------------------------------------
bool
base_local_ctx_t::find_constant(const char *raw_name, v_type_t * &type, LLVMValueRef &value)
{
    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto p = decls.constants.find(raw_name))
    {
        t = *p;

        {   auto itv = constant_values.find(raw_name);

            if (itv != constant_values.end())
            {
                v = itv->second;
            }
        }

        if (!v)
        {
            auto itv = global_ctx.constant_values.find(raw_name);

            if (itv != global_ctx.constant_values.end())
            {
                v = itv->second;
            }
        }
    }

    if (!t) return false;

    type  = t;
    value = v;

    return true;
}

//---------------------------------------------------------------------
bool
base_local_ctx_t::find_symbol(const char *raw_name, v_type_t * &type, void * &value)
{
    v_type_t *t = nullptr;
    void     *v = nullptr;

    t = get_symbol_type(raw_name);

    if (!t) return false;

    v = find_symbol_value(raw_name);

    type  = t;
    value = v;

    return true;
}


//---------------------------------------------------------------------
bool
base_local_ctx_t::obtain_identifier(const std::string &name, v_type_t * &type, LLVMValueRef &value)
{
    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto *p = vars.find(name))
    {
        t = p->first;
        v = p->second;
    }
    else
    {
        auto raw_name = check_alias(name);

        auto cname = raw_name.c_str();

        if (!find_constant(cname, t, v))
        {
            t = get_symbol_type(cname);

            if (!t)  return false;

            if (auto *ft = dynamic_cast<v_type_function_t *>(t))
            {
                v = LLVMGetNamedFunction(module, cname);

                if (!v) v = LLVMAddFunction(module, cname, ft->llvm_type());

                t = global_ctx.make_pointer_type(t, 0);         //- Sic!
            }
            else
            {
                v = LLVMGetNamedGlobal(module, cname);

                v_type_t *st = t;

                bool is_reference = (t->kind() == v_type_t::k_reference);

                if (is_reference) st = static_cast<v_type_reference_t *>(st)->element_type();

                if (!v) v = LLVMAddGlobal(module, st->llvm_type(), cname);

                if (!is_reference)  t = global_ctx.make_pointer_type(t, 0);         //- Sic!
            }
        }
    }

    assert(t);

    type  = t;
    value = v;

    return true;
}


//---------------------------------------------------------------------
LLVMValueRef
base_local_ctx_t::prepare_function(const char *name, v_type_t *type)
{
    LLVMValueRef f = LLVMAddFunction(module, name, type->llvm_type());

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(f, "entry");

    LLVMPositionBuilderAtEnd(global_ctx.builder, entry);

    vars = variables_t();       //- Sic!

    auto ret_type = static_cast<v_type_function_t *>(type)->return_type();

    vars = vars.set("voidc.internal_return_type", {ret_type, nullptr});

    if (ret_type->kind() != v_type_t::k_void)
    {
        auto ret_value = LLVMBuildAlloca(global_ctx.builder, ret_type->llvm_type(), "ret_value");

        vars = vars.set("voidc.internal_return_value", {nullptr, ret_value});   //- Sic!
    }

    function_leave_b = LLVMAppendBasicBlock(f, "f_leave_b");
    auto f_leave_bv  = LLVMBasicBlockAsValue(function_leave_b);

    vars = vars.set("voidc.internal_branch_target_leave", {nullptr, f_leave_bv});   //- Sic!

    return f;
}

//---------------------------------------------------------------------
void
base_local_ctx_t::finish_function(void)
{
    auto cur_b = LLVMGetInsertBlock(global_ctx.builder);

    if (!LLVMGetBasicBlockTerminator(cur_b))
    {
        auto leave_bv = vars["voidc.internal_branch_target_leave"].second;
        auto leave_b  = LLVMValueAsBasicBlock(leave_bv);

        LLVMBuildBr(global_ctx.builder, leave_b);
    }


    LLVMMoveBasicBlockAfter(function_leave_b, cur_b);

    LLVMPositionBuilderAtEnd(global_ctx.builder, function_leave_b);


    auto ret_type = vars["voidc.internal_return_type"].first;

    if (ret_type->kind() == v_type_t::k_void)
    {
        LLVMBuildRetVoid(global_ctx.builder);
    }
    else
    {
        auto ret_var_v = vars["voidc.internal_return_value"].second;
        auto ret_value = LLVMBuildLoad(global_ctx.builder, ret_var_v, "ret_value");

        LLVMBuildRet(global_ctx.builder, ret_value);
    }


    LLVMClearInsertionPosition(global_ctx.builder);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::adopt_result(v_type_t *type, LLVMValueRef value)
{
    bool src_ref = (type->kind() == v_type_t::k_reference);

    switch(intptr_t(result_type))
    {
    case -1:        //- Unreference...

        if (src_ref)
        {
            result_type = static_cast<v_type_reference_t *>(type)->element_type();

            value = LLVMBuildLoad(global_ctx.builder, value, "tmp");

            break;
        }

        //- Fallthrough!

    case  0:        //- Get "as is"...

        result_type = type;

        break;

    default:        //- Adopt...

        if (result_type == type)  break;

        bool dst_ref = (result_type->kind() == v_type_t::k_reference);

        auto dst_typ = result_type;

        if (dst_ref)  dst_typ = static_cast<v_type_reference_t *>(dst_typ)->element_type();

        auto src_typ = type;

        if (src_ref)
        {
            src_typ = static_cast<v_type_reference_t *>(src_typ)->element_type();

            value = LLVMBuildLoad(global_ctx.builder, value, "tmp");
        }

        if (src_typ != dst_typ)
        {
            if (dst_typ == global_ctx.void_ptr_type  &&
                src_typ->kind() == v_type_t::k_pointer)
            {
                value = LLVMBuildPointerCast(global_ctx.builder, value, dst_typ->llvm_type(), "");
            }
            else
            {
                value = v_convert_to_type(src_typ, value, dst_typ);     //- "generic" ...
            }
        }

        if (dst_ref)  value = make_temporary(dst_typ, value);
    }

    result_value = value;
}


//---------------------------------------------------------------------
LLVMValueRef
base_local_ctx_t::make_temporary(v_type_t *type, LLVMValueRef value)
{
    if (!temporaries_stack.front())
    {
        v_type_t    *llvm_stacksave_ft;
        LLVMValueRef llvm_stacksave_f;

        obtain_identifier("llvm.stacksave", llvm_stacksave_ft, llvm_stacksave_f);

        temporaries_stack.front() = LLVMBuildCall(global_ctx.builder, llvm_stacksave_f, nullptr, 0, "tmp_stack_ptr");
    }

    auto tmp = LLVMBuildAlloca(global_ctx.builder, type->llvm_type(), "tmp");

    LLVMBuildStore(global_ctx.builder, value, tmp);

    return  tmp;
}

//---------------------------------------------------------------------
void
base_local_ctx_t::push_temporaries(void)
{
    temporaries_stack.push_front(nullptr);      //- Sic!
}

//---------------------------------------------------------------------
void
base_local_ctx_t::pop_temporaries(void)
{
    auto stack_ptr = temporaries_stack.front();

    temporaries_stack.pop_front();

    if (!stack_ptr) return;

    v_type_t    *llvm_stackrestore_ft;
    LLVMValueRef llvm_stackrestore_f;

    obtain_identifier("llvm.stackrestore", llvm_stackrestore_ft, llvm_stackrestore_f);

    LLVMBuildCall(global_ctx.builder, llvm_stackrestore_f, &stack_ptr, 1, "");
}

//---------------------------------------------------------------------
extern "C"
{

static LLVMValueRef
v_convert_to_type_default(v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
{
    if (t0 == t1)   return v0;      //- No conversion...

    //- Only simple scalar "promotions" ...
    //- t1 must be "bigger or equal" than t0 ...

    LLVMOpcode opcode = LLVMOpcode(0);      //- ?

    if (v_type_is_floating_point(t1))
    {
        if (v_type_is_floating_point(t0))
        {
            opcode = LLVMFPExt;
        }
        else
        {
            assert(dynamic_cast<v_type_integer_t *>(t0));

            auto t = static_cast<v_type_integer_t *>(t0);

            if (t->is_signed()) opcode = LLVMSIToFP;
            else                opcode = LLVMUIToFP;
        }
    }
    else
    {
        assert(dynamic_cast<v_type_integer_t *>(t0));

        auto t = static_cast<v_type_integer_t *>(t0);

        if (t->is_signed()) opcode = LLVMSExt;
        else                opcode = LLVMZExt;
    }

    auto &gctx = *voidc_global_ctx_t::target;

    return  LLVMBuildCast(gctx.builder, opcode, v0, t1->llvm_type(), "");
}

VOIDC_DLLEXPORT_BEGIN_VARIABLE

    LLVMValueRef (*v_convert_to_type)(v_type_t *t0, LLVMValueRef v0, v_type_t *t1) = v_convert_to_type_default;

VOIDC_DLLEXPORT_END
}


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
static
voidc_global_ctx_t *voidc_global_ctx = nullptr;

voidc_global_ctx_t * const & voidc_global_ctx_t::voidc = voidc_global_ctx;
base_global_ctx_t  *         voidc_global_ctx_t::target;

LLVMOrcLLJITRef      voidc_global_ctx_t::jit;
LLVMOrcJITDylibRef   voidc_global_ctx_t::main_jd;

LLVMTargetMachineRef voidc_global_ctx_t::target_machine;
LLVMPassManagerRef   voidc_global_ctx_t::pass_manager;

//---------------------------------------------------------------------
voidc_global_ctx_t::voidc_global_ctx_t()
  : base_global_ctx_t(LLVMGetGlobalContext(), sizeof(int), sizeof(long), sizeof(intptr_t)),
    opaque_type_type(make_struct_type("struct.voidc_opaque_type")),
    type_ptr_type(make_pointer_type(opaque_type_type, 0))
{
    assert(voidc_global_ctx == nullptr);

    voidc_global_ctx = this;

    auto add_type = [this](const char *raw_name, v_type_t *type)
    {
        decls.constants_insert({raw_name, type});

        decls.symbols_insert({raw_name, opaque_type_type});

        add_symbol_value(raw_name, type);
    };

    add_type("voidc_opaque_type", opaque_type_type);        //- Sic!

    {   add_type("v_type_ptr", type_ptr_type);

        v_type_t *types[] =
        {
            type_ptr_type,
            make_pointer_type(type_ptr_type, 0),
            unsigned_type,
            bool_type
        };

#define DEF(name, ret, num) \
        decls.symbols_insert({#name, make_function_type(ret, types, num, false)});

        DEF(v_function_type, type_ptr_type, 4)

        types[0] = char_ptr_type;
        types[1] = type_ptr_type;
        types[2] = make_pointer_type(void_type, 0);

        DEF(v_add_symbol, void_type, 3)

#undef DEF
    }

#ifdef _WIN32

    decls.constants_insert({"_WIN32", bool_type});

    constant_values.insert({"_WIN32", LLVMConstInt(bool_type->llvm_type(), true, false)});      //- ?

#endif
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
    LLVMOrcCreateLLJIT(&jit, 0);            //- Sic!

    main_jd = LLVMOrcLLJITGetMainJITDylib(jit);

    {   LLVMOrcDefinitionGeneratorRef gen = nullptr;

        LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(&gen, LLVMOrcLLJITGetGlobalPrefix(jit), nullptr, nullptr);

        LLVMOrcJITDylibAddGenerator(main_jd, gen);
    }

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

    LLVMAddAlwaysInlinerPass(pass_manager);

    //-------------------------------------------------------------
    target = new voidc_global_ctx_t();      //- Sic!

    voidc_types_static_initialize();        //- Sic!

    static_cast<voidc_global_ctx_t *>(target)->initialize();    //- Sic!

    //-------------------------------------------------------------
#define DEF(name) \
    voidc->add_symbol_value("voidc_" #name, (void *)name);

    DEF(target_machine)
    DEF(pass_manager)

#undef DEF

    //-------------------------------------------------------------
    {   v_type_t *typ[3];

        typ[0] = voidc->char_ptr_type;
        typ[1] = voidc->size_t_type;
        typ[2] = voidc->bool_type;

        auto ft = voidc->make_function_type(voidc->void_type, typ, 3, false);

        voidc->decls.symbols_insert({"voidc_object_file_load_to_jit_internal_helper", ft});
    }

    //-------------------------------------------------------------
    voidc->flush_unit_symbols();

    //-------------------------------------------------------------
#ifndef NDEBUG

//  LLVMEnablePrettyStackTrace();

#endif
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::static_terminate(void)
{
    voidc->run_cleaners();

    voidc_types_static_terminate();

    LLVMDisposePassManager(pass_manager);

    LLVMDisposeMessage(voidc_triple);
    LLVMDisposeTargetData(voidc_target_data_layout);

    LLVMOrcDisposeLLJIT(jit);

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
static LLVMOrcJITTargetAddress
add_object_file_to_jit_with_rt(LLVMMemoryBufferRef membuf,
                               LLVMOrcJITDylibRef jd,
                               LLVMOrcResourceTrackerRef rt,
                               bool search_action = false)
{
    char *msg = nullptr;

    auto &jit = voidc_global_ctx_t::jit;

    //-------------------------------------------------------------
    auto bref = LLVMCreateBinary(membuf, nullptr, &msg);
    assert(bref);

    auto mb_copy = LLVMBinaryCopyMemoryBuffer(bref);

    //-------------------------------------------------------------
    auto lerr = LLVMOrcLLJITAddObjectFileWithRT(jit, rt, mb_copy);

    if (lerr)
    {
        msg = LLVMGetErrorMessage(lerr);

        printf("\n%s\n", msg);

        LLVMDisposeErrorMessage(msg);
    }

    //-------------------------------------------------------------
    LLVMOrcJITTargetAddress ret = 0;

    auto it = LLVMObjectFileCopySymbolIterator(bref);

    static std::regex act_regex("unit_[0-9]+_[0-9]+_action");

    while(!LLVMObjectFileIsSymbolIteratorAtEnd(bref, it))
    {
        auto sname = LLVMGetSymbolName(it);

        if (auto sym = unwrap(jit)->lookup(*unwrap(jd), sname))
        {
            auto addr = sym->getAddress();

            if (search_action  &&  std::regex_match(sname, act_regex))
            {
                ret = addr;
            }
        }

        LLVMMoveToNextSymbol(it);
    }

    LLVMDisposeSymbolIterator(it);

    //-------------------------------------------------------------
    LLVMDisposeBinary(bref);


//  unwrap(jd)->dump(outs());


    return ret;
}

//---------------------------------------------------------------------
static LLVMOrcJITTargetAddress
add_object_file_to_jit(LLVMMemoryBufferRef membuf,
                       LLVMOrcJITDylibRef jd,
                       bool search_action = false)
{
    return add_object_file_to_jit_with_rt(membuf, jd, LLVMOrcJITDylibGetDefaultResourceTracker(jd), search_action);
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_module_to_jit(LLVMModuleRef module)
{
    assert(target == voidc);    //- Sic!

//  verify_module(module);

    //-------------------------------------------------------------
    LLVMMemoryBufferRef mod_buffer = nullptr;

    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   module,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &mod_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(mod_buffer);

    add_object_file_to_jit(mod_buffer, main_jd);

    LLVMDisposeMemoryBuffer(mod_buffer);
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    if (unwrap(jit)->lookup(*unwrap(main_jd), raw_name)) return;

    unit_symbols[unwrap(jit)->mangleAndIntern(raw_name)] = JITEvaluatedSymbol::fromPointer(value);
}


//---------------------------------------------------------------------
void
voidc_global_ctx_t::flush_unit_symbols(void)
{
    if (unit_symbols.empty()) return;

    auto err = unwrap(main_jd)->define(absoluteSymbols(unit_symbols));

    unit_symbols.clear();
}


//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
voidc_local_ctx_t::voidc_local_ctx_t(voidc_global_ctx_t &global)
  : base_local_ctx_t(global)
{
    auto es = LLVMOrcLLJITGetExecutionSession(voidc_global_ctx_t::jit);

    std::string jd_name("local_jd_" + std::to_string(global.local_jd_hash));

    global.local_jd_hash += 1;

    LLVMOrcExecutionSessionCreateJITDylib(es, &local_jd, jd_name.c_str());

    assert(local_jd);

    setup_link_order();
}

//---------------------------------------------------------------------
voidc_local_ctx_t::~voidc_local_ctx_t()
{
    run_cleaners();

    if (parent_ctx)
    {
        //- Restore link order

        static_cast<voidc_local_ctx_t *>(parent_ctx)->setup_link_order();
    }
    else    //- ?
    {
        unwrap(voidc_global_ctx_t::main_jd)->removeFromLinkOrder(*unwrap(local_jd));
    }

    LLVMOrcJITDylibClear(local_jd);         //- So, how to "delete" local_jd ?
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    auto &jit = voidc_global_ctx_t::jit;

    if (unwrap(jit)->lookup(*unwrap(local_jd), raw_name)) return;

    unit_symbols[unwrap(jit)->mangleAndIntern(raw_name)] = JITEvaluatedSymbol::fromPointer(value);
}


//---------------------------------------------------------------------
void *
voidc_local_ctx_t::find_symbol_value(const char *raw_name)
{
    auto sym = unwrap(voidc_global_ctx_t::jit)->lookup(*unwrap(local_jd), raw_name);

    if (!sym)
    {
        sym = unwrap(voidc_global_ctx_t::jit)->lookup(*unwrap(voidc_global_ctx_t::main_jd), raw_name);
    }

    if (sym)  return (void *)sym->getAddress();

    return nullptr;
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_module_to_jit(LLVMModuleRef mod)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

//  verify_module(mod);

    //-------------------------------------------------------------
    LLVMMemoryBufferRef mod_buffer = nullptr;

    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   mod,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &mod_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(mod_buffer);

    add_object_file_to_jit(mod_buffer, local_jd);

    LLVMDisposeMemoryBuffer(mod_buffer);
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

    prepare_function(fun_name.c_str(), global_ctx.make_function_type(global_ctx.void_type, nullptr, 0, false));
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::finish_unit_action(void)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

    finish_function();

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

    auto rt = LLVMOrcJITDylibCreateResourceTracker(local_jd);

    auto addr = add_object_file_to_jit_with_rt(unit_buffer, local_jd, rt, true);

    void (*unit_action)() = (void (*)())addr;

    unit_action();

    flush_unit_symbols();

    LLVMOrcResourceTrackerRemove(rt);
    LLVMOrcReleaseResourceTracker(rt);      //- ?

    fflush(stdout);     //- WTF?
    fflush(stderr);     //- WTF?
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::flush_unit_symbols(void)
{
    if (!unit_symbols.empty())
    {
        auto err = unwrap(local_jd)->define(absoluteSymbols(unit_symbols));

        unit_symbols.clear();
    }

    voidc_global_ctx_t::voidc->flush_unit_symbols();
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::setup_link_order(void)
{
#ifdef _WIN32
    auto flags = JITDylibLookupFlags::MatchAllSymbols;
#else
    auto flags = JITDylibLookupFlags::MatchExportedSymbolsOnly;
#endif

    auto g_jd = unwrap(voidc_global_ctx_t::main_jd);
    auto l_jd = unwrap(local_jd);

    JITDylibSearchOrder so =
    {
        { l_jd, flags },    //- First  - the local
        { g_jd, flags }     //- Second - the global
    };

    g_jd->setLinkOrder(so, false);      //- Set "as is"
    l_jd->setLinkOrder(so, false);      //- Set "as is"
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
    run_cleaners();

    LLVMContextDispose(llvm_ctx);
}


//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    symbol_values.insert({raw_name, value});
}


//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
target_local_ctx_t::target_local_ctx_t(base_global_ctx_t &global)
  : base_local_ctx_t(global)
{
}

target_local_ctx_t::~target_local_ctx_t()
{
    run_cleaners();
}


//---------------------------------------------------------------------
void
target_local_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    symbol_values.insert({raw_name, value});
}


//---------------------------------------------------------------------
void *
target_local_ctx_t::find_symbol_value(const char *raw_name)
{
    for (auto &sv : {symbol_values, static_cast<target_global_ctx_t &>(global_ctx).symbol_values})
    {
        auto it = sv.find(raw_name);

        if (it != sv.end())
        {
            return  it->second;
        }
    }

    return  nullptr;
}


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
void
v_export_symbol_type(const char *raw_name, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_symbol(raw_name, type, nullptr);
}

void
v_export_symbol_value(const char *raw_name, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_symbol(raw_name, nullptr, value);
}

void
v_export_symbol(const char *raw_name, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_symbol(raw_name, type, value);
}

void
v_export_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_constant(raw_name, type, value);
}

void
v_export_type(const char *raw_name, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_type(raw_name, type);
}

//---------------------------------------------------------------------
void
v_add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_symbol(raw_name, type, value);
}

void
v_add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_constant(raw_name, type, value);
}

void
v_add_type(const char *raw_name, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_type(raw_name, type);
}

//---------------------------------------------------------------------
bool
v_find_constant(const char *raw_name, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto p = lctx.decls.constants.find(raw_name))
    {
        t = *p;

        if (value)
        {
            {   auto itv = lctx.constant_values.find(raw_name);

                if (itv != lctx.constant_values.end())
                {
                    v = itv->second;
                }
            }

            if (!v)
            {
                auto itv = gctx.constant_values.find(raw_name);

                if (itv != gctx.constant_values.end())
                {
                    v = itv->second;
                }
            }
        }
    }

    if (type)   *type  = t;
    if (value)  *value = v;

    return bool(t);
}

v_type_t *
v_find_constant_type(const char *raw_name)
{
    v_type_t *t = nullptr;

    v_find_constant(raw_name, &t, nullptr);

    return t;
}

LLVMValueRef
v_find_constant_value(const char *raw_name)
{
    LLVMValueRef v = nullptr;

    v_find_constant(raw_name, nullptr, &v);

    return v;
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

void
voidc_add_module_to_jit(LLVMModuleRef module)
{
    voidc_global_ctx_t::add_module_to_jit(module);
}

void
voidc_add_local_module_to_jit(LLVMModuleRef module)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.add_module_to_jit(module);
}

//---------------------------------------------------------------------
void
voidc_add_object_file_to_jit(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

    add_object_file_to_jit(membuf, gctx.main_jd);
}

void
voidc_add_local_object_file_to_jit(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    add_object_file_to_jit(membuf, lctx.local_jd);
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
voidc_set_unit_buffer(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.unit_buffer = membuf;
}

LLVMMemoryBufferRef
voidc_get_unit_buffer(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    return  lctx.unit_buffer;
}

//---------------------------------------------------------------------
void
voidc_flush_unit_symbols(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.flush_unit_symbols();
}


//---------------------------------------------------------------------
void
voidc_object_file_load_to_jit_internal_helper(const char *buf, size_t len, bool is_local)
{
    auto modbuf = LLVMCreateMemoryBufferWithMemoryRange(buf, len, "modbuf", 0);

    if (is_local) voidc_add_local_object_file_to_jit(modbuf);
    else          voidc_add_object_file_to_jit(modbuf);

    LLVMDisposeMemoryBuffer(modbuf);
}

void
voidc_compile_load_module_to_jit(LLVMModuleRef module, bool is_local)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    //- Emit module -> membuf

    LLVMMemoryBufferRef membuf = nullptr;

    char *msg = nullptr;

    auto err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine,
                                                   module,
                                                   LLVMObjectFile,
                                                   &msg,
                                                   &membuf);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(membuf);

    auto membuf_ptr  = LLVMGetBufferStart(membuf);
    auto membuf_size = LLVMGetBufferSize(membuf);

    auto membuf_const = LLVMConstString(membuf_ptr, membuf_size, 1);
    auto membuf_const_type = LLVMTypeOf(membuf_const);

    auto membuf_glob = LLVMAddGlobal(lctx.module, membuf_const_type, "membuf_g");

    LLVMSetLinkage(membuf_glob, LLVMPrivateLinkage);

    LLVMSetInitializer(membuf_glob, membuf_const);

    LLVMValueRef val[3];

    val[0] = LLVMConstInt(gctx.int_type->llvm_type(), 0, 0);
    val[1] = val[0];

    auto membuf_const_ptr = LLVMBuildGEP(gctx.builder, membuf_glob, val, 2, "membuf_const_ptr");

    v_type_t    *t;
    LLVMValueRef f;

    lctx.obtain_identifier("voidc_object_file_load_to_jit_internal_helper", t, f);
    assert(f);

    val[0] = membuf_const_ptr;
    val[1] = LLVMConstInt(gctx.size_t_type->llvm_type(), membuf_size, 0);
    val[2] = LLVMConstInt(gctx.bool_type->llvm_type(), is_local, 0);

    LLVMBuildCall(gctx.builder, f, val, 3, "");

    LLVMDisposeMemoryBuffer(membuf);
}


VOIDC_DLLEXPORT_END


static void
load_module_helper(LLVMModuleRef module, bool is_local)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    gctx.prepare_module_for_jit(module);

    if (lctx.module)    //- Called directly from unit ...
    {
        if (is_local) lctx.add_module_to_jit(module);
        else          gctx.add_module_to_jit(module);
    }

    //- Generate unit ...

    auto saved_module = lctx.module;

    //- Do replace/make unit...

    if (lctx.unit_buffer)   LLVMDisposeMemoryBuffer(lctx.unit_buffer);

    static int column = 0;

    lctx.prepare_unit_action(0, column++);      //- line, column ?..

    voidc_compile_load_module_to_jit(module, is_local);

//  base_global_ctx_t::debug_print_module = 1;

    lctx.finish_unit_action();

    lctx.module = saved_module;
}


VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
void
voidc_unit_load_local_module_to_jit(LLVMModuleRef module)
{
    load_module_helper(module, true);
}

//---------------------------------------------------------------------
void
voidc_unit_load_module_to_jit(LLVMModuleRef module)
{
    load_module_helper(module, false);
}


//---------------------------------------------------------------------
LLVMValueRef
v_prepare_function(const char *name, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.prepare_function(name, type);
}

//---------------------------------------------------------------------
void
v_finish_function(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.finish_function();
}


//---------------------------------------------------------------------
void
v_add_variable(const char *name, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = lctx.vars.set(name, {type, value});
}

v_type_t *
v_get_variable_type(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *pv = lctx.vars.find(name))  return pv->first;
    else                                  return nullptr;
}

LLVMValueRef
v_get_variable_value(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *pv = lctx.vars.find(name))  return pv->second;
    else                                  return nullptr;
}

bool
v_get_variable(const char *name, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *pv = lctx.vars.find(name))
    {
        if (type)   *type  = pv->first;
        if (value)  *value = pv->second;

        return true;
    }

    return false;
}

//---------------------------------------------------------------------
size_t
v_get_variables_size(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.vars.size();
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

    lctx.vars_stack.push_front({lctx.decls, lctx.vars});
}

void
v_restore_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &top = lctx.vars_stack.front();

    lctx.decls = top.first;
    lctx.vars  = top.second;

    lctx.vars_stack.pop_front();
}

//---------------------------------------------------------------------
v_type_t *
v_get_result_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.result_type;
}

void
v_set_result_type(v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = type;
}

LLVMValueRef
v_get_result_value(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.result_value;
}

void
v_set_result_value(LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_value = val;
}

//---------------------------------------------------------------------
void
v_adopt_result(v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.adopt_result(type, value);
}

//---------------------------------------------------------------------
LLVMValueRef
v_make_temporary(v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.make_temporary(type, value);
}

void
v_push_temporaries(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_temporaries();
}

void
v_pop_temporaries(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.pop_temporaries();
}


//---------------------------------------------------------------------
v_type_t *
v_find_symbol_type(const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_symbol_type(raw_name);
}

//---------------------------------------------------------------------
void *
v_find_symbol_value(const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.find_symbol_value(raw_name);
}

//---------------------------------------------------------------------
v_type_t *
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
v_export_alias(const char *name, const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_alias(name, raw_name);
}

void
v_add_alias(const char *name, const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_alias(name, raw_name);
}


//---------------------------------------------------------------------
void
v_export_intrinsic(const char *name, void *fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_intrinsic(name, fun, aux);
}

void
v_add_intrinsic(const char *name, void *fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_intrinsic(name, fun, aux);
}

void *
v_get_intrinsic(const char *name, void **aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &intrinsics = lctx.decls.intrinsics;

    if (auto p = intrinsics.find(name))
    {
        if (aux)  *aux = p->second;

        return  p->first;
    }

    return nullptr;
}


//---------------------------------------------------------------------
void
v_add_cleaner(void (*fun)(void *), void *data)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_cleaner(fun, data);
}

void
v_add_local_cleaner(void (*fun)(void *), void *data)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_cleaner(fun, data);
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
    auto ret = new target_local_ctx_t(*gctx);

    ret->filename = filename;

    return ret;
}

void
v_target_destroy_local_ctx(base_local_ctx_t *lctx)
{
    delete lctx;
}

//---------------------------------------------------------------------
bool
v_target_local_ctx_has_parent(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.has_parent();
}


//---------------------------------------------------------------------
#ifdef _WIN32

void ___chkstk_ms(void) {}       //- WTF !?!?!?!?!?!?!?!?!

#endif


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


