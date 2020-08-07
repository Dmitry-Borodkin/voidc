//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"

#include <stdexcept>
#include <regex>
#include <cassert>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/OrcBindings.h>
#include <llvm-c/Support.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_alloca(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &builder =  voidc_global_ctx_t::builder;
    auto &cctx    = *voidc_global_ctx_t::target->current_ctx;

    assert(*args);
    if ((*args)->data.size() < 1  ||  (*args)->data.size() > 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*(*args)->data[0]);

    auto type = cctx.find_type(ident.name.c_str());
    if (!type)
    {
        throw std::runtime_error("Type not found: " + ident.name);
    }

    LLVMValueRef v;

    if ((*args)->data.size() == 1)
    {
        v = LLVMBuildAlloca(builder, type, cctx.ret_name);
    }
    else
    {
        (*args)->data[1]->accept(*vis);         //- Количество...

        v = LLVMBuildArrayAlloca(builder, type, cctx.args[0], cctx.ret_name);

        cctx.args.clear();
    }

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_getelementptr(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &builder =  voidc_global_ctx_t::builder;
    auto &cctx    = *voidc_global_ctx_t::target->current_ctx;

    assert(*args);
    if ((*args)->data.size() < 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(cctx.arg_types.empty());

    (*args)->accept(*vis);

    auto v = LLVMBuildGEP(builder, cctx.args[0], &cctx.args[1], cctx.args.size()-1, cctx.ret_name);

    cctx.args.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_store(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &builder =  voidc_global_ctx_t::builder;
    auto &cctx    = *voidc_global_ctx_t::target->current_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    assert(cctx.arg_types.empty());

    (*args)->data[1]->accept(*vis);         //- Сначала "куда"

    cctx.arg_types.resize(2);

    cctx.arg_types[1] = LLVMGetElementType(LLVMTypeOf(cctx.args[0]));

    (*args)->data[0]->accept(*vis);         //- Теперь "что"

    auto v = LLVMBuildStore(builder, cctx.args[1], cctx.args[0]);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_load(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &builder =  voidc_global_ctx_t::builder;
    auto &cctx    = *voidc_global_ctx_t::target->current_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    (*args)->data[0]->accept(*vis);

    auto v = LLVMBuildLoad(builder, cctx.args[0], cctx.ret_name);

    cctx.args.clear();

    cctx.ret_value = v;
}

//---------------------------------------------------------------------
static
void v_cast(const visitor_ptr_t *vis, const ast_arg_list_ptr_t *args)
{
    auto &builder =  voidc_global_ctx_t::builder;
    auto &cctx    = *voidc_global_ctx_t::target->current_ctx;

    assert(*args);
    if ((*args)->data.size() != 3)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    (*args)->data[0]->accept(*vis);         //- Opcode

    auto opcode = LLVMOpcode(LLVMConstIntGetZExtValue(cctx.args[0]));

    (*args)->data[1]->accept(*vis);         //- Value

    auto &ident = dynamic_cast<const ast_arg_identifier_t &>(*(*args)->data[2]);

    auto type = cctx.find_type(ident.name.c_str());
    if (!type)
    {
        throw std::runtime_error("Type not found: " + ident.name);
    }

    LLVMValueRef v;

    v = LLVMBuildCast(builder, opcode, cctx.args[1], type, cctx.ret_name);

    cctx.args.clear();

    cctx.ret_value = v;
}


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
static
LLVMTypeRef mk_int_type(size_t sz)
{
    switch(sz)
    {
    case  1:    return LLVMInt8Type();
    case  2:    return LLVMInt16Type();
    case  4:    return LLVMInt32Type();
    case  8:    return LLVMInt64Type();
    case 16:    return LLVMInt128Type();

    default:
        throw std::runtime_error("Bad integer size: " + std::to_string(sz));
    }
}

base_global_ctx_t::base_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size)
  : void_type     (LLVMVoidType()),
    bool_type     (LLVMInt1Type()),
    char_type     (LLVMInt8Type()),
    short_type    (LLVMInt16Type()),
    int_type      (mk_int_type(int_size)),
    long_type     (mk_int_type(long_size)),
    long_long_type(LLVMInt64Type()),
    intptr_t_type (mk_int_type(ptr_size)),
    size_t_type   (mk_int_type(ptr_size)),
    char32_t_type (LLVMInt32Type())
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

//---------------------------------------------------------------------
void base_global_ctx_t::initialize(void)
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

#undef DEF
}


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
base_local_ctx_t::base_local_ctx_t(const std::string _filename, base_global_ctx_t &_global)
  : filename(_filename),
    global(_global),
    parent_ctx(_global.current_ctx)
{
    global.current_ctx = this;
}

base_local_ctx_t::~base_local_ctx_t()
{
    global.current_ctx = parent_ctx;
}

//---------------------------------------------------------------------
const std::string
base_local_ctx_t::check_alias(const std::string &name)
{
    try
    {
        try
        {
            return  aliases.at(name);
        }
        catch(std::out_of_range)
        {
            return  global.aliases.at(name);
        }
    }
    catch(std::out_of_range) {}

    return  name;
}

//---------------------------------------------------------------------
bool
base_local_ctx_t::find_function(const std::string &fun_name, LLVMTypeRef &fun_type, LLVMValueRef &fun_value)
{
    fun_type  = nullptr;
    fun_value = nullptr;

    try
    {
        fun_value = vars.at(fun_name);
    }
    catch(std::out_of_range)
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
base_local_ctx_t::find_identifier(const std::string &name)
{
    LLVMValueRef value = nullptr;

    try
    {
        value = vars.at(name);
    }
    catch(std::out_of_range)
    {
        auto raw_name = check_alias(name);

        try
        {
            try
            {
                value = constants.at(raw_name);
            }
            catch(std::out_of_range)
            {
                value = global.constants.at(raw_name);
            }
        }
        catch(std::out_of_range)
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
LLVMBuilderRef       voidc_global_ctx_t::builder;
LLVMPassManagerRef   voidc_global_ctx_t::pass_manager;

//---------------------------------------------------------------------
voidc_global_ctx_t::voidc_global_ctx_t()
  : base_global_ctx_t(sizeof(int), sizeof(long), sizeof(intptr_t)),
    LLVMOpaqueType_type   (LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct.LLVMOpaqueType")),
    LLVMTypeRef_type      (LLVMPointerType(LLVMOpaqueType_type, 0)),
    LLVMOpaqueContext_type(LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct.LLVMOpaqueContext")),
    LLVMContextRef_type   (LLVMPointerType(LLVMOpaqueContext_type, 0))
{
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

#undef DEF
    }
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

    try
    {
        type = symbol_types.at(m_name);
    }
    catch(std::out_of_range) {}

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

    try
    {
        value = LLVMSearchForAddressOfSymbol(m_name);
    }
    catch(std::out_of_range) {}

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

    try
    {
        type  = symbol_types.at(m_name);
        value = LLVMSearchForAddressOfSymbol(m_name);
    }
    catch(std::out_of_range) {}

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

    try
    {
        auto &[t,v] = symbols.at(m_name);

        tt = t;
        tv = v;
    }
    catch(std::out_of_range)
    {
        try
        {
            tt = voidc_global_ctx_t::voidc->symbol_types.at(m_name);
            tv = LLVMSearchForAddressOfSymbol(m_name);
        }
        catch(std::out_of_range) {}
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

    try
    {
        type = symbols.at(m_name).first;
    }
    catch(std::out_of_range)
    {
        try
        {
            type = voidc_global_ctx_t::voidc->symbol_types.at(m_name);
        }
        catch(std::out_of_range) {}
    }

    LLVMOrcDisposeMangledSymbol(m_name);

    return type;
}


//---------------------------------------------------------------------
uint64_t
voidc_local_ctx_t::resolver(const char *m_name, void *)
{
    auto &cctx = *(voidc_local_ctx_t *)voidc_global_ctx->current_ctx;       //- voidc!

    try
    {
        return  (uint64_t)cctx.symbols.at(m_name).second;
    }
    catch(std::out_of_range)
    {
        return  (uint64_t)LLVMSearchForAddressOfSymbol(m_name);
    }
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
  : base_global_ctx_t(int_size, long_size, ptr_size)
{
    initialize();
}


//---------------------------------------------------------------------
void target_global_ctx_t::add_symbol_type(const char *raw_name, LLVMTypeRef type)
{
    if (symbols.count(raw_name) != 0)
    {
        symbols[raw_name].first = type;
    }
    else
    {
        symbols[raw_name] = { type, nullptr };
    }
}

//---------------------------------------------------------------------
void target_global_ctx_t::add_symbol_value(const char *raw_name, void *value)
{
    if (symbols.count(raw_name) != 0)
    {
        symbols[raw_name].second = value;
    }
    else
    {
        symbols[raw_name] = { nullptr, value };
    }
}

//---------------------------------------------------------------------
void target_global_ctx_t::add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    symbols[raw_name] = { type, value };
}


//---------------------------------------------------------------------
LLVMTypeRef
target_global_ctx_t::get_symbol_type(const char *raw_name)
{
    try
    {
        return symbols.at(raw_name).first;
    }
    catch(std::out_of_range) {}

    return nullptr;
}

//---------------------------------------------------------------------
void *
target_global_ctx_t::get_symbol_value(const char *raw_name)
{
    try
    {
        return symbols.at(raw_name).second;
    }
    catch(std::out_of_range) {}

    return nullptr;
}

//---------------------------------------------------------------------
void
target_global_ctx_t::get_symbol(const char *raw_name, LLVMTypeRef &type, void * &value)
{
    type  = nullptr;
    value = nullptr;

    try
    {
        auto &[t,v] = symbols.at(raw_name);

        type  = t;
        value = v;
    }
    catch(std::out_of_range) {}
}


//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
target_local_ctx_t::target_local_ctx_t(const std::string filename, base_global_ctx_t &global)
  : base_local_ctx_t(filename, global)
{
}

//---------------------------------------------------------------------
void target_local_ctx_t::add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    symbols[raw_name] = {type, value};
}

//---------------------------------------------------------------------
LLVMTypeRef target_local_ctx_t::find_type(const char *type_name)
{
    LLVMTypeRef tt = nullptr;

    void *tv = nullptr;

    auto raw_name = check_alias(type_name);

    try
    {
        auto &[t,v] = symbols.at(raw_name);

        tt = t;
        tv = v;
    }
    catch(std::out_of_range)
    {
        voidc_global_ctx_t::target->get_symbol(raw_name.c_str(), tt, tv);
    }

    if (tt != voidc_global_ctx_t::voidc->LLVMOpaqueType_type)
    {
        tv = nullptr;
    }

    return (LLVMTypeRef)tv;
}

//---------------------------------------------------------------------
LLVMTypeRef target_local_ctx_t::find_symbol_type(const char *raw_name)
{
    try
    {
        return symbols.at(raw_name).first;
    }
    catch(std::out_of_range) {}

    return voidc_global_ctx_t::target->get_symbol_type(raw_name);
}


//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
void v_add_symbol_type(const char *raw_name, LLVMTypeRef type)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol_type(raw_name, type);
}

void v_add_symbol_value(const char *raw_name, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol_value(raw_name, value);
}

void v_add_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol(raw_name, type, value);
}

void v_add_constant(const char *name, LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.constants[name] = val;
}

//---------------------------------------------------------------------
void v_add_local_symbol(const char *raw_name, LLVMTypeRef type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    lctx.add_symbol(raw_name, type, value);
}

void v_add_local_constant(const char *name, LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    lctx.constants[name] = val;
}

//---------------------------------------------------------------------
void v_add_variable(const char *name, LLVMValueRef val)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    lctx.vars[name] = val;
}

LLVMValueRef v_get_argument(int num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    return  lctx.args[num];
}

void v_clear_arguments(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    lctx.args.clear();
    lctx.arg_types.clear();
}


//---------------------------------------------------------------------
LLVMTypeRef v_find_symbol_type(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    LLVMTypeRef type = nullptr;

    auto raw_name = lctx.check_alias(name);

    try
    {
        LLVMValueRef value;

        try
        {
            value = lctx.constants.at(raw_name);
        }
        catch(std::out_of_range)
        {
            value = gctx.constants.at(raw_name);
        }

        type = LLVMTypeOf(value);
    }
    catch(std::out_of_range)
    {
        type = lctx.find_symbol_type(raw_name.c_str());
    }

    return type;
}

//---------------------------------------------------------------------
void v_add_alias(const char *name, const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.aliases[name] = raw_name;
}

void v_add_local_alias(const char *name, const char *raw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    lctx.aliases[name] = raw_name;
}


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void voidc_global_ctx_t::static_initialize(void)
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
    target = new voidc_global_ctx_t();      //- Sic!

    //-------------------------------------------------------------
    voidc->add_symbol_value("voidc_resolver", (void *)voidc_local_ctx_t::resolver);

#define DEF(name) \
    voidc->add_symbol_value("voidc_" #name, (void *)name);

    DEF(target_machine)
    DEF(jit)
    DEF(builder)
    DEF(pass_manager)

#undef DEF

    LLVMLoadLibraryPermanently(nullptr);        //- Sic!!!

    voidc->add_symbol_value("stdin",  (void *)stdin);       //- WTF?
    voidc->add_symbol_value("stdout", (void *)stdout);      //- WTF?
    voidc->add_symbol_value("stderr", (void *)stderr);      //- WTF?
}

//---------------------------------------------------------------------
void voidc_global_ctx_t::static_terminate(void)
{
    LLVMDisposePassManager(pass_manager);

    LLVMDisposeBuilder(builder);

    LLVMOrcDisposeInstance(jit);

    delete voidc;

    LLVMShutdown();
}


//=====================================================================
//- AST Visitor - Compiler (level 0) ...
//=====================================================================
static void compile_ast_stmt_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}
static void compile_ast_arg_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_ptr_t *vis, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    auto &builder = voidc_global_ctx_t::builder;

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.current_ctx);

    assert(lctx.args.empty());

    if (!*stmt_list)  return;

    std::string hdr = "unit_" + std::to_string(line) + "_" + std::to_string(column);

    std::string mod_name = hdr + "_module";
    std::string fun_name = hdr + "_action";

    lctx.module = LLVMModuleCreateWithName(mod_name.c_str());

    LLVMSetSourceFileName(lctx.module, lctx.filename.c_str(), lctx.filename.size());

    {   auto dl = LLVMCreateTargetDataLayout(voidc_global_ctx_t::target_machine);
        auto tr = LLVMGetTargetMachineTriple(voidc_global_ctx_t::target_machine);

        LLVMSetModuleDataLayout(lctx.module, dl);
        LLVMSetTarget(lctx.module, tr);

        LLVMDisposeMessage(tr);
        LLVMDisposeTargetData(dl);
    }

    LLVMTypeRef  unit_ft = LLVMFunctionType(LLVMVoidType(), nullptr, 0, false);
    LLVMValueRef unit_f  = LLVMAddFunction(lctx.module, fun_name.c_str(), unit_ft);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(unit_f, "entry");

    LLVMPositionBuilderAtEnd(builder, entry);

    (*stmt_list)->accept(*vis);

    LLVMBuildRetVoid(builder);

    LLVMClearInsertionPosition(builder);

    //-------------------------------------------------------------
    char *msg = nullptr;

    auto err = LLVMVerifyModule(lctx.module, LLVMReturnStatusAction, &msg);
    if (err)
    {
        char *txt = LLVMPrintModuleToString(lctx.module);

        printf("\n%s\n", txt);

        LLVMDisposeMessage(txt);

        printf("\n%s\n", msg);
    }

    LLVMDisposeMessage(msg);

    if (err)  exit(1);          //- Sic !!!

    //-------------------------------------------------------------
    LLVMRunPassManager(gctx.pass_manager, lctx.module);

    //-------------------------------------------------------------
    err = LLVMTargetMachineEmitToMemoryBuffer(gctx.target_machine,
                                              lctx.module,
                                              LLVMObjectFile,
                                              &msg,
                                              &lctx.unit_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(lctx.unit_buffer);


//    {   LLVMMemoryBufferRef asm_buffer = nullptr;
//
//        LLVMTargetMachineEmitToMemoryBuffer(gctx.target_machine,
//                                            lctx.module,
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


    LLVMDisposeModule(lctx.module);

    lctx.vars.clear();
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_ast_stmt_t(const visitor_ptr_t *vis, const std::string *vname, const ast_call_ptr_t *call)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    auto &var_name = *vname;

    lctx.ret_name  = var_name.c_str();
    lctx.ret_value = nullptr;

    (*call)->accept(*vis);

    if (lctx.ret_name[0])   lctx.vars[var_name] = lctx.ret_value;
}


