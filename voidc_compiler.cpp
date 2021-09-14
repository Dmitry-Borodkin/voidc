//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_compiler.h"

#include "voidc_types.h"
#include "voidc_target.h"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
visitor_sptr_t voidc_compiler;


//=====================================================================
//- AST Visitor - Compiler (level 0) ...
//=====================================================================
static
void compile_ast_stmt_list_t(const visitor_sptr_t *vis, void *,
                             const ast_stmt_list_sptr_t *list)
{
    for (auto &it : (*list)->data)
    {
        it->accept(*vis);
    }
}

static
void compile_ast_expr_list_t(const visitor_sptr_t *vis, void *,
                             const ast_expr_list_sptr_t *list)
{
    for (auto &it : (*list)->data)
    {
        it->accept(*vis);
    }
}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_sptr_t *vis, void *,
                        const ast_stmt_list_sptr_t *stmt_list, int line, int column)
{
    if (!*stmt_list)  return;

    auto saved_target = voidc_global_ctx_t::target;

    voidc_global_ctx_t::target = voidc_global_ctx_t::voidc;

    auto &gctx = *voidc_global_ctx_t::target;                           //- target == voidc (!)
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);     //- Sic!

    auto saved_module = lctx.module;

    lctx.prepare_unit_action(line, column);

    (*stmt_list)->accept(*vis);

    lctx.finish_unit_action();

    lctx.module = saved_module;

    voidc_global_ctx_t::target = saved_target;
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_ast_stmt_t(const visitor_sptr_t *vis, void *,
                        const std::string *vname, const ast_expr_sptr_t *expr)
{
    if (!*expr) return;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = INVIOLABLE_TAG;

    lctx.push_temporaries();

    (*expr)->accept(*vis);

    lctx.pop_temporaries();

    auto const &ret_name = vname->c_str();

    if (ret_name[0])
    {
        if (lctx.result_value)
        {
            size_t len = 0;

            LLVMGetValueName2(lctx.result_value, &len);

            if (len == 0)
            {
                LLVMSetValueName2(lctx.result_value, ret_name, vname->size());
            }
        }

        lctx.vars = lctx.vars.set(*vname, {lctx.result_type, lctx.result_value});
    }
}


//---------------------------------------------------------------------
//- expr_call
//---------------------------------------------------------------------
static
void compile_ast_expr_call_t(const visitor_sptr_t *vis, void *,
                             const ast_expr_sptr_t      *fexpr,
                             const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto fname = std::dynamic_pointer_cast<const ast_expr_identifier_t>(*fexpr))
    {
        auto &fun_name = fname->name;

        void *void_fun = nullptr;
        void *aux;

        {   auto &intrinsics = lctx.decls.intrinsics;

            auto it = intrinsics.find(fun_name);

            if (it != intrinsics.end())
            {
                void_fun = it->second.first;
                aux      = it->second.second;
            }
        }

        if (!void_fun)
        {
            auto &intrinsics = (*vis)->intrinsics;

            if (auto p = intrinsics.find(fun_name))
            {
                void_fun = p->first;
                aux      = p->second;
            }
        }

        if (void_fun)
        {
            typedef void (*intrinsic_t)(const visitor_sptr_t *vis, void *aux, const ast_expr_list_sptr_t *args);

            reinterpret_cast<intrinsic_t>(void_fun)(vis, aux, args);

            return;
        }
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*fexpr)->accept(*vis);

    v_type_t    *t = lctx.result_type;
    LLVMValueRef f = lctx.result_value;


    if (auto *ft = dynamic_cast<v_type_pointer_t *>(t))
    {
        t = ft->element_type();
    }

    auto ft = static_cast<v_type_function_t *>(t);

    auto par_count = ft->param_count();
    auto par_types = ft->param_types();

    auto arg_count = (*args)->data.size();


    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    for (int i=0; i<arg_count; ++i)
    {
        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis);

        values[i] = lctx.result_value;
    }

    auto v = LLVMBuildCall(gctx.builder, f, values.get(), arg_count, "");


    lctx.result_type = tt;

    lctx.adopt_result(ft->return_type(), v);
}


