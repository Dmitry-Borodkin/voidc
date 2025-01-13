//---------------------------------------------------------------------
//- Copyright (C) 2020-2025 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"

#include <stdexcept>
#include <cassert>

#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Object.h>
#include <llvm-c/Transforms/PassBuilder.h>

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
base_compile_ctx_t::declarations_t::overloads_insert(v_quark_t q, v_type_t *t, v_quark_t r)
{
    if (auto *map = overloads.find(q))
    {
        overloads = overloads.set(q, map->set(t, r));
    }
    else
    {
        overloads = overloads.set(q, immer::map<v_type_t *, v_quark_t>().set(t, r));
    }
}

//---------------------------------------------------------------------
void
base_compile_ctx_t::declarations_t::insert(const declarations_t &other)
{
    for (auto it : other.aliases)     aliases_insert(it);
    for (auto it : other.constants)   constants_insert(it);
    for (auto it : other.symbols)     symbols_insert(it);
    for (auto it : other.intrinsics)  intrinsics_insert(it);
    for (auto it : other.properties)  properties_insert(it);

    for (auto i : other.overloads)
    {
        for (auto j : i.second)
        {
            overloads_insert(i.first, j.first, j.second);
        }
    }
}


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
base_global_ctx_t::base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : voidc_types_ctx_t(ctx, int_size, long_size, ptr_size),
    builder(LLVMCreateBuilderInContext(ctx)),
    char_ptr_type(make_pointer_type(char_type, 0)),         //- address space 0 !?
    void_ptr_type(make_pointer_type(void_type, 0))          //- address space 0 !?
{
    _is_initialized = true;

    voidc_types_make_level_0_intrinsics(*this);

    make_level_0_intrinsics(*this);
}

base_global_ctx_t::~base_global_ctx_t()
{
    LLVMDisposeBuilder(builder);
}


//---------------------------------------------------------------------
static v_quark_t voidc_typenames_q;

//---------------------------------------------------------------------
void
base_global_ctx_t::initialize_type(v_quark_t raw_name, v_type_t *type)
{
    decls.constants_insert({raw_name, static_type_type});

    constant_values.insert({raw_name, reinterpret_cast<LLVMValueRef>(type)});       //- Sic!

    auto &vctx = *voidc_global_ctx_t::voidc;

    if (this == &vctx)
    {
        decls.symbols_insert({raw_name, vctx.type_type});

        add_symbol_value(raw_name, type);

        decls.overloads_insert(voidc_typenames_q, type, raw_name);      //- Sic!
    }
};


//---------------------------------------------------------------------
static int  debug_print_module = 0;
static bool debug_print_assembly = false;

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

    if (err)  abort();          //- Sic !!!

    if (voidc_global_ctx_t::target == voidc_global_ctx_t::voidc  &&  debug_print_assembly)
    {
        LLVMMemoryBufferRef buf = nullptr;

        err = LLVMTargetMachineEmitToMemoryBuffer(voidc_global_ctx_t::target_machine, module, LLVMAssemblyFile, &msg, &buf);

        if (err)
        {
            printf("\n%s\n", msg);

            LLVMDisposeMessage(msg);
        }
        else
        {
            auto asmbuf_ptr = LLVMGetBufferStart(buf);

            printf("\n%s\n", asmbuf_ptr);

            LLVMDisposeMemoryBuffer(buf);
        }

        if (err)  abort();          //- Sic ?!?
    }
}


//---------------------------------------------------------------------
static v_quark_t llvm_stacksave_q;
static v_quark_t llvm_stackrestore_q;


