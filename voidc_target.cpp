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
//- Intrinsics (true)
//---------------------------------------------------------------------
static void
v_alloca(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 1  ||  (*args)->data.size() > 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    auto type = lctx.obtain_type((*args)->data[0]);
    auto llvm_type = type->llvm_type();

    LLVMValueRef v;

    if ((*args)->data.size() == 1)              //- Just one
    {
        v = LLVMBuildAlloca(gctx.builder, llvm_type, "");
    }
    else                                        //- Array...
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[1]->accept(*vis, aux);        //- Array size

        v = LLVMBuildArrayAlloca(gctx.builder, llvm_type, lctx.result_value, "");
    }

    auto t = gctx.make_pointer_type(type, LLVMGetPointerAddressSpace(LLVMTypeOf(v)));

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_getelementptr(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    auto arg_count = (*args)->data.size();

    auto types  = std::make_unique<v_type_t *[]>(arg_count);
    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    for (int i=0; i<arg_count; ++i)
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis, aux);

        types[i]  = lctx.result_type;
        values[i] = lctx.result_value;
    }

    auto v = LLVMBuildGEP(gctx.builder, values[0], &values[1], arg_count-1, "");

    auto *p = static_cast<v_type_pointer_t *>(v_type_get_scalar_type(types[0]));

    v_type_t *t = p->element_type();

    for (int i=2; i<arg_count; ++i)
    {
        if (auto *ti = dynamic_cast<v_type_array_t *>(t))
        {
            t = ti->element_type();
            continue;
        }

        if (auto *ti = dynamic_cast<v_type_vector_t *>(t))
        {
            t = ti->element_type();
            continue;
        }

        if (auto *ti = dynamic_cast<v_type_struct_t *>(t))
        {
            auto idx = values[i];

            auto num = unsigned(LLVMConstIntGetZExtValue(idx));

            t = ti->element_types()[num];
            continue;
        }

        assert(false && "Wrong GEP index");

        t = nullptr;
        break;
    }

    t = gctx.make_pointer_type(t, p->address_space());

    for (int i=0; i<arg_count; ++i)
    {
        if (auto *vt = dynamic_cast<v_type_vector_t *>(types[i]))
        {
            auto count = vt->size();

            if (vt->is_scalable())  t = gctx.make_svector_type(t, count);
            else                    t = gctx.make_vector_type(t, count);

            break;
        }
    }

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_store(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[1]->accept(*vis, aux);        //- "Place"

    auto place = lctx.result_value;

    lctx.result_type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    (*args)->data[0]->accept(*vis, aux);        //- "Value"

    LLVMBuildStore(gctx.builder, lctx.result_value, place);
}