//---------------------------------------------------------------------
//- expr_identifier
//---------------------------------------------------------------------
static
void compile_ast_expr_identifier_t(const visitor_sptr_t *vis, void *,
                                   const std::string *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    LLVMValueRef v = nullptr;
    v_type_t    *t = nullptr;

    if (!lctx.obtain_identifier(*name, t, v))
    {
        throw std::runtime_error("Identifier not found: " + *name);
    }

    if (!v  &&  lctx.result_type == voidc_global_ctx_t::voidc->type_ptr_type)
    {
        auto raw_name = lctx.check_alias(*name);

        auto cname = raw_name.c_str();

        t = lctx.get_symbol_type(cname);

        assert(t == voidc_global_ctx_t::voidc->opaque_type_type);

        v = LLVMGetNamedGlobal(lctx.module, cname);

        if (!v) v = LLVMAddGlobal(lctx.module, t->llvm_type(), cname);

        t = voidc_global_ctx_t::voidc->type_ptr_type;
    }

    lctx.adopt_result(t, v);
}


//---------------------------------------------------------------------
//- expr_integer
//---------------------------------------------------------------------
static
void compile_ast_expr_integer_t(const visitor_sptr_t *vis, void *,
                                intptr_t num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = lctx.result_type;

    LLVMValueRef v;

    if (t == INVIOLABLE_TAG  ||  t == UNREFERENCE_TAG)
    {
        t = gctx.int_type;

        v = LLVMConstInt(t->llvm_type(), (long long)num, true);

        lctx.result_type = t;
        lctx.result_value = v;
    }
    else
    {
        bool is_reference = (t->kind() == v_type_t::k_reference);

        if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

        if (num == 0)
        {
            v = LLVMConstNull(t->llvm_type());      //- Sic!!!
        }
        else
        {
            static const unsigned tot_bits = 8*sizeof(long long);

            unsigned w = tot_bits - __builtin_clrsbll(num);

            if (w == 0) w = 1;

            t = gctx.make_int_type(w);

            v = LLVMConstInt(t->llvm_type(), (long long)num, true);
        }

        lctx.adopt_result(t, v);
    }
}


//---------------------------------------------------------------------
//- expr_string
//---------------------------------------------------------------------
static
void compile_ast_expr_string_t(const visitor_sptr_t *vis, void *,
                               const std::string *str)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;


    auto typ_ = gctx.make_array_type(gctx.char_type, str->size()+1)->llvm_type();

    auto str_ga = LLVMAddGlobal(lctx.module, typ_, "str");

    LLVMSetLinkage(str_ga, LLVMPrivateLinkage);

    auto str_va = LLVMConstStringInContext(gctx.llvm_ctx, str->c_str(), unsigned(str->size()), 0);

    LLVMSetInitializer(str_ga, str_va);

    auto n0 = LLVMConstNull(gctx.int_type->llvm_type());

    LLVMValueRef val[2] = { n0, n0 };

    auto v = LLVMConstGEP(str_ga, val, 2);


//    auto v = LLVMBuildGlobalStringPtr(gctx.builder, str->c_str(), "str");

    lctx.adopt_result(gctx.char_ptr_type, v);
}


//---------------------------------------------------------------------
//- expr_char
//---------------------------------------------------------------------
static
void compile_ast_expr_char_t(const visitor_sptr_t *vis, void *,
                             char32_t c)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = gctx.char32_t_type;

    auto v = LLVMConstInt(t->llvm_type(), c, false);

    lctx.adopt_result(t, v);
}