//---------------------------------------------------------------------
void
base_global_ctx_t::initialize(void)
{
    assert(voidc_global_ctx_t::voidc);

    //-----------------------------------------------------------------
    auto q = v_quark_from_string;               //- ?!?!?!?!?!?!?!?

    //-----------------------------------------------------------------
#define DEF(name) \
    initialize_type(q(#name), name##_type);

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

    initialize_type(q("v_static_type_t"), static_type_type);

    //-----------------------------------------------------------------
    auto add_bool_const = [this, q](const char *_raw_name, bool value)
    {
        auto raw_name = q(_raw_name);

        decls.constants_insert({raw_name, bool_type});

        constant_values.insert({raw_name, LLVMConstInt(bool_type->llvm_type(), value, false)});     //- Sic!
    };

    add_bool_const("false", false);
    add_bool_const("true",  true);

    //-----------------------------------------------------------------
    v_type_t *i8_ptr_type = make_pointer_type(make_int_type(8), 0);

    auto stacksave_ft    = make_function_type(i8_ptr_type, nullptr, 0, false);
    auto stackrestore_ft = make_function_type(void_type, &i8_ptr_type, 1, false);

    decls.symbols_insert({llvm_stacksave_q,    stacksave_ft});
    decls.symbols_insert({llvm_stackrestore_q, stackrestore_ft});

    //-----------------------------------------------------------------
    auto add_quark_name = [&](const char *name, v_quark_t quark)
    {
        auto qname = q(name);

        decls.symbols_insert({qname, char_type});

        add_symbol_value(qname, (void *)v_quark_to_string(quark));
    };

    add_quark_name("voidc_llvm_stacksave_name",    llvm_stacksave_q);
    add_quark_name("voidc_llvm_stackrestore_name", llvm_stackrestore_q);
}


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
static v_quark_t base_obtain_alias_default(void *, v_quark_t qname, bool _export);
static v_quark_t base_lookup_alias_default(void *, v_quark_t qname);

#ifdef NEW_ADOPT

static bool base_try_to_adopt_default(void *, v_type_t *t0, LLVMValueRef v0);
static bool base_try_to_convert_default(void *, v_type_t *t0, LLVMValueRef v0, v_type_t *t1, LLVMValueRef *pv1);

#else

static void         base_adopt_result_default(void *, v_type_t *type, LLVMValueRef value);
static LLVMValueRef v_convert_to_type_default(void *, v_type_t *t0, LLVMValueRef v0, v_type_t *t1);

#endif

static LLVMValueRef v_make_temporary_default(void *, v_type_t *t, LLVMValueRef v);

base_local_ctx_t::base_local_ctx_t(base_global_ctx_t &_global)
  : global_ctx(_global),
    parent_ctx(_global.local_ctx)
{
    global_ctx.local_ctx = this;

    decls.insert(global_ctx.decls);

    set_obtain_alias_hook(base_obtain_alias_default, this);
    set_lookup_alias_hook(base_lookup_alias_default, this);

#ifdef NEW_ADOPT

    set_try_to_adopt_hook(base_try_to_adopt_default, this);
    set_try_to_convert_hook(base_try_to_convert_default, this);

#else

    set_adopt_result_hook(base_adopt_result_default, this);
    set_convert_to_type_hook(v_convert_to_type_default, this);

#endif

    set_make_temporary_hook(v_make_temporary_default, this);
}

base_local_ctx_t::~base_local_ctx_t()
{
    global_ctx.local_ctx = parent_ctx;
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_alias(v_quark_t name, v_quark_t raw_name)
{
    if (export_data)  export_data->first.aliases_insert({name, raw_name});

    decls.aliases_insert({name, raw_name});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_alias(v_quark_t name, v_quark_t raw_name)
{
    decls.aliases_insert({name, raw_name});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_constant(v_quark_t name, v_type_t *type, LLVMValueRef value)
{
    assert(&type->context == static_cast<voidc_types_ctx_t *>(&global_ctx));

    auto raw_name = obtain_alias(name, true);

    if (export_data)  export_data->first.constants_insert({raw_name, type});

    decls.constants_insert({raw_name, type});

    if (value)  global_ctx.constant_values.insert_or_assign(raw_name, value);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_constant(v_quark_t name, v_type_t *type, LLVMValueRef value)
{
    assert(&type->context == static_cast<voidc_types_ctx_t *>(&global_ctx));

    auto raw_name = obtain_alias(name, false);

    decls.constants_insert({raw_name, type});

    if (value)  constant_values.insert_or_assign(raw_name, value);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_symbol(v_quark_t name, v_type_t *type, void *value)
{
    assert(&type->context == static_cast<voidc_types_ctx_t *>(&global_ctx));

    auto raw_name = obtain_alias(name, true);

    if (type)
    {
        if (export_data)  export_data->first.symbols_insert({raw_name, type});

        decls.symbols_insert({raw_name, type});
    }

    if (value)  global_ctx.add_symbol_value(raw_name, value);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_symbol(v_quark_t name, v_type_t *type, void *value)
{
    assert(&type->context == static_cast<voidc_types_ctx_t *>(&global_ctx));

    auto raw_name = obtain_alias(name, false);

    if (type)   decls.symbols_insert({raw_name, type});

    if (value)  add_symbol_value(raw_name, value);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_intrinsic(v_quark_t fun_name, void *fun, void *aux)
{
    if (export_data)  export_data->first.intrinsics_insert({fun_name, {fun, aux}});

    decls.intrinsics_insert({fun_name, {fun, aux}});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_intrinsic(v_quark_t fun_name, void *fun, void *aux)
{
    decls.intrinsics_insert({fun_name, {fun, aux}});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_property(v_quark_t name, const std::any &value)
{
    if (export_data)  export_data->first.properties_insert({name, value});

    decls.properties_insert({name, value});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_property(v_quark_t name, const std::any &value)
{
    decls.properties_insert({name, value});
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_overload(v_quark_t name, v_type_t *type, v_quark_t over)
{
    assert(&type->context == static_cast<voidc_types_ctx_t *>(&global_ctx));

    if (export_data)  export_data->first.overloads_insert(name, type, over);

    decls.overloads_insert(name, type, over);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_overload(v_quark_t name, v_type_t *type, v_quark_t over)
{
    assert(&type->context == static_cast<voidc_types_ctx_t *>(&global_ctx));

    decls.overloads_insert(name, type, over);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_type(v_quark_t type_name, v_type_t *type)
{
    auto &gctx = global_ctx;

    assert(&type->context == static_cast<voidc_types_ctx_t *>(&gctx));

    export_constant(type_name, gctx.static_type_type, reinterpret_cast<LLVMValueRef>(type));

    auto &vctx = *voidc_global_ctx_t::voidc;

    if (&gctx == &vctx) export_symbol(type_name, vctx.type_type, type);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_type(v_quark_t type_name, v_type_t *type)
{
    auto &gctx = global_ctx;

    assert(&type->context == static_cast<voidc_types_ctx_t *>(&gctx));

    add_constant(type_name, gctx.static_type_type, reinterpret_cast<LLVMValueRef>(type));

    auto &vctx = *voidc_global_ctx_t::voidc;

    if (&gctx == &vctx) add_symbol(type_name, vctx.type_type, type);
}


//---------------------------------------------------------------------
void
base_local_ctx_t::export_effort(compile_ctx_action_t fun, void *aux)
{
    if (export_data)  export_data->second.push_back({fun, aux});

    fun(aux);       //- Sic!
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_effort(compile_ctx_action_t fun, void *aux)
{
    fun(aux);       //- Sic!
}


//---------------------------------------------------------------------
v_type_t *
base_local_ctx_t::find_type(v_quark_t type_name)
{
    auto &gctx = global_ctx;

    if (auto *p = vars.find(type_name))
    {
        if (p->first == gctx.static_type_type)
        {
            return reinterpret_cast<v_type_t *>(p->second);
        }
    }

    auto raw_name = lookup_alias(type_name);

    {   v_type_t    *t = nullptr;
        LLVMValueRef v = nullptr;

        if (find_constant(raw_name, t, v)  &&  t == gctx.static_type_type)
        {
            return reinterpret_cast<v_type_t *>(v);
        }
    }

    return nullptr;
}


//---------------------------------------------------------------------
bool
base_local_ctx_t::find_constant(v_quark_t raw_name, v_type_t * &type, LLVMValueRef &value)
{
    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto p = decls.constants.find(raw_name))
    {
        t = *p;

        for (auto &cv : {constant_values, global_ctx.constant_values})
        {
            auto itv = cv.find(raw_name);

            if (itv != cv.end())
            {
                v = itv->second;

                break;
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
base_local_ctx_t::find_symbol(v_quark_t raw_name, v_type_t * &type, void * &value)
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
static void *
get_hook(base_local_ctx_t *lctx, v_quark_t quark, void **paux)
{
    if (auto *p = lctx->decls.intrinsics.find(quark))
    {
        if (paux) *paux = p->second;

        return  p->first;
    }

    return nullptr;
}

static void
set_hook(base_local_ctx_t *lctx, v_quark_t quark, void *fun, void *aux)
{
    lctx->decls.intrinsics_insert({quark, {fun, aux}});
}

//---------------------------------------------------------------------
static v_quark_t obtain_alias_q;
static v_quark_t lookup_alias_q;
static v_quark_t obtain_module_q;
static v_quark_t finish_module_q;

#ifdef NEW_ADOPT

static v_quark_t try_to_adopt_q;
static v_quark_t try_to_convert_q;

#else

static v_quark_t adopt_result_q;
static v_quark_t convert_to_type_q;

#endif

static v_quark_t make_temporary_q;

//---------------------------------------------------------------------
obtain_alias_t
base_local_ctx_t::get_obtain_alias_hook(void **paux)
{
    return  obtain_alias_t(get_hook(this, obtain_alias_q, paux));
}

void
base_local_ctx_t::set_obtain_alias_hook(obtain_alias_t fun, void *aux)
{
    set_hook(this, obtain_alias_q, (void *)fun, aux);
}

//---------------------------------------------------------------------
lookup_alias_t
base_local_ctx_t::get_lookup_alias_hook(void **paux)
{
    return  lookup_alias_t(get_hook(this, lookup_alias_q, paux));
}

void
base_local_ctx_t::set_lookup_alias_hook(lookup_alias_t fun, void *aux)
{
    set_hook(this, lookup_alias_q, (void *)fun, aux);
}

//---------------------------------------------------------------------
obtain_module_t
base_local_ctx_t::get_obtain_module_hook(void **paux)
{
    return  obtain_module_t(get_hook(this, obtain_module_q, paux));
}

void
base_local_ctx_t::set_obtain_module_hook(obtain_module_t fun, void *aux)
{
    set_hook(this, obtain_module_q, (void *)fun, aux);
}

//---------------------------------------------------------------------
finish_module_t
base_local_ctx_t::get_finish_module_hook(void **paux)
{
    return  finish_module_t(get_hook(this, finish_module_q, paux));
}

void
base_local_ctx_t::set_finish_module_hook(finish_module_t fun, void *aux)
{
    set_hook(this, finish_module_q, (void *)fun, aux);
}


#ifdef NEW_ADOPT


//---------------------------------------------------------------------
try_to_adopt_t
base_local_ctx_t::get_try_to_adopt_hook(void **paux)
{
    return  try_to_adopt_t(get_hook(this, try_to_adopt_q, paux));
}

void
base_local_ctx_t::set_try_to_adopt_hook(try_to_adopt_t fun, void *aux)
{
    set_hook(this, try_to_adopt_q, (void *)fun, aux);
}

//---------------------------------------------------------------------
try_to_convert_t
base_local_ctx_t::get_try_to_convert_hook(void **paux)
{
    return  try_to_convert_t(get_hook(this, try_to_convert_q, paux));
}

void
base_local_ctx_t::set_try_to_convert_hook(try_to_convert_t fun, void *aux)
{
    set_hook(this, try_to_convert_q, (void *)fun, aux);
}


#else


//---------------------------------------------------------------------
adopt_result_t
base_local_ctx_t::get_adopt_result_hook(void **paux)
{
    return  adopt_result_t(get_hook(this, adopt_result_q, paux));
}

void
base_local_ctx_t::set_adopt_result_hook(adopt_result_t fun, void *aux)
{
    set_hook(this, adopt_result_q, (void *)fun, aux);
}

//---------------------------------------------------------------------
convert_to_type_t
base_local_ctx_t::get_convert_to_type_hook(void **paux)
{
    return  convert_to_type_t(get_hook(this, convert_to_type_q, paux));
}

void
base_local_ctx_t::set_convert_to_type_hook(convert_to_type_t fun, void *aux)
{
    set_hook(this, convert_to_type_q, (void *)fun, aux);
}


#endif


//---------------------------------------------------------------------
make_temporary_t
base_local_ctx_t::get_make_temporary_hook(void **paux)
{
    return  make_temporary_t(get_hook(this, make_temporary_q, paux));
}

void
base_local_ctx_t::set_make_temporary_hook(make_temporary_t fun, void *aux)
{
    set_hook(this, make_temporary_q, (void *)fun, aux);
}


//---------------------------------------------------------------------
bool
base_local_ctx_t::obtain_identifier(v_quark_t name, v_type_t * &type, LLVMValueRef &value)
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
        auto raw_name = lookup_alias(name);

        if (!find_constant(raw_name, t, v)  ||  !v)         //- Sic!
        {
            t = get_symbol_type(raw_name);

            if (!t)  return false;

            if (!(module  ||  (module = obtain_module())))  return false;

            assert(module);

            auto cname = v_quark_to_string(raw_name);

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
void
base_local_ctx_t::push_builder_ip(void)
{
    builder_ip_stack.push_front(unwrap(global_ctx.builder)->saveIP());
}

//---------------------------------------------------------------------
void
base_local_ctx_t::pop_builder_ip(void)
{
    auto &top = builder_ip_stack.front();

    unwrap(global_ctx.builder)->restoreIP(top);

    builder_ip_stack.pop_front();
}


//---------------------------------------------------------------------
static v_quark_t voidc_internal_function_type_q;
static v_quark_t voidc_internal_return_value_q;
static v_quark_t voidc_internal_branch_target_leave_q;

//---------------------------------------------------------------------
LLVMValueRef
base_local_ctx_t::prepare_function(const char *raw_name, v_type_t *type)
{
    LLVMValueRef f = LLVMGetNamedFunction(module, raw_name);        //- Sic!

    if (!f)  f = LLVMAddFunction(module, raw_name, type->llvm_type());

    auto entry = LLVMAppendBasicBlockInContext(global_ctx.llvm_ctx, f, "entry");

    LLVMPositionBuilderAtEnd(global_ctx.builder, entry);

    assert(vars.size() == 0);           //- Sic!

    push_variables();                   //- Sic !!!

    vars = vars.set(voidc_internal_function_type_q, {type, nullptr});

    auto ft_ = type->llvm_type();

    auto rt_ = LLVMGetReturnType(ft_);

    if (LLVMGetTypeKind(rt_) != LLVMVoidTypeKind)
    {
        auto ret_type = static_cast<v_type_function_t *>(type)->return_type();

        auto ret_var_v = LLVMBuildAlloca(global_ctx.builder, ret_type->llvm_type(), "ret_var_v");

        vars = vars.set(voidc_internal_return_value_q, {ret_type, ret_var_v});      //- Sic!
    }

    function_leave_b = LLVMAppendBasicBlockInContext(global_ctx.llvm_ctx, f, "f_leave_b");
    auto f_leave_bv  = LLVMBasicBlockAsValue(function_leave_b);

    vars = vars.set(voidc_internal_branch_target_leave_q, {nullptr, f_leave_bv});   //- Sic!

    return f;
}

//---------------------------------------------------------------------
void
base_local_ctx_t::finish_function(void)
{
    auto cur_b = LLVMGetInsertBlock(global_ctx.builder);

    if (!LLVMGetBasicBlockTerminator(cur_b))
    {
        auto leave_bv = vars[voidc_internal_branch_target_leave_q].second;
        auto leave_b  = LLVMValueAsBasicBlock(leave_bv);

        LLVMBuildBr(global_ctx.builder, leave_b);
    }


    LLVMMoveBasicBlockAfter(function_leave_b, cur_b);

    LLVMPositionBuilderAtEnd(global_ctx.builder, function_leave_b);


    auto type = vars[voidc_internal_function_type_q].first;

    auto ft_ = type->llvm_type();

    auto rt_ = LLVMGetReturnType(ft_);

    if (LLVMGetTypeKind(rt_) == LLVMVoidTypeKind)
    {
        LLVMBuildRetVoid(global_ctx.builder);
    }
    else
    {
        auto [ret_type, ret_var_v] = vars[voidc_internal_return_value_q];

        auto ret_value = LLVMBuildLoad2(global_ctx.builder, ret_type->llvm_type(), ret_var_v, "ret_value");

        LLVMBuildRet(global_ctx.builder, ret_value);
    }

    LLVMClearInsertionPosition(global_ctx.builder);

    pop_variables();            //- Sic !!!

    vars = variables_t();       //- Sic!
}


//---------------------------------------------------------------------
extern "C"
{

static v_quark_t
base_obtain_alias_default(void *void_ctx, v_quark_t name, bool)
{
    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));

    if (auto q = lctx.decls.aliases.find(name))  return *q;

    return  name;
}

static v_quark_t
base_lookup_alias_default(void *void_ctx, v_quark_t name)
{
    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));

    if (auto q = lctx.decls.aliases.find(name))  return *q;

    return  name;
}


#ifdef NEW_ADOPT


static bool
base_try_to_adopt_default(void *void_ctx, v_type_t *type, LLVMValueRef value)
{
    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));
    auto &gctx = lctx.global_ctx;

    switch(intptr_t(lctx.result_type))
    {
    case -1:        //- UNREFERENCE_TAG ...

        if (type->kind() == v_type_t::k_reference)
        {
            auto et = static_cast<v_type_reference_t *>(type)->element_type();

            if (et->kind() == v_type_t::k_array)
            {
                //- Special case for C-like array-to-pointer "promotion"...

                et = static_cast<v_type_array_t *>(et)->element_type();

                auto as = static_cast<v_type_reference_t *>(type)->address_space();

                lctx.result_type = gctx.make_pointer_type(et, as);

                value = lctx.convert_to_type(type, value, lctx.result_type);

                break;
            }

            lctx.result_type = et;

            value = LLVMBuildLoad2(gctx.builder, et->llvm_type(), value, "tmp");

            break;
        }

        if (type->kind() == v_type_t::k_array)
        {
            //- Special case for C-like array-to-pointer "promotion"...

            auto et = static_cast<v_type_array_t *>(type)->element_type();

            lctx.result_type = gctx.make_pointer_type(et, 0);

            value = lctx.convert_to_type(type, value, lctx.result_type);

            break;
        }

        //- Fallthrough!

    case  0:        //- INVIOLABLE_TAG ...

        lctx.result_type = type;

        break;

    default:        //- Adopt...

        if (lctx.result_type == type)   break;

        return  lctx.try_to_convert(type, value, lctx.result_type, &lctx.result_value);
    }

    lctx.result_value = value;

    return true;
}


#else


static void
base_adopt_result_default(void *void_ctx, v_type_t *type, LLVMValueRef value)
{
    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));
    auto &gctx = lctx.global_ctx;

    bool src_ref = (type->kind() == v_type_t::k_reference);

    switch(intptr_t(lctx.result_type))
    {
    case -1:        //- Unreference...

        if (src_ref)
        {
            auto et = static_cast<v_type_reference_t *>(type)->element_type();

            if (et->kind() == v_type_t::k_array)
            {
                //- Special case for C-like array-to-pointer "promotion"...

                et = static_cast<v_type_array_t *>(et)->element_type();

                auto as = static_cast<v_type_reference_t *>(type)->address_space();

                lctx.result_type = gctx.make_pointer_type(et, as);

                value = lctx.convert_to_type(type, value, lctx.result_type);

                break;
            }

            lctx.result_type = et;

            value = LLVMBuildLoad2(gctx.builder, et->llvm_type(), value, "tmp");

            break;
        }

        if (type->kind() == v_type_t::k_array)
        {
            //- Special case for C-like array-to-pointer "promotion"...

            auto et = static_cast<v_type_array_t *>(type)->element_type();

            lctx.result_type = gctx.make_pointer_type(et, 0);

            value = lctx.convert_to_type(type, value, lctx.result_type);

            break;
        }

        //- Fallthrough!

    case  0:        //- Get "as is"...

        lctx.result_type = type;

        break;

    default:        //- Adopt...

        if (lctx.result_type == type)   break;

        bool dst_ref = (lctx.result_type->kind() == v_type_t::k_reference);

        auto dst_typ = lctx.result_type;

        if (dst_ref)  dst_typ = static_cast<v_type_reference_t *>(dst_typ)->element_type();

        auto src_typ = type;

        if (src_ref)
        {
            src_typ = static_cast<v_type_reference_t *>(src_typ)->element_type();

            if (src_typ->kind() == v_type_t::k_array  &&
                dst_typ->kind() == v_type_t::k_pointer)
            {
                //- Special case for C-like array-to-pointer "promotion"...

                auto et = static_cast<v_type_array_t *>(src_typ)->element_type();

                auto as = static_cast<v_type_pointer_t *>(dst_typ)->address_space();

                src_typ = gctx.make_pointer_type(et, as);

                value = lctx.convert_to_type(type, value, src_typ);
            }
            else
            {
                value = LLVMBuildLoad2(gctx.builder, src_typ->llvm_type(), value, "tmp");
            }
        }

        if (src_typ != dst_typ)
        {
            if (dst_typ == gctx.void_ptr_type  &&
                src_typ->kind() == v_type_t::k_pointer)
            {
                value = LLVMBuildPointerCast(gctx.builder, value, dst_typ->llvm_type(), "");
            }
            else
            {
                value = lctx.convert_to_type(src_typ, value, dst_typ);      //- "generic" ...
            }
        }

        if (dst_ref)  value = lctx.make_temporary(dst_typ, value);
    }

    lctx.result_value = value;
}


#endif


}

//---------------------------------------------------------------------
void
base_local_ctx_t::push_variables(void)
{
    vars_stack.push_front({compiler, decls, std::move(cleaners), vars});

    cleaners.clear();
}

//---------------------------------------------------------------------
static
int hold_state = 0;         //- Dirty hack!!!

void
base_local_ctx_t::pop_variables(void)
{
    if (hold_state <= 0)
    {
        run_cleaners();

        std::tie(compiler, decls, cleaners, vars) = std::move(vars_stack.front());
    }
    else
    {
        auto saved_cleaners = std::move(cleaners);

        std::tie(std::ignore, std::ignore, cleaners, vars) = std::move(vars_stack.front());

        saved_cleaners.reverse();

        for (auto it : saved_cleaners)
        {
            cleaners.push_front(it);
        }

        hold_state -= 1;
    }

    vars_stack.pop_front();
}


#ifdef NEW_ADOPT


//---------------------------------------------------------------------
void
base_local_ctx_t::adopt_result(v_type_t *type, LLVMValueRef value)
{
    if (try_to_adopt(type, value))  return;

    throw std::runtime_error("Impossible to adopt!");
}


//---------------------------------------------------------------------
LLVMValueRef
base_local_ctx_t::convert_to_type(v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
{
    LLVMValueRef v1;

    if (try_to_convert(t0, v0, t1, &v1))  return v1;

    throw std::runtime_error("Impossible to convert!");
}


#endif


//---------------------------------------------------------------------
extern "C"
{

static void
temporaries_stack_front_cleaner(void *data)
{
    auto &lctx = *reinterpret_cast<base_local_ctx_t *>(data);

    auto &stack_front = lctx.temporaries_stack.front();

    assert(stack_front.first != nullptr);

    v_type_t    *t;
    LLVMValueRef f;

    lctx.obtain_identifier(llvm_stackrestore_q, t, f);

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    LLVMBuildCall2(lctx.global_ctx.builder, t->llvm_type(), f, &stack_front.first, 1, "");
}

}

//---------------------------------------------------------------------
static void
initialize_temporaries_stack_front(base_local_ctx_t &lctx)
{
    auto &stack_front = lctx.temporaries_stack.front();

    assert(stack_front.first == nullptr);

    v_type_t    *t;
    LLVMValueRef f;

    lctx.obtain_identifier(llvm_stacksave_q, t, f);

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    auto stack_ptr = LLVMBuildCall2(lctx.global_ctx.builder, t->llvm_type(), f, nullptr, 0, "tmp_stack_ptr");

    stack_front.first = stack_ptr;

    lctx.add_temporary_cleaner(temporaries_stack_front_cleaner, &lctx);
}

//---------------------------------------------------------------------
void
base_local_ctx_t::add_temporary_cleaner(temporary_cleaner_t fun, void *data)
{
    temporaries_stack.front().second.push_front({fun, data});
}

//---------------------------------------------------------------------
void
base_local_ctx_t::push_temporaries(void)
{
    temporaries_stack.push_front({nullptr, {}});        //- Sic!
}

//---------------------------------------------------------------------
void
base_local_ctx_t::pop_temporaries(void)
{
    {   auto cur_b = LLVMGetInsertBlock(global_ctx.builder);

        if (auto trm_v = LLVMGetBasicBlockTerminator(cur_b))
        {
            LLVMPositionBuilderBefore(global_ctx.builder, trm_v);
        }
    }

    {   auto &cleaners = temporaries_stack.front().second;

        for (auto &it: cleaners) it.first(it.second);

        cleaners.clear();       //- Sic!
    }

    temporaries_stack.pop_front();
}

//---------------------------------------------------------------------
extern "C"
{


#ifdef NEW_ADOPT


//---------------------------------------------------------------------
static bool
base_try_to_convert_default(void *void_ctx, v_type_t *t0, LLVMValueRef v0, v_type_t *t1, LLVMValueRef *pv1)
{
    if (t0 == t1)
    {
        //- No conversion...

        *pv1 = v0;

        return true;
    }

    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));
    auto &gctx = lctx.global_ctx;

    //- Primary scalar conversions

    {   LLVMOpcode opcode = LLVMOpcode(0);

        if (v_type_is_floating_point(t0)  &&  v_type_is_floating_point(t1))
        {
            auto w0 = v_type_floating_point_get_width(t0);
            auto w1 = v_type_floating_point_get_width(t1);

            if (w0 < w1)  return false;

            //- Floating point -> floating point

            opcode = LLVMFPExt;
        }
        else if (v_type_is_integer(t0))
        {
            bool t0_signed = v_type_integer_is_signed(t0);

            if (v_type_is_floating_point(t1))
            {
                //- Integer -> floating point

                if (t0_signed)  opcode = LLVMSIToFP;
                else            opcode = LLVMUIToFP;
            }
            else if (v_type_is_integer(t1))
            {
                auto w0 = v_type_integer_get_width(t0);
                auto w1 = v_type_integer_get_width(t1);

                if (w0 < w1)  return false;

                //- Integer -> integer

                if (t0_signed)  opcode = LLVMSExt;
                else            opcode = LLVMZExt;
            }
        }

        if (opcode)
        {
            *pv1 = LLVMBuildCast(gctx.builder, opcode, v0, t1->llvm_type(), "");

            return true;
        }
    }

    auto t0_k = t0->kind();
    auto t1_k = t1->kind();

    if (t0_k == v_type_t::k_pointer  &&  t1_k == v_type_t::k_pointer)
    {
        if (static_cast<v_type_refptr_t *>(t1)->element_type() != gctx.void_type)
        {
            return false;
        }

        //- *T -> *void

        *pv1 = LLVMBuildPointerCast(gctx.builder, v0, t1->llvm_type(), "");

        return true;
    }

    //- Primary(?) reference conversions

    bool t0_reference = (t0_k == v_type_t::k_reference);
    bool t1_reference = (t1_k == v_type_t::k_reference);

    v_type_t *et0 = (t0_reference  ?  static_cast<v_type_refptr_t *>(t0)->element_type()  :  nullptr);
    v_type_t *et1 = (t1_reference  ?  static_cast<v_type_refptr_t *>(t1)->element_type()  :  nullptr);

    bool t0_ref_array = (t0_reference  &&  et0->kind() == v_type_t::k_array);

    if (t0_reference)
    {
        if (t0_ref_array)
        {
            if (t1_k == v_type_t::k_pointer)
            {
                auto e0 = static_cast<v_type_array_t *>(et0)->element_type();

                auto e1 = static_cast<v_type_refptr_t *>(t1)->element_type();

                if (e0 == e1  ||  e1 == gctx.void_type)         //- Sic!
                {
                    //- *T[_] -> *T,  or  *T[_] ->> *void

                    *pv1 = LLVMBuildPointerCast(gctx.builder, v0, t1->llvm_type(), "");

                    return true;
                }
            }
        }
        else if (et0 == t1)
        {
            //- &T -> T

            *pv1 = LLVMBuildLoad2(gctx.builder, t0->llvm_type(), v0, "");

            return true;
        }
    }
    else if (t1_reference)
    {
        if (t0 == et1)
        {
            //- T -> &T






        }

    }












    switch(t0_k)
    {
    case v_type_t::k_f16:
    case v_type_t::k_f32:
    case v_type_t::k_f64:
    case v_type_t::k_f128:
      {




      }

    case v_type_t::k_int:
    case v_type_t::k_uint:
      {
      }

    case v_type_t::k_pointer:
      {


      }

    case v_type_t::k_reference:
      {


      }

    case v_type_t::k_array:
      {


      }





    }



    return false;
}


#else


//---------------------------------------------------------------------
static LLVMValueRef
v_convert_to_type_default(void *void_ctx, v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
{
    if (t0 == t1)   return v0;      //- No conversion...

    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));
    auto &gctx = lctx.global_ctx;

    if (t1->kind() == v_type_t::k_pointer)
    {
        LLVMValueRef v1 = 0;

        if (auto tr = dynamic_cast<v_type_reference_t *>(t0))
        {
            if (auto ta = dynamic_cast<v_type_array_t *>(tr->element_type()))
            {
                //- C-like array-to-pointer "promotion" (from reference)...

                v1 = v0;
            }
        }
        else if (t0->kind() == v_type_t::k_array)
        {
            //- C-like array-to-pointer "promotion" (from value)...

            if (LLVMIsConstant(v0))
            {
                v1 = LLVMAddGlobal(lctx.module, t0->llvm_type(), "arr");

                LLVMSetInitializer(v1, v0);

                LLVMSetLinkage(v1, LLVMPrivateLinkage);

                LLVMSetUnnamedAddress(v1, LLVMGlobalUnnamedAddr);

                LLVMSetGlobalConstant(v1, true);
            }
            else
            {
                v1 = lctx.make_temporary(t0, v0);
            }
        }

        if (v1)   return  LLVMBuildPointerCast(gctx.builder, v1, t1->llvm_type(), "");
    }

    //- Just simple scalar "promotions" ...
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
            if (auto t = dynamic_cast<v_type_integer_t *>(t0))
            {
                if (t->is_signed()) opcode = LLVMSIToFP;
                else                opcode = LLVMUIToFP;
            }
        }
    }
    else
    {
        if (auto t = dynamic_cast<v_type_integer_t *>(t0))
        {
            if (t->is_signed()) opcode = LLVMSExt;
            else                opcode = LLVMZExt;
        }
    }

    if (!opcode)  throw std::runtime_error("Impossible to convert!");

    return  LLVMBuildCast(gctx.builder, opcode, v0, t1->llvm_type(), "");
}


#endif


//---------------------------------------------------------------------
static LLVMValueRef
v_make_temporary_default(void *void_ctx, v_type_t *type, LLVMValueRef value)
{
    auto &lctx = *(reinterpret_cast<base_local_ctx_t *>(void_ctx));
    auto &gctx = lctx.global_ctx;

    if (!lctx.temporaries_stack.front().first)
    {
        initialize_temporaries_stack_front(lctx);
    }

    auto tmp = LLVMBuildAlloca(gctx.builder, type->llvm_type(), "tmp");

    if (value)  LLVMBuildStore(gctx.builder, value, tmp);           //- Sic!

    return  tmp;
}

}


//---------------------------------------------------------------------
//- Voidc template Context
//---------------------------------------------------------------------
template<typename T, typename... TArgs>
voidc_template_ctx_t<T, TArgs...>::~voidc_template_ctx_t()
{
    auto &es = unwrap(voidc_global_ctx_t::jit)->getExecutionSession();

    for (auto jd : deque_jd)
    {
//      auto e0 = unwrap(voidc_global_ctx_t::jit)->deinitialize(*unwrap(jd));

        auto e1 = es.removeJITDylib(*unwrap(jd));
    }
}

//---------------------------------------------------------------------
template<typename T, typename... TArgs>
void
voidc_template_ctx_t<T, TArgs...>::add_symbol_value(v_quark_t raw_name_q, void *value)
{
    auto raw_name = v_quark_to_string(raw_name_q);

    auto vjit = unwrap(voidc_global_ctx_t::voidc->jit);

    if (vjit->lookup(*unwrap(base_jd), raw_name)) return;

    unit_symbols[vjit->mangleAndIntern(raw_name)] = { ExecutorAddr::fromPtr(value), JITSymbolFlags::Exported };
}

//---------------------------------------------------------------------
template<typename T, typename... Targs>
static inline
void
flush_unit_symbols_helper(voidc_template_ctx_t<T, Targs...> *ctx)
{
    if (ctx->unit_symbols.empty()) return;

    auto err = unwrap(ctx->base_jd)->define(absoluteSymbols(ctx->unit_symbols));

    ctx->unit_symbols.clear();
}

template<>
void
voidc_global_template_ctx_t::flush_unit_symbols(void)
{
    flush_unit_symbols_helper(this);
}

template<>
void
voidc_local_template_ctx_t::flush_unit_symbols(void)
{
    flush_unit_symbols_helper(this);

    flush_unit_symbols_helper(voidc_global_ctx_t::voidc);
}


//---------------------------------------------------------------------
template<>
void
voidc_global_template_ctx_t::setup_link_order(LLVMOrcJITDylibRef jd)
{
#ifdef _WIN32
    const auto flags = JITDylibLookupFlags::MatchAllSymbols;
#else
    const auto flags = JITDylibLookupFlags::MatchExportedSymbolsOnly;
#endif

    size_t sz = deque_jd.size() + 2;

    if (local_ctx)  sz += 1;

    JITDylibSearchOrder so(sz);

    auto it = so.begin();

    auto set_item = [&it](LLVMOrcJITDylibRef _jd, JITDylibLookupFlags _flags = flags)
    {
        *it++ = {unwrap(_jd), _flags};
    };

    set_item(jd);

    if (local_ctx)  set_item(static_cast<voidc_local_ctx_t *>(local_ctx)->base_jd);

    for (auto _jd : deque_jd)   set_item(_jd);

    set_item(base_jd, JITDylibLookupFlags::MatchAllSymbols);

    unwrap(jd)->setLinkOrder(so, false);        //- Set "as is"
}

//---------------------------------------------------------------------
template<>
void
voidc_local_template_ctx_t::setup_link_order(LLVMOrcJITDylibRef jd)
{
#ifdef _WIN32
    const auto flags = JITDylibLookupFlags::MatchAllSymbols;
#else
    const auto flags = JITDylibLookupFlags::MatchExportedSymbolsOnly;
#endif

    auto &vctx = *voidc_global_ctx_t::voidc;

    size_t sz = 2 + deque_jd.size() + vctx.deque_jd.size();

    JITDylibSearchOrder so(sz);

    auto it = so.begin();

    auto set_item = [&it](LLVMOrcJITDylibRef _jd, JITDylibLookupFlags _flags = flags)
    {
        *it++ = {unwrap(_jd), _flags};
    };

    set_item(jd);

    for (auto _jd : deque_jd)   set_item(_jd);

    for (auto _jd : vctx.deque_jd)  set_item(_jd);

    set_item(vctx.base_jd, JITDylibLookupFlags::MatchAllSymbols);

    unwrap(jd)->setLinkOrder(so, false);        //- Set "as is"
}


//---------------------------------------------------------------------
struct search_request_t
{
    const char *prefix;
    unsigned    length;

    LLVMOrcJITTargetAddress addr;
};

//---------------------------------------------------------------------
static void
add_object_file_to_jd_with_rt(LLVMMemoryBufferRef membuf,
                              LLVMOrcJITDylibRef jd,
                              LLVMOrcResourceTrackerRef rt,
                              search_request_t *req)
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

    while(!LLVMObjectFileIsSymbolIteratorAtEnd(bref, it))
    {
        auto sname = LLVMGetSymbolName(it);

        if (auto sym = unwrap(jit)->lookup(*unwrap(jd), sname))
        {
            auto addr = sym->getValue();

            if (!req) continue;

            for (int i=0; req[i].prefix; ++i)
            {
                if (std::strncmp(sname, req[i].prefix, req[i].length) == 0)
                {
                    req[i].addr = addr;
                }
            }
        }

        LLVMMoveToNextSymbol(it);
    }

    LLVMDisposeSymbolIterator(it);

    //-------------------------------------------------------------
    LLVMDisposeBinary(bref);

//  unwrap(jd)->dump(outs());
}

//---------------------------------------------------------------------
static void
add_object_file_to_jd(LLVMMemoryBufferRef membuf,
                      LLVMOrcJITDylibRef jd,
                      search_request_t *req)
{
    add_object_file_to_jd_with_rt(membuf, jd, LLVMOrcJITDylibGetDefaultResourceTracker(jd), req);
}

//---------------------------------------------------------------------
template<typename T, typename... TArgs>
void
voidc_template_ctx_t<T, TArgs...>::add_object_file_to_jit(LLVMMemoryBufferRef membuf)
{
//  assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic !?!

    auto es = LLVMOrcLLJITGetExecutionSession(voidc_global_ctx_t::jit);

    auto &vctx = *voidc_global_ctx_t::voidc;

    std::string jd_name("voidc_jd_" + std::to_string(vctx.jd_hash));

    vctx.jd_hash += 1;

    LLVMOrcJITDylibRef jd = nullptr;

    LLVMOrcExecutionSessionCreateJITDylib(es, &jd, jd_name.c_str());

    assert(jd);

    setup_link_order(jd);

    search_request_t req[] =
    {
        { "voidc.init_func.", 16, 0 },
        { "voidc.term_func.", 16, 0 },

        { 0, 0, 0 }
    };

    add_object_file_to_jd(membuf, jd, req);

    if (auto addr = req[0].addr)
    {
        void (*init_fun)() = (void (*)())addr;

        init_fun();
    }

    unwrap(jd)->setLinkOrder({{unwrap(vctx.base_jd), JITDylibLookupFlags::MatchAllSymbols}});       //- Sic!    WTF?

    deque_jd.push_front(jd);

    if (auto addr = req[1].addr)
    {
        auto cleaner = [](void *data)
        {
            void (*term_fun)() = (void (*)())data;

            term_fun();
        };

        this->add_cleaner(cleaner, (void *)addr);
    }
}


//---------------------------------------------------------------------
template<typename T, typename... TArgs>
void
voidc_template_ctx_t<T, TArgs...>::add_module_to_jit(LLVMModuleRef mod)
{
//  assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic !?!

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

        abort();                //- Sic !!!
    }

    assert(mod_buffer);

    add_object_file_to_jit(mod_buffer);

    LLVMDisposeMemoryBuffer(mod_buffer);
}


//---------------------------------------------------------------------
template class voidc_template_ctx_t<base_global_ctx_t, LLVMContextRef, size_t, size_t, size_t>;
template class voidc_template_ctx_t<base_local_ctx_t, base_global_ctx_t &>;


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
static
voidc_global_ctx_t *voidc_global_ctx = nullptr;

VOIDC_DLLEXPORT_BEGIN_VARIABLE

voidc_global_ctx_t * const & voidc_global_ctx_t::voidc = voidc_global_ctx;
base_global_ctx_t  *         voidc_global_ctx_t::target;

LLVMOrcLLJITRef voidc_global_ctx_t::jit;

LLVMTargetMachineRef voidc_global_ctx_t::target_machine;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
voidc_global_ctx_t::voidc_global_ctx_t()
  : voidc_template_ctx_t(LLVMGetGlobalContext(), sizeof(int), sizeof(long), sizeof(intptr_t)),
    type_type(make_struct_type(v_quark_from_string("v_type_t"))),
    type_ptr_type(make_pointer_type(type_type, 0))
{
    assert(voidc_global_ctx == nullptr);

    voidc_global_ctx = this;

    //-------------------------------------------------------------
    base_jd = LLVMOrcLLJITGetMainJITDylib(jit);

//  deque_jd.push_front(base_jd);

    {   LLVMOrcDefinitionGeneratorRef gen = nullptr;

        LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(&gen, LLVMOrcLLJITGetGlobalPrefix(jit), nullptr, nullptr);

        LLVMOrcJITDylibAddGenerator(base_jd, gen);
    }

    //-------------------------------------------------------------
    auto q = v_quark_from_string;

    initialize_type(q("v_type_t"),   type_type);
    initialize_type(q("v_type_ptr"), type_ptr_type);

    initialize_type(q("v_quark_t"), make_uint_type(32));            //- Sic!

    {   v_type_t *types[] =
        {
            type_ptr_type,
            make_pointer_type(type_ptr_type, 0),
            unsigned_type,
            bool_type
        };

#define DEF(name, ret, num) \
        decls.symbols_insert({q(#name), make_function_type(ret, types, num, false)});

        DEF(v_function_type, type_ptr_type, 4)

        types[0] = char_ptr_type;
        types[1] = type_ptr_type;
        types[2] = make_pointer_type(void_type, 0);

        DEF(v_add_symbol, void_type, 3)

#undef DEF
    }

#ifdef _WIN32

    decls.constants_insert({q("_WIN32"), void_type});           //- Sic!!!

#endif

#ifdef NDEBUG

    decls.constants_insert({q("NDEBUG"), void_type});           //- Sic!!!

#endif
}


//---------------------------------------------------------------------
static char *voidc_triple = nullptr;

static v_quark_t voidc_object_file_load_to_jit_internal_helper_q;

//---------------------------------------------------------------------
void
voidc_global_ctx_t::static_initialize(void)
{
    auto q = v_quark_from_string;

    voidc_typenames_q = q("voidc.typenames_dict");

    obtain_alias_q  = q("voidc.hook_obtain_alias");
    lookup_alias_q  = q("voidc.hook_lookup_alias");
    obtain_module_q = q("voidc.hook_obtain_module");
    finish_module_q = q("voidc.hook_finish_module");

#ifdef NEW_ADOPT

    try_to_adopt_q   = q("voidc.hook_try_to_adopt");
    try_to_convert_q = q("voidc.hook_try_to_convert");

#else

    adopt_result_q    = q("voidc.hook_adopt_result");
    convert_to_type_q = q("voidc.hook_convert_to_type");

#endif

    make_temporary_q = q("voidc.hook_make_temporary");

#if LLVM_VERSION_MAJOR < 18
    llvm_stacksave_q                     = q("llvm.stacksave");
    llvm_stackrestore_q                  = q("llvm.stackrestore");
#else
    llvm_stacksave_q                     = q("llvm.stacksave.p0");
    llvm_stackrestore_q                  = q("llvm.stackrestore.p0");
#endif

    voidc_internal_function_type_q       = q("voidc.internal_function_type");
    voidc_internal_return_value_q        = q("voidc.internal_return_value");
    voidc_internal_branch_target_leave_q = q("voidc.internal_branch_target_leave");

    //-------------------------------------------------------------
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();

    //-------------------------------------------------------------
    LLVMOrcCreateLLJIT(&jit, 0);            //- Sic!

    //-------------------------------------------------------------
    voidc_types_static_initialize();        //- Sic!

    target = new voidc_global_ctx_t();      //- Sic!

    voidc_types_make_voidc_constants();     //- Sic!

    static_cast<voidc_global_ctx_t *>(target)->initialize();    //- Sic!

    //-------------------------------------------------------------
    {   const char *triple = LLVMOrcLLJITGetTripleString(jit);

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
                LLVMCodeGenLevelDefault,

#ifdef _WIN32                                       //- WTF !?!
                LLVMRelocDefault,                   //- WTF !?!
#else                                               //- WTF !?!
                LLVMRelocPIC,                       //- WTF !?!
#endif                                              //- WTF !?!

                LLVMCodeModelJITDefault
            );

        LLVMDisposeMessage(cpu_features);
        LLVMDisposeMessage(cpu_name);
    }

    voidc->data_layout = LLVMCreateTargetDataLayout(target_machine);
    voidc_triple       = LLVMGetTargetMachineTriple(target_machine);

    //-------------------------------------------------------------
#define DEF(name) \
    voidc->add_symbol_value(q("voidc_" #name), (void *)name);

    DEF(target_machine)

#undef DEF

    //-------------------------------------------------------------
    voidc_object_file_load_to_jit_internal_helper_q = q("voidc_object_file_load_to_jit_internal_helper");

    {   v_type_t *typ[3];

        typ[0] = voidc->char_ptr_type;
        typ[1] = voidc->size_t_type;
        typ[2] = voidc->bool_type;

        auto ft = voidc->make_function_type(voidc->void_type, typ, 3, false);

        voidc->decls.symbols_insert({voidc_object_file_load_to_jit_internal_helper_q, ft});
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

    LLVMDisposeMessage(voidc_triple);
    LLVMDisposeTargetData(voidc->data_layout);

    delete voidc;

    LLVMOrcDisposeLLJIT(jit);

    LLVMShutdown();
}


//---------------------------------------------------------------------
static bool verify_jit_module_optimized = false;

void
voidc_global_ctx_t::prepare_module_for_jit(LLVMModuleRef module)
{
    assert(target == voidc);    //- Sic!

    verify_module(module);

    //-------------------------------------------------------------
    LLVMSetModuleDataLayout(module, voidc->data_layout);
    LLVMSetTarget(module, voidc_triple);

    //-------------------------------------------------------------
    auto opts = LLVMCreatePassBuilderOptions();

    auto err = LLVMRunPasses(module, "default<O3>", target_machine, opts);

    if (err)
    {
        auto msg = LLVMGetErrorMessage(err);

        printf("LLVMRunPasses: %s\n", msg);

        LLVMDisposeErrorMessage(msg);
    }

    LLVMDisposePassBuilderOptions(opts);

    //-------------------------------------------------------------
    if (verify_jit_module_optimized)  verify_module(module);
}


//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
#ifndef NEW_ADOPT

static void voidc_adopt_result_default(void *, v_type_t *type, LLVMValueRef value);

#endif

voidc_local_ctx_t::voidc_local_ctx_t(voidc_global_ctx_t &global)
  : voidc_template_ctx_t(global)
{
    compiler = make_level_0_voidc_compiler();

#ifndef NEW_ADOPT

    set_adopt_result_hook(voidc_adopt_result_default, this);

#endif

    auto es = LLVMOrcLLJITGetExecutionSession(voidc_global_ctx_t::jit);

    std::string jd_name("voidc_jd_" + std::to_string(global.jd_hash));

    global.jd_hash += 1;

    LLVMOrcExecutionSessionCreateJITDylib(es, &base_jd, jd_name.c_str());

    assert(base_jd);

    setup_link_order(base_jd);

    deque_jd.push_front(base_jd);
}

//---------------------------------------------------------------------
voidc_local_ctx_t::~voidc_local_ctx_t()
{
    run_cleaners();
}


//---------------------------------------------------------------------
void
voidc_local_ctx_t::export_type(v_quark_t type_name, v_type_t *type)
{
    base_local_ctx_t::export_type(type_name, type);

    auto raw_name = lookup_alias(type_name);

    export_overload(voidc_typenames_q, type, raw_name);          //- Sic!
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::add_type(v_quark_t type_name, v_type_t *type)
{
    base_local_ctx_t::add_type(type_name, type);

    auto raw_name = lookup_alias(type_name);

    add_overload(voidc_typenames_q, type, raw_name);            //- Sic!
}


//---------------------------------------------------------------------
void *
voidc_local_ctx_t::find_symbol_value(v_quark_t raw_name_q)
{
    auto raw_name = v_quark_to_string(raw_name_q);

    auto &vctx = *voidc_global_ctx_t::voidc;

    std::deque<LLVMOrcJITDylibRef> main_dq = {vctx.base_jd};

    for (auto dq : {deque_jd, vctx.deque_jd, main_dq})
    {
        for (auto jd : dq)
        {
            auto sym = unwrap(voidc_global_ctx_t::jit)->lookup(*unwrap(jd), raw_name);

            if (sym)  return (void *)sym->getValue();
        }
    }

    return nullptr;
}


//---------------------------------------------------------------------
extern "C"
{


#ifdef NEW_ADOPT


static bool
voidc_try_to_adopt_default(void *void_ctx, v_type_t *type, LLVMValueRef value)
{
    return false;
}


#else


static void
voidc_adopt_result_default(void *void_ctx, v_type_t *type, LLVMValueRef value)
{
    auto &vctx = *voidc_global_ctx_t::voidc;
    auto &lctx = *(reinterpret_cast<voidc_local_ctx_t *>(void_ctx));

    if (lctx.result_type == UNREFERENCE_TAG  &&  type == vctx.static_type_type)
    {
        const v_quark_t *pq = nullptr;

        if (auto *i = lctx.decls.overloads.find(voidc_typenames_q))         //- WTF ?!?!?!?!?!?!?
        {
            pq = i->find(reinterpret_cast<v_type_t *>(value));
        }

        if (pq)
        {
            auto q = *pq;

            auto t = lctx.get_symbol_type(q);

            assert(t == vctx.type_type);

            auto cname = v_quark_to_string(q);

            auto v = LLVMGetNamedGlobal(lctx.module, cname);

            if (!v) v = LLVMAddGlobal(lctx.module, t->llvm_type(), cname);

            lctx.result_type  = vctx.type_ptr_type;
            lctx.result_value = v;

            return;
        }
    }

    base_adopt_result_default(void_ctx, type, value);           //- Sic!
}


#endif


}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::prepare_unit_action(int line, int column)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);    //- Sic!

    std::string tail = std::to_string(line) + "_" + std::to_string(column);

    std::string mod_name = "voidc.unit_module_" + tail;
    std::string fun_name = "voidc.unit_action_" + tail;

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

    finish_module(module);

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

        abort();                //- Sic !!!
    }

    assert(unit_buffer);

    //-------------------------------------------------------------
    LLVMDisposeModule(module);

    vars = variables_t();       //- ?
}

//---------------------------------------------------------------------
void
voidc_local_ctx_t::run_unit_action(void)
{
    if (!unit_buffer) return;

    auto es = LLVMOrcLLJITGetExecutionSession(voidc_global_ctx_t::jit);

    auto &gctx = *voidc_global_ctx_t::voidc;

    std::string jd_name("voidc_unit_jd_" + std::to_string(gctx.jd_hash));

    gctx.jd_hash += 1;

    LLVMOrcJITDylibRef jd = nullptr;

    LLVMOrcExecutionSessionCreateJITDylib(es, &jd, jd_name.c_str());

    assert(jd);

    setup_link_order(jd);

    auto rt = LLVMOrcJITDylibCreateResourceTracker(jd);

    search_request_t req[] =
    {
        { "voidc.init_func.", 16, 0 },
        { "voidc.term_func.", 16, 0 },

        { "voidc.unit_action_", 18, 0 },

        { 0, 0, 0 }
    };

    add_object_file_to_jd_with_rt(unit_buffer, jd, rt, req);

    if (auto addr = req[0].addr)
    {
        void (*init_fun)() = (void (*)())addr;

        init_fun();
    }

    void (*unit_action)() = (void (*)())req[2].addr;

    unit_action();

    if (auto addr = req[1].addr)
    {
        void (*term_fun)() = (void (*)())addr;

        term_fun();
    }

    flush_unit_symbols();

    LLVMOrcResourceTrackerRemove(rt);
    LLVMOrcReleaseResourceTracker(rt);      //- ?

    auto e2 = unwrap(voidc_global_ctx_t::jit)->getExecutionSession().removeJITDylib(*unwrap(jd));       //- WTF ?

    fflush(stdout);     //- WTF?
    fflush(stderr);     //- WTF?
}


//---------------------------------------------------------------------
//- Target template Context
//---------------------------------------------------------------------
template<typename T, typename... TArgs>
void
target_template_ctx_t<T, TArgs...>::add_symbol_value(v_quark_t raw_name, void *value)
{
    symbol_values.insert({raw_name, value});
}

//---------------------------------------------------------------------
template class target_template_ctx_t<base_global_ctx_t, LLVMContextRef, size_t, size_t, size_t>;
template class target_template_ctx_t<base_local_ctx_t, base_global_ctx_t &>;


//---------------------------------------------------------------------
//- Target Global Context
//---------------------------------------------------------------------
target_global_ctx_t::target_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size)
  : target_template_ctx_t(LLVMContextCreate(), int_size, long_size, ptr_size)
{
    initialize();
}

target_global_ctx_t::~target_global_ctx_t()
{
    run_cleaners();

    LLVMContextDispose(llvm_ctx);
}


//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
target_local_ctx_t::target_local_ctx_t(base_global_ctx_t &global)
  : target_template_ctx_t(global)
{
    compiler = make_level_0_target_compiler();
}

target_local_ctx_t::~target_local_ctx_t()
{
    run_cleaners();
}

//---------------------------------------------------------------------
void *
target_local_ctx_t::find_symbol_value(v_quark_t raw_name)
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
visitor_t *
v_get_compiler(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  &lctx.compiler;
}


//---------------------------------------------------------------------
void
v_export_symbol_q(v_quark_t qname, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_symbol(qname, type, value);
}

void
v_export_symbol_type_q(v_quark_t qname, v_type_t *type)
{
    v_export_symbol_q(qname, type, nullptr);
}

void
v_export_symbol_value_q(v_quark_t qname, void *value)
{
    v_export_symbol_q(qname, nullptr, value);
}

void
v_export_symbol(const char *raw_name, v_type_t *type, void *value)
{
    v_export_symbol_q(v_quark_from_string(raw_name), type, value);
}

void
v_export_symbol_type(const char *raw_name, v_type_t *type)
{
    v_export_symbol_q(v_quark_from_string(raw_name), type, nullptr);
}

void
v_export_symbol_value(const char *raw_name, void *value)
{
    v_export_symbol_q(v_quark_from_string(raw_name), nullptr, value);
}

void
v_export_constant_q(v_quark_t qname, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_constant(qname, type, value);
}

void
v_export_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    v_export_constant_q(v_quark_from_string(raw_name), type, value);
}

void
v_export_type_q(v_quark_t qname, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_type(qname, type);
}

void
v_export_type(const char *raw_name, v_type_t *type)
{
    v_export_type_q(v_quark_from_string(raw_name), type);
}

//---------------------------------------------------------------------
void
v_add_symbol_q(v_quark_t qname, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_symbol(qname, type, value);
}

void
v_add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    v_add_symbol_q(v_quark_from_string(raw_name), type, value);
}

void
v_add_constant_q(v_quark_t qname, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_constant(qname, type, value);
}

void
v_add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    v_add_constant_q(v_quark_from_string(raw_name), type, value);
}

void
v_add_type_q(v_quark_t qname, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_type(qname, type);
}

void
v_add_type(const char *raw_name, v_type_t *type)
{
    v_add_type_q(v_quark_from_string(raw_name), type);
}

void
v_export_effort(compile_ctx_action_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_effort(fun, aux);
}

void
v_add_effort(compile_ctx_action_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_effort(fun, aux);
}


//---------------------------------------------------------------------
bool
v_find_constant_q(v_quark_t qname, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    if (auto p = lctx.decls.constants.find(qname))
    {
        t = *p;

        if (value)
        {
            for (auto &cv : {lctx.constant_values, gctx.constant_values})
            {
                auto itv = cv.find(qname);

                if (itv != cv.end())
                {
                    v = itv->second;

                    break;
                }
            }
        }
    }

    if (type)   *type  = t;
    if (value)  *value = v;

    return bool(t);
}

v_type_t *
v_find_constant_type_q(v_quark_t qname)
{
    v_type_t *t = nullptr;

    v_find_constant_q(qname, &t, nullptr);

    return t;
}

LLVMValueRef
v_find_constant_value_q(v_quark_t qname)
{
    LLVMValueRef v = nullptr;

    v_find_constant_q(qname, nullptr, &v);

    return v;
}

bool
v_find_constant(const char *raw_name, v_type_t **type, LLVMValueRef *value)
{
    auto qname = v_quark_try_string(raw_name);

    if (!qname)  return false;

    return  v_find_constant_q(qname, type, value);
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
    debug_print_module = n;
}

void
voidc_debug_print_assembly(bool f)
{
    debug_print_assembly = f;
}

void
v_verify_module(LLVMModuleRef module)
{
    base_global_ctx_t::verify_module(module);
}

void
voidc_verify_jit_module_optimized(bool f)
{
    verify_jit_module_optimized = f;
}

void
voidc_prepare_module_for_jit(LLVMModuleRef module)
{
    voidc_global_ctx_t::prepare_module_for_jit(module);
}

void
voidc_add_module_to_jit(LLVMModuleRef module)
{
    auto &gctx = *voidc_global_ctx_t::voidc;

    gctx.add_module_to_jit(module);
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

    gctx.add_object_file_to_jit(membuf);
}

void
voidc_add_local_object_file_to_jit(LLVMMemoryBufferRef membuf)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.add_object_file_to_jit(membuf);
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

void
voidc_clear_unit_buffer(void)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    if (!lctx.unit_buffer)  return;

    LLVMDisposeMemoryBuffer(lctx.unit_buffer);

    lctx.unit_buffer = nullptr;
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
static
int skip_load = 0;

void
voidc_skip_object_file_load(int n) { skip_load += n; }

void
voidc_object_file_load_to_jit_internal_helper(const char *buf, size_t len, bool is_local)
{
    if (!is_local  &&  skip_load > 0)
    {
        --skip_load;

        return;
    }

    auto modbuf = LLVMCreateMemoryBufferWithMemoryRange(buf, len, "modbuf", 0);

    if (is_local) voidc_add_local_object_file_to_jit(modbuf);
    else          voidc_add_object_file_to_jit(modbuf);

    LLVMDisposeMemoryBuffer(modbuf);
}

void
voidc_compile_load_object_file_to_jit(LLVMMemoryBufferRef membuf, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    auto membuf_ptr  = LLVMGetBufferStart(membuf);
    auto membuf_size = LLVMGetBufferSize(membuf);

    auto membuf_const = LLVMConstString(membuf_ptr, membuf_size, 1);
    auto membuf_const_type = LLVMTypeOf(membuf_const);

    auto membuf_glob = LLVMAddGlobal(lctx.module, membuf_const_type, "membuf_g");

    LLVMSetLinkage(membuf_glob, LLVMPrivateLinkage);

    LLVMSetInitializer(membuf_glob, membuf_const);

    LLVMValueRef val[3];

    v_type_t    *t;
    LLVMValueRef f;

    lctx.obtain_identifier(voidc_object_file_load_to_jit_internal_helper_q, t, f);
    assert(f);

    val[0] = membuf_glob;
    val[1] = LLVMConstInt(gctx.size_t_type->llvm_type(), membuf_size, 0);
    val[2] = LLVMConstInt(gctx.bool_type->llvm_type(), is_local, 0);

    t = static_cast<v_type_pointer_t *>(t)->element_type();

    LLVMBuildCall2(gctx.builder, t->llvm_type(), f, val, 3, "");

    //-----------------------------------------------------------------
    if (do_load)
    {
        if (is_local) voidc_add_local_object_file_to_jit(membuf);
        else          voidc_add_object_file_to_jit(membuf);
    }
}

void
voidc_compile_load_module_to_jit(LLVMModuleRef module, bool is_local, bool do_load)
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

        abort();                //- Sic !!!
    }

    assert(membuf);

    voidc_compile_load_object_file_to_jit(membuf, is_local, do_load);

    LLVMDisposeMemoryBuffer(membuf);
}


VOIDC_DLLEXPORT_END


static void
load_object_file_helper(LLVMMemoryBufferRef membuf, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    //- Generate unit ...

    auto saved_module = lctx.module;

    //- Do replace/make unit...

    if (lctx.unit_buffer)   LLVMDisposeMemoryBuffer(lctx.unit_buffer);

    lctx.prepare_unit_action(0, 0);         //- line, column ?..

    voidc_compile_load_object_file_to_jit(membuf, is_local, do_load);

//  base_global_ctx_t::debug_print_module = 1;

    lctx.finish_unit_action();

    lctx.module = saved_module;
}


static void
load_module_helper(LLVMModuleRef module, bool is_local, bool do_load)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    lctx.finish_module(module);

    gctx.prepare_module_for_jit(module);

    //- Generate unit ...

    auto saved_module = lctx.module;

    //- Do replace/make unit...

    if (lctx.unit_buffer)   LLVMDisposeMemoryBuffer(lctx.unit_buffer);

    lctx.prepare_unit_action(0, 0);         //- line, column ?..

    voidc_compile_load_module_to_jit(module, is_local, do_load);

//  base_global_ctx_t::debug_print_module = 1;

    lctx.finish_unit_action();

    lctx.module = saved_module;
}


VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
void
voidc_unit_load_local_object_file_to_jit(LLVMMemoryBufferRef membuf, bool do_load)
{
    load_object_file_helper(membuf, true, do_load);
}

//---------------------------------------------------------------------
void
voidc_unit_load_object_file_to_jit(LLVMMemoryBufferRef membuf, bool do_load)
{
    load_object_file_helper(membuf, false, do_load);
}


//---------------------------------------------------------------------
void
voidc_unit_load_local_module_to_jit(LLVMModuleRef module, bool do_load)
{
    load_module_helper(module, true, do_load);
}

//---------------------------------------------------------------------
void
voidc_unit_load_module_to_jit(LLVMModuleRef module, bool do_load)
{
    load_module_helper(module, false, do_load);
}


//---------------------------------------------------------------------
obtain_module_t
v_get_obtain_module_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_obtain_module_hook(paux);
}

void
v_set_obtain_module_hook(obtain_module_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_obtain_module_hook(fun, aux);
}

LLVMModuleRef
v_obtain_module(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.obtain_module();
}

bool
v_obtain_identifier_q(v_quark_t qname, v_type_t * *type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t    *t;
    LLVMValueRef v;

    bool ok = lctx.obtain_identifier(qname, t, v);

    if (ok)
    {
        if (type)   *type  = t;
        if (value)  *value = v;
    }

    return  ok;
}

bool
v_obtain_identifier(const char *name, v_type_t * *type, LLVMValueRef *value)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return false;

    return  v_obtain_identifier_q(qname, type, value);
}

//---------------------------------------------------------------------
finish_module_t
v_get_finish_module_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_finish_module_hook(paux);
}