//---------------------------------------------------------------------
static void
v_load(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis, aux);

    auto v = LLVMBuildLoad(gctx.builder, lctx.result_value, "");

    auto t = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_cast(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[0]->accept(*vis, aux);        //- Value

    auto src_value = lctx.result_value;

    auto src_type = lctx.result_type;
    auto dst_type = lctx.obtain_type((*args)->data[1]);

    auto opcode = LLVMOpcode(0);

    if (auto *pst = dynamic_cast<v_type_reference_t *>(src_type))
    {
        if (auto *pdt = dynamic_cast<v_type_reference_t *>(dst_type))
        {
            //- WTF ?!?!?!?!?!?!?

            if (pst->address_space() == pdt->address_space())   opcode = LLVMBitCast;
            else                                                opcode = LLVMAddrSpaceCast;
        }
        else
        {
            src_value = LLVMBuildLoad(gctx.builder, src_value, "tmp");

            src_type  = pst->element_type();
        }
    }

    if (!opcode)
    {
        auto src_stype = v_type_get_scalar_type(src_type);
        auto dst_stype = v_type_get_scalar_type(dst_type);

        if (v_type_is_floating_point(src_stype))
        {
            if (v_type_is_floating_point(dst_stype))
            {
                auto sz = v_type_floating_point_get_width(src_stype);
                auto dz = v_type_floating_point_get_width(dst_stype);

                if (sz == dz)       opcode = LLVMBitCast;       //- ?
                else if (sz > dz)   opcode = LLVMFPTrunc;
                else                opcode = LLVMFPExt;
            }
            else if (auto *idt = dynamic_cast<v_type_integer_t *>(dst_stype))
            {
                if (idt->is_signed()) opcode = LLVMFPToSI;
                else                  opcode = LLVMFPToUI;
            }
            else
            {
                throw std::runtime_error("Bad cast from floating point");
            }
        }
        else if (auto *ist = dynamic_cast<v_type_integer_t *>(src_stype))
        {
            if (v_type_is_floating_point(dst_stype))
            {
                if (ist->is_signed()) opcode = LLVMSIToFP;
                else                  opcode = LLVMUIToFP;
            }
            else if (auto *idt = dynamic_cast<v_type_integer_t *>(dst_stype))
            {
                auto sw = ist->width();
                auto dw = idt->width();

                if (sw == dw)       opcode = LLVMBitCast;       //- ?
                else if (sw > dw)   opcode = LLVMTrunc;
                else
                {
                    if (ist->is_signed()) opcode = LLVMSExt;        //- ?
                    else                  opcode = LLVMZExt;        //- ?
                }
            }
            else if (auto *pdt = dynamic_cast<v_type_pointer_t *>(dst_stype))
            {
                opcode = LLVMIntToPtr;
            }
            else
            {
                throw std::runtime_error("Bad cast from integer");
            }
        }
        else if (auto *pst = dynamic_cast<v_type_pointer_t *>(src_stype))
        {
            if (auto *idt = dynamic_cast<v_type_integer_t *>(dst_stype))
            {
                opcode = LLVMPtrToInt;
            }
            else if (auto *pdt = dynamic_cast<v_type_pointer_t *>(dst_stype))
            {
                if (pst->address_space() == pdt->address_space())   opcode = LLVMBitCast;
                else                                                opcode = LLVMAddrSpaceCast;
            }
            else
            {
                throw std::runtime_error("Bad cast from pointer");
            }
        }
        else
        {
            throw std::runtime_error("Bad cast");
        }
    }

    auto v = LLVMBuildCast(gctx.builder, opcode, src_value, dst_type->llvm_type(), "");

    lctx.result_type = tt;

    lctx.adopt_result(dst_type, v);
}