//=====================================================================
//- Intrinsics (true)
//=====================================================================
static void
v_alloca(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() < 1  ||  (*args)->data.size() > 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    auto tt = lctx.result_type;


    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[0]->accept(*vis);             //- Element type

    assert(lctx.result_value == nullptr);


    auto type = lctx.result_type;
    auto llvm_type = type->llvm_type();

    LLVMValueRef v;

    if ((*args)->data.size() == 1)              //- Just one
    {
        v = LLVMBuildAlloca(gctx.builder, llvm_type, "");
    }
    else                                        //- Array...
    {
        lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[1]->accept(*vis);             //- Array size

        v = LLVMBuildArrayAlloca(gctx.builder, llvm_type, lctx.result_value, "");
    }

    auto t = gctx.make_pointer_type(type, LLVMGetPointerAddressSpace(LLVMTypeOf(v)));

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_getelementptr(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
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

        (*args)->data[i]->accept(*vis);

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
v_store(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(*args);
    if ((*args)->data.size() != 2)
    {
        throw std::runtime_error("Wrong arguments number: " + std::to_string((*args)->data.size()));
    }

    lctx.result_type = UNREFERENCE_TAG;

    (*args)->data[1]->accept(*vis);             //- "Place"

    auto place = lctx.result_value;

    lctx.result_type = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    (*args)->data[0]->accept(*vis);             //- "Value"

    LLVMBuildStore(gctx.builder, lctx.result_value, place);
}

//---------------------------------------------------------------------
static void
v_load(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
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

    (*args)->data[0]->accept(*vis);

    auto v = LLVMBuildLoad(gctx.builder, lctx.result_value, "");

    auto t = static_cast<v_type_pointer_t *>(lctx.result_type)->element_type();

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_cast(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
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

    (*args)->data[0]->accept(*vis);             //- Value

    auto src_value = lctx.result_value;

    auto src_type = lctx.result_type;


    lctx.result_type = INVIOLABLE_TAG;

    (*args)->data[1]->accept(*vis);             //- Type

    assert(lctx.result_value == nullptr);


    auto dst_type = lctx.result_type;

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
v_pointer(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
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

    (*args)->data[0]->accept(*vis);

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
v_reference(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
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

    (*args)->data[0]->accept(*vis);

    auto pt = static_cast<v_type_pointer_t *>(lctx.result_type);

    auto v = lctx.result_value;
    auto t = gctx.make_reference_type(pt->element_type(), pt->address_space());

    lctx.result_type = tt;

    lctx.adopt_result(t, v);
}

//---------------------------------------------------------------------
static void
v_assign(const visitor_sptr_t *vis, void *, const ast_expr_list_sptr_t *args)
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

    (*args)->data[0]->accept(*vis);             //- "Place"

    auto type  = lctx.result_type;
    auto place = lctx.result_value;

    lctx.result_type = static_cast<v_type_reference_t *>(lctx.result_type)->element_type();

    (*args)->data[1]->accept(*vis);             //- "Value"

    LLVMBuildStore(gctx.builder, lctx.result_value, place);

    lctx.result_type = tt;

    lctx.adopt_result(type, place);
}


//=====================================================================
//- Compiler visitor
//=====================================================================
static
visitor_sptr_t compile_visitor_level_zero;

visitor_sptr_t
make_voidc_compiler(void)
{
    if (!compile_visitor_level_zero)
    {
        voidc_visitor_t vis;

#define DEF(type) \
        vis = vis.set_void_method(v_##type##_visitor_method_tag, (void *)compile_##type);

        DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF

#define DEF(name) \
        vis = vis.set_intrinsic(#name, (void *)name, nullptr);

        DEF(v_alloca)
        DEF(v_getelementptr)
        DEF(v_store)
        DEF(v_load)
        DEF(v_cast)
        DEF(v_pointer)
        DEF(v_reference)
        DEF(v_assign)

#undef DEF

        compile_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }

    assert(compile_visitor_level_zero);

    return  compile_visitor_level_zero;
}