void
v_set_finish_module_hook(finish_module_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_finish_module_hook(fun, aux);
}

void
v_finish_module(LLVMModuleRef mod)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.finish_module(mod);
}


//---------------------------------------------------------------------
void
v_save_builder_ip(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_builder_ip();
}

void
v_restore_builder_ip(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.pop_builder_ip();
}


//---------------------------------------------------------------------
LLVMValueRef
v_prepare_function(const char *raw_name, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.prepare_function(raw_name, type);
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
v_add_variable_q(v_quark_t qname, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.vars = lctx.vars.set(qname, {type, value});
}

void
v_add_variable(const char *name, v_type_t *type, LLVMValueRef value)
{
    v_add_variable_q(v_quark_from_string(name), type, value);
}

bool
v_get_variable_q(v_quark_t qname, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *pv = lctx.vars.find(qname))
    {
        if (type)   *type  = pv->first;
        if (value)  *value = pv->second;

        return true;
    }

    return false;
}

v_type_t *
v_get_variable_type_q(v_quark_t qname)
{
    v_type_t *t = nullptr;

    v_get_variable_q(qname, &t, nullptr);

    return t;
}

LLVMValueRef
v_get_variable_value_q(v_quark_t qname)
{
    LLVMValueRef v = nullptr;

    v_get_variable_q(qname, nullptr, &v);

    return v;
}