//---------------------------------------------------------------------
static void
v_pointer(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[0]->accept(*vis, aux);

    auto t = lctx.result_type;
    auto v = lctx.result_value;

    if (t->kind() == v_type_t::k_reference)
    {
        auto rt = static_cast<v_type_reference_t *>(lctx.result_type);

        t = gctx.make_pointer_type(rt->element_type(), rt->address_space());
    }
    else
    {
        v = lctx.make_temporary(t, v);

        t = gctx.make_pointer_type(t, 0);
    }

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_reference(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 1)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[0]->accept(*vis, aux);

    auto pt = static_cast<v_type_pointer_t *>(lctx.result_type);

    auto v = lctx.result_value;
    auto t = gctx.make_reference_type(pt->element_type(), pt->address_space());

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_assign(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;

    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[0]->accept(*vis, aux);        //- "Place"

    auto type  = lctx.result_type;
    auto place = lctx.result_value;

    lctx.result_type = static_cast<v_type_reference_t *>(lctx.result_type)->element_type();

    (*args)->data[1]->accept(*vis, aux);        //- "Value"

    LLVMBuildStore(gctx.builder, lctx.result_value, place);

    lctx.result_type = tt;

    lctx.adopt_result(type, place);
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
#define DEF(name) \
    intrinsics[#name] = name;

    DEF(v_alloca)
    DEF(v_getelementptr)
    DEF(v_store)
    DEF(v_load)
    DEF(v_cast)
    DEF(v_pointer)
    DEF(v_reference)
    DEF(v_assign)

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

    auto opaque_type = voidc_global_ctx_t::voidc->opaque_type_type;

#define DEF(name) \
    add_symbol(#name, opaque_type, (void *)name##_type);

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

    constants["false"] = { bool_type, LLVMConstInt(bool_type->llvm_type(), 0, false) };
    constants["true"]  = { bool_type, LLVMConstInt(bool_type->llvm_type(), 1, false) };

    //-----------------------------------------------------------------
    v_type_t *i8_ptr_type = make_pointer_type(make_int_type(8), 0);

    auto stacksave_ft    = make_function_type(i8_ptr_type, nullptr, 0, false);
    auto stackrestore_ft = make_function_type(void_type, &i8_ptr_type, 1, false);

    add_symbol_type("llvm.stacksave",    stacksave_ft);
    add_symbol_type("llvm.stackrestore", stackrestore_ft);
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
v_type_t *
base_local_ctx_t::find_type(const char *type_name)
{
    if (auto *p = vars.find(type_name))
    {
        if (p->second == nullptr)   return p->first;    //- Sic!
    }

    return nullptr;
}

//---------------------------------------------------------------------
v_type_t *
base_local_ctx_t::obtain_type(const ast_expr_sptr_t &expr)
{
    v_type_t *ret;

    expr->accept(voidc_type_calc, &ret);

    return ret;
}

//---------------------------------------------------------------------
bool
base_local_ctx_t::obtain_identifier(const std::string &name, v_type_t * &type, LLVMValueRef &value)
{
    type  = nullptr;
    value = nullptr;

    if (auto *p = vars.find(name))
    {
        type  = p->first;
        value = p->second;
    }
    else
    {
        auto raw_name = check_alias(name);

        {   auto it = constants.find(raw_name);

            if (it != constants.end())
            {
                type  = it->second.first;
                value = it->second.second;
            }
        }

        if (!value)
        {
            auto it = global_ctx.constants.find(raw_name);

            if (it != global_ctx.constants.end())
            {
                type  = it->second.first;
                value = it->second.second;
            }
        }

        if (!value)
        {
            auto cname = raw_name.c_str();

            type = find_symbol_type(cname);

            if (!type)  return false;

            if (auto *ft = dynamic_cast<v_type_function_t *>(type))
            {
                value = LLVMGetNamedFunction(module, cname);

                if (!value) value = LLVMAddFunction(module, cname, ft->llvm_type());

                type = global_ctx.make_pointer_type(type, 0);       //- Sic!
            }
            else
            {
                value = LLVMGetNamedGlobal(module, cname);

                v_type_t *t = type;

                bool is_reference = (type->kind() == v_type_t::k_reference);

                if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

                if (!value) value = LLVMAddGlobal(module, t->llvm_type(), cname);

                if (!is_reference)  type = global_ctx.make_pointer_type(type, 0);       //- Sic!
            }
        }
    }

    return bool(value);
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
    opaque_type_type(make_struct_type("struct.voidc_opaque_type"))
{
    assert(voidc_global_ctx == nullptr);

    voidc_global_ctx = this;

    add_symbol("voidc_opaque_type", opaque_type_type, opaque_type_type);        //- Sic!

    {   auto type_ptr_type = make_pointer_type(opaque_type_type, 0);

        add_symbol("v_type_ptr", opaque_type_type, type_ptr_type);

        v_type_t *types[] =
        {
            type_ptr_type,
            make_pointer_type(type_ptr_type, 0),
            unsigned_type,
            bool_type
        };

#define DEF(name, ret, num) \
        add_symbol_type(#name, make_function_type(ret, types, num, false));

        DEF(v_function_type, type_ptr_type, 4)

        types[0] = char_ptr_type;
        types[1] = type_ptr_type;

        DEF(v_add_symbol_type, void_type, 2)

        DEF(v_struct_type_named, type_ptr_type, 1)

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
    voidc->add_symbol_value("stdin",  (void *)stdin);       //- WTF?
    voidc->add_symbol_value("stdout", (void *)stdout);      //- WTF?
    voidc->add_symbol_value("stderr", (void *)stderr);      //- WTF?

    //-------------------------------------------------------------
    {   v_type_t *typ[3];

        typ[0] = voidc->char_ptr_type;
        typ[1] = voidc->size_t_type;
        typ[2] = voidc->bool_type;

        auto ft = voidc->make_function_type(voidc->void_type, typ, 3, false);

        voidc->add_symbol_type("voidc_object_file_load_to_jit_internal_helper", ft);
    }

    //-------------------------------------------------------------
    voidc_global_ctx_t::voidc->flush_unit_symbols();

    //-------------------------------------------------------------
#ifndef NDEBUG

//  LLVMEnablePrettyStackTrace();

#endif
}

//---------------------------------------------------------------------
static std::forward_list<void (*)(void)> voidc_atexit_list;

void
voidc_global_ctx_t::static_terminate(void)
{
    for (auto it: voidc_atexit_list)  it();

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
voidc_global_ctx_t::add_symbol_type(const char *raw_name, v_type_t *type)
{
    symbol_types[raw_name] = type;
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
voidc_global_ctx_t::add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    symbol_types[raw_name] = type;

    if (unwrap(jit)->lookup(*unwrap(main_jd), raw_name)) return;

    unit_symbols[unwrap(jit)->mangleAndIntern(raw_name)] = JITEvaluatedSymbol::fromPointer(value);
}


//---------------------------------------------------------------------
v_type_t *
voidc_global_ctx_t::get_symbol_type(const char *raw_name)
{
    auto it = symbol_types.find(raw_name);

    if (it != symbol_types.end())  return it->second;

    return nullptr;
}

//---------------------------------------------------------------------
void *
voidc_global_ctx_t::get_symbol_value(const char *raw_name)
{
    auto sym = unwrap(jit)->lookup(*unwrap(main_jd), raw_name);

    if (sym)  return (void *)sym->getAddress();

    return nullptr;
}

//---------------------------------------------------------------------
void
voidc_global_ctx_t::get_symbol(const char *raw_name, v_type_t * &type, void * &value)
{
    type  = nullptr;
    value = nullptr;

    auto it = symbol_types.find(raw_name);

    if (it != symbol_types.end())  type = it->second;

    auto sym = unwrap(jit)->lookup(*unwrap(main_jd), raw_name);

    if (sym)  value = (void *)sym->getAddress();
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
voidc_local_ctx_t::voidc_local_ctx_t(const std::string filename, voidc_global_ctx_t &global)
  : base_local_ctx_t(filename, global)
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
voidc_local_ctx_t::add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    symbol_types[raw_name] = type;

    if (!value) return;

    auto &jit = voidc_global_ctx_t::jit;

    if (unwrap(jit)->lookup(*unwrap(local_jd), raw_name)) return;

    unit_symbols[unwrap(jit)->mangleAndIntern(raw_name)] = JITEvaluatedSymbol::fromPointer(value);
}

//---------------------------------------------------------------------
v_type_t *
voidc_local_ctx_t::find_type(const char *type_name)
{
    if (auto r = base_local_ctx_t::find_type(type_name))  return r;

    auto raw_name = check_alias(type_name);

    auto cname = raw_name.c_str();

    if (find_symbol_type(cname) == voidc_global_ctx_t::voidc->opaque_type_type)
    {
        auto sym = unwrap(voidc_global_ctx_t::jit)->lookup(*unwrap(local_jd), cname);

        if (!sym)       //- WTF?
        {
            sym = unwrap(voidc_global_ctx_t::jit)->lookup(*unwrap(voidc_global_ctx_t::main_jd), cname);
        }

        if (sym)  return (v_type_t *)sym->getAddress();
    }

    return nullptr;
}

//---------------------------------------------------------------------
v_type_t *
voidc_local_ctx_t::find_symbol_type(const char *raw_name)
{
    {   auto it = symbol_types.find(raw_name);

        if (it != symbol_types.end())
        {
            return it->second;
        }
    }

    {   auto it = voidc_global_ctx_t::voidc->symbol_types.find(raw_name);

        if (it != voidc_global_ctx_t::voidc->symbol_types.end())
        {
            return it->second;
        }
    }

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
    LLVMContextDispose(llvm_ctx);
}


//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol_type(const char *raw_name, v_type_t *type)
{
    auto it = symbols.find(raw_name);

    if (it != symbols.end())
    {
        it->second.first = type;
    }
    else
    {
        symbols[raw_name] = {type, nullptr};
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
        symbols[raw_name] = {nullptr, value};
    }
}

//---------------------------------------------------------------------
void
target_global_ctx_t::add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    symbols[raw_name] = {type, value};
}


//---------------------------------------------------------------------
v_type_t *
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
target_global_ctx_t::get_symbol(const char *raw_name, v_type_t * &type, void * &value)
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
target_local_ctx_t::add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    symbols[raw_name] = {type, value};
}

//---------------------------------------------------------------------
v_type_t *
target_local_ctx_t::find_type(const char *type_name)
{
    if (auto r = base_local_ctx_t::find_type(type_name))  return r;

    v_type_t *tt = nullptr;

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

    if (tt != voidc_global_ctx_t::voidc->opaque_type_type)
    {
        tv = nullptr;
    }

    return (v_type_t *)tv;
}

//---------------------------------------------------------------------
v_type_t *
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
void
v_add_symbol_type(const char *raw_name, v_type_t *type)
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
v_add_symbol(const char *raw_name, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.add_symbol(raw_name, type, value);
}

void
v_add_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;

    gctx.constants[raw_name] = {type, value};
}

//---------------------------------------------------------------------
void
v_add_local_symbol(const char *raw_name, v_type_t *type, void *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.add_symbol(raw_name, type, value);
}

void
v_add_local_constant(const char *raw_name, v_type_t *type, LLVMValueRef value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.constants[raw_name] = {type, value};
}

//---------------------------------------------------------------------
bool
v_find_constant(const char *raw_name, v_type_t **type, LLVMValueRef *value)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t    *t = nullptr;
    LLVMValueRef v = nullptr;

    {   auto it = lctx.constants.find(raw_name);

        if (it != lctx.constants.end())
        {
            t = it->second.first;
            v = it->second.second;
        }
    }

    if (!t)
    {
        auto it = gctx.constants.find(raw_name);

        if (it != gctx.constants.end())
        {
            t = it->second.first;
            v = it->second.second;
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
v_find_symbol_type(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t *type = nullptr;

    auto raw_name = lctx.check_alias(name);

    {   auto it = lctx.constants.find(raw_name);

        if (it != lctx.constants.end())  type = it->second.first;
    }

    if (!type)
    {
        auto it = gctx.constants.find(raw_name);

        if (it != gctx.constants.end())  type = it->second.first;
    }

    if (!type)
    {
        type = lctx.find_symbol_type(raw_name.c_str());
    }

    return type;
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
v_type_t *
v_obtain_type(const ast_expr_sptr_t *expr)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.obtain_type(*expr);
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

    if (fun)  gctx.intrinsics[name] = fun;
    else      gctx.intrinsics.erase(name);
}

base_global_ctx_t::intrinsic_t
v_get_intrinsic(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;

    auto it = gctx.intrinsics.find(name);

    if (it != gctx.intrinsics.end())  return it->second;
    else                              return nullptr;
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

//---------------------------------------------------------------------
bool
v_target_local_ctx_has_parent(void)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    return  lctx.has_parent();
}


//---------------------------------------------------------------------
void
voidc_atexit(void (*func)(void))
{
    voidc_atexit_list.push_front(func);
}


//---------------------------------------------------------------------
#ifdef _WIN32

void ___chkstk_ms(void) {}       //- WTF !?!?!?!?!?!?!?!?!

#endif


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