//---------------------------------------------------------------------
//- call
//---------------------------------------------------------------------
static
void compile_ast_call_t(const visitor_ptr_t *vis, const std::string *fname, const ast_arg_list_ptr_t *args)
{
    auto &builder = voidc_global_ctx_t::builder;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    assert(lctx.args.empty());

    auto &fun_name = *fname;

    if (gctx.intrinsics.count(fun_name))
    {
        gctx.intrinsics[fun_name](vis, args);

        return;
    }

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = lctx.find_function(fun_name, ft, f);
    if (!ok)
    {
        throw std::runtime_error("Function not found: " + fun_name);
    }

    lctx.arg_types.resize(LLVMCountParamTypes(ft));

    LLVMGetParamTypes(ft, lctx.arg_types.data());

    if (*args) (*args)->accept(*vis);

    auto v = LLVMBuildCall(builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();

    lctx.ret_value = v;
}


//---------------------------------------------------------------------
//- arg_identifier
//---------------------------------------------------------------------
static
void compile_ast_arg_identifier_t(const visitor_ptr_t *vis, const std::string *name)
{
    auto &builder = voidc_global_ctx_t::builder;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    LLVMValueRef v = lctx.find_identifier(*name);
    if (!v)
    {
        throw std::runtime_error("Identifier not found: " + *name);
    }

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        auto void_ptr_type = LLVMPointerType(gctx.void_type, 0);

        auto at = lctx.arg_types[idx];
        auto vt = LLVMTypeOf(v);

        if (at != vt  &&
            at == void_ptr_type  &&
            LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
        {
            v = LLVMBuildPointerCast(builder, v, void_ptr_type, name->c_str());
        }
    }

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_integer
//---------------------------------------------------------------------
static
void compile_ast_arg_integer_t(const visitor_ptr_t *vis, intptr_t num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    auto idx = lctx.args.size();

    LLVMTypeRef t = gctx.int_type;

    if (idx < lctx.arg_types.size())
    {
        t = lctx.arg_types[idx];
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

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_string
//---------------------------------------------------------------------
static
void compile_ast_arg_string_t(const visitor_ptr_t *vis, const std::string *str)
{
    auto &builder = voidc_global_ctx_t::builder;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    auto v = LLVMBuildGlobalStringPtr(builder, str->c_str(), "str");

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        auto void_ptr_type = LLVMPointerType(gctx.void_type, 0);

        auto at = lctx.arg_types[idx];

        if (at == void_ptr_type)
        {
            v = LLVMBuildPointerCast(builder, v, void_ptr_type, "void_str");
        }
    }

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_char
//---------------------------------------------------------------------
static
void compile_ast_arg_char_t(const visitor_ptr_t *vis, char32_t c)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.current_ctx;

    auto v = LLVMConstInt(gctx.int_type, c, false);      //- ?

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- Compiler visitor
//---------------------------------------------------------------------
static
visitor_ptr_t compile_visitor_level_zero;

visitor_ptr_t
make_compile_visitor(void)
{
    if (!compile_visitor_level_zero)
    {
        voidc_visitor_t vis;

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

    assert(compile_visitor_level_zero);

    return  compile_visitor_level_zero;
}