bool
v_get_variable(const char *name, v_type_t **type, LLVMValueRef *value)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return false;

    return  v_get_variable_q(qname, type, value);
}

v_type_t *
v_get_variable_type(const char *name)
{
    v_type_t *t = nullptr;

    v_get_variable(name, &t, nullptr);

    return t;
}

LLVMValueRef
v_get_variable_value(const char *name)
{
    LLVMValueRef v = nullptr;

    v_get_variable(name, nullptr, &v);

    return v;
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
v_clear_variables(void)         //- ?
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

    lctx.push_variables();
}

void
v_restore_variables(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.pop_variables();
}

void
v_hold_compilation_state(int n)         //- ?!?
{
    hold_state = n;
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


#ifdef NEW_ADOPT


//---------------------------------------------------------------------
try_to_adopt_t
v_get_try_to_adopt_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_try_to_adopt_hook(paux);
}

void
v_set_try_to_adopt_hook(try_to_adopt_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_try_to_adopt_hook(fun, aux);
}


#else


//---------------------------------------------------------------------
adopt_result_t
v_get_adopt_result_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_adopt_result_hook(paux);
}

void
v_set_adopt_result_hook(adopt_result_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_adopt_result_hook(fun, aux);
}


#endif


void
v_adopt_result(v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.adopt_result(type, value);
}


#ifdef NEW_ADOPT


//---------------------------------------------------------------------
try_to_convert_t
v_get_try_to_convert_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_try_to_convert_hook(paux);
}

void
v_set_try_to_convert_hook(try_to_convert_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_try_to_convert_hook(fun, aux);
}


#else


//---------------------------------------------------------------------
convert_to_type_t
v_get_convert_to_type_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_convert_to_type_hook(paux);
}

void
v_set_convert_to_type_hook(convert_to_type_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_convert_to_type_hook(fun, aux);
}


#endif


LLVMValueRef
v_convert_to_type(v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.convert_to_type(t0, v0, t1);
}

//---------------------------------------------------------------------
make_temporary_t
v_get_make_temporary_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_make_temporary_hook(paux);
}

void
v_set_make_temporary_hook(make_temporary_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_make_temporary_hook(fun, aux);
}

LLVMValueRef
v_make_temporary(v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.make_temporary(type, value);
}

void
v_add_temporary_cleaner(temporary_cleaner_t fun, void *data)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_temporary_cleaner(fun, data);
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

LLVMValueRef
v_get_temporaries_front(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.temporaries_stack.front().first;
}


//---------------------------------------------------------------------
v_type_t *
v_find_symbol_type_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_symbol_type(qname);
}

v_type_t *
v_find_symbol_type(const char *raw_name)
{
    auto qname = v_quark_try_string(raw_name);

    if (!qname)  return nullptr;

    return v_find_symbol_type_q(qname);
}

//---------------------------------------------------------------------
void *
v_find_symbol_value_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.find_symbol_value(qname);
}

void *
v_find_symbol_value(const char *raw_name)
{
    auto qname = v_quark_try_string(raw_name);

    if (!qname)  return nullptr;

    return v_find_symbol_value_q(qname);
}

//---------------------------------------------------------------------
v_type_t *
v_find_type_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto ret = lctx.find_type(qname);

#ifndef NDEBUG

    if (!ret)
    {
        printf("v_find_type: %s  not found!\n", v_quark_to_string(qname));
    }

#endif

    return  ret;
}

v_type_t *
v_find_type(const char *name)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    return v_find_type_q(qname);
}


//---------------------------------------------------------------------
void
v_export_alias_q(v_quark_t qname, v_quark_t qraw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_alias(qname, qraw_name);
}

void
v_export_alias(const char *name, const char *raw_name)
{
    auto q = v_quark_from_string;

    v_export_alias_q(q(name), q(raw_name));
}

void
v_add_alias_q(v_quark_t qname, v_quark_t qraw_name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_alias(qname, qraw_name);
}

void
v_add_alias(const char *name, const char *raw_name)
{
    auto q = v_quark_from_string;

    v_add_alias_q(q(name), q(raw_name));
}

v_quark_t
v_obtain_alias_q(v_quark_t qname, bool _export)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.obtain_alias(qname, _export);
}

const char *
v_obtain_alias(const char *name, bool _export)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto qname = v_quark_from_string(name);

    auto raw_qname = lctx.obtain_alias(qname, _export);

    return  v_quark_to_string(raw_qname);
}

v_quark_t
v_lookup_alias_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.lookup_alias(qname);
}

const char *
v_lookup_alias(const char *name)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return name;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto raw_qname = lctx.lookup_alias(qname);

    return  v_quark_to_string(raw_qname);
}

//---------------------------------------------------------------------
obtain_alias_t
v_get_obtain_alias_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_obtain_alias_hook(paux);
}

void
v_set_obtain_alias_hook(obtain_alias_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_obtain_alias_hook(fun, aux);
}

lookup_alias_t
v_get_lookup_alias_hook(void **paux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return lctx.get_lookup_alias_hook(paux);
}

void
v_set_lookup_alias_hook(lookup_alias_t fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.set_lookup_alias_hook(fun, aux);
}

//---------------------------------------------------------------------
void
v_export_intrinsic_q(v_quark_t qname, void *fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_intrinsic(qname, fun, aux);
}

void
v_export_intrinsic(const char *name, void *fun, void *aux)
{
    v_export_intrinsic_q(v_quark_from_string(name), fun, aux);
}

void
v_add_intrinsic_q(v_quark_t qname, void *fun, void *aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_intrinsic(qname, fun, aux);
}

void
v_add_intrinsic(const char *name, void *fun, void *aux)
{
    v_add_intrinsic_q(v_quark_from_string(name), fun, aux);
}

void *
v_get_intrinsic_q(v_quark_t qname, void **aux)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &intrinsics = lctx.decls.intrinsics;

    if (auto p = intrinsics.find(qname))
    {
        if (aux)  *aux = p->second;

        return  p->first;
    }

    return nullptr;
}

void *
v_get_intrinsic(const char *name, void **aux)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    return v_get_intrinsic_q(qname, aux);
}


//---------------------------------------------------------------------
void
v_export_property_q(v_quark_t qname, const std::any *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_property(qname, *value);
}

void
v_export_property(const char *name, const std::any *value)
{
    v_export_property_q(v_quark_from_string(name), value);
}

void
v_add_property_q(v_quark_t qname, const std::any *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_property(qname, *value);
}

void
v_add_property(const char *name, const std::any *value)
{
    v_add_property_q(v_quark_from_string(name), value);
}

const std::any *
v_get_property_q(v_quark_t qname)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &props = lctx.decls.properties;

    return props.find(qname);
}

const std::any *
v_get_property(const char *name)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    return v_get_property_q(qname);
}


//---------------------------------------------------------------------
void
v_export_overload_q(v_quark_t qname, v_type_t *type, v_quark_t over)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.export_overload(qname, type, over);
}

void
v_export_overload(const char *name, v_type_t *type, const char *over)
{
    v_export_overload_q(v_quark_from_string(name), type, v_quark_from_string(over));
}

void
v_add_overload_q(v_quark_t qname, v_type_t *type, v_quark_t over)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_overload(qname, type, over);
}

void
v_add_overload(const char *name, v_type_t *type, const char *over)
{
    v_add_overload_q(v_quark_from_string(name), type, v_quark_from_string(over));
}

v_quark_t
v_get_overload_q(v_quark_t qname, v_type_t *type)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto *i = lctx.decls.overloads.find(qname))
    {
        if (auto *j = i->find(type))
        {
            return *j;
        }
    }

    return 0;
}

const char *
v_get_overload(const char *name, v_type_t *type)
{
    auto qname = v_quark_try_string(name);

    if (!qname)  return nullptr;

    auto q = v_get_overload_q(qname, type);

    if (q)  return v_quark_to_string(q);
    else    return nullptr;
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
LLVMTargetDataRef
v_target_get_voidc_data_layout(void)
{
    return voidc_global_ctx_t::voidc->data_layout;
}

LLVMTargetDataRef
v_target_get_data_layout(void)
{
    return voidc_global_ctx_t::target->data_layout;
}

void
v_target_set_data_layout(LLVMTargetDataRef layout)
{
    voidc_global_ctx_t::target->data_layout = layout;
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
void
v_target_global_ctx_add_alias(base_global_ctx_t *gctx, v_quark_t name, v_quark_t raw_name)
{
    gctx->decls.aliases_insert({name, raw_name});
}

//---------------------------------------------------------------------
void
v_target_global_ctx_add_intrinsic(base_global_ctx_t *gctx, v_quark_t fun_name, void *fun, void *aux)
{
    gctx->decls.intrinsics_insert({fun_name, {fun, aux}});
}

//---------------------------------------------------------------------
void
v_target_global_ctx_add_constant_value(base_global_ctx_t *gctx, v_quark_t raw_name, LLVMValueRef value)
{
    gctx->constant_values.insert_or_assign(raw_name, value);
}

LLVMValueRef
v_target_global_ctx_get_constant_value(base_global_ctx_t *gctx, v_quark_t raw_name)
{
    auto &cv = gctx->constant_values;

    auto itv = cv.find(raw_name);

    if (itv != cv.end())  return  itv->second;

    return  nullptr;
}


//---------------------------------------------------------------------
base_local_ctx_t *
v_target_create_local_ctx(const char *filename, base_global_ctx_t *gctx)
{
    auto ret = new target_local_ctx_t(*gctx);

    ret->filename = filename;

    auto &map = gctx->imported;

    if (auto it = map.find(filename); it != map.end())
    {
        ret->export_data = &it->second;         //- Sic !!!
    }

    return ret;
}

void
v_target_destroy_local_ctx(base_local_ctx_t *lctx)
{
    delete lctx;
}

//---------------------------------------------------------------------
base_local_ctx_t *
v_target_get_local_ctx(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return  gctx.local_ctx;
}

base_local_ctx_t *
v_target_get_voidc_local_ctx(void)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    return  vctx.local_ctx;
}

bool
v_target_local_ctx_has_parent(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.has_parent();
}

//---------------------------------------------------------------------
const char *
v_target_local_ctx_get_filename(base_local_ctx_t *lctx)
{
    return  lctx->filename.c_str();
}


//---------------------------------------------------------------------
#ifdef _WIN32

void ___chkstk_ms(void) {}       //- WTF !?!?!?!?!?!?!?!?!

#endif


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


