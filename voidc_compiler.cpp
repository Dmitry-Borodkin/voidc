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
visitor_sptr_t voidc_type_calc;


//=====================================================================
//- AST Visitor - Compiler (level 0) ...
//=====================================================================
static
void compile_ast_stmt_list_t(const visitor_sptr_t *vis, void *aux,
                             const ast_stmt_list_sptr_t *list)
{
    for (auto &it : (*list)->data)
    {
        it->accept(*vis, aux);
    }
}

static
void compile_ast_expr_list_t(const visitor_sptr_t *vis, void *aux,
                             const ast_expr_list_sptr_t *list)
{
    for (auto &it : (*list)->data)
    {
        it->accept(*vis, aux);
    }
}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_sptr_t *vis, void *aux,
                        const ast_stmt_list_sptr_t *stmt_list, int line, int column)
{
    if (!*stmt_list)  return;

    auto saved_target = voidc_global_ctx_t::target;

    voidc_global_ctx_t::target = voidc_global_ctx_t::voidc;

    auto &gctx = *voidc_global_ctx_t::target;                           //- target == voidc (!)
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);     //- Sic!

    auto saved_module = lctx.module;

    lctx.prepare_unit_action(line, column);

    (*stmt_list)->accept(*vis, aux);

    lctx.finish_unit_action();

    lctx.module = saved_module;

    voidc_global_ctx_t::target = saved_target;
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_ast_stmt_t(const visitor_sptr_t *vis, void *aux,
                        const std::string *vname, const ast_expr_sptr_t *expr)
{
    if (!*expr) return;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = INVIOLABLE_TAG;

    lctx.push_temporaries();

    (*expr)->accept(*vis, aux);

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
void compile_ast_expr_call_t(const visitor_sptr_t *vis, void *aux,
                             const ast_expr_sptr_t      *fexpr,
                             const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto fname = std::dynamic_pointer_cast<const ast_expr_identifier_t>(*fexpr))
    {
        auto &fun_name = fname->name;

        auto &intrinsics = lctx.decls.intrinsics;

        if (intrinsics.count(fun_name))
        {
            intrinsics[fun_name](vis, aux, args);

            return;
        }
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*fexpr)->accept(*vis, aux);

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

        (*args)->data[i]->accept(*vis, aux);

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
void compile_ast_expr_identifier_t(const visitor_sptr_t *vis, void *aux,
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
void compile_ast_expr_integer_t(const visitor_sptr_t *vis, void *aux,
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

        if (t->kind() == v_type_t::k_pointer  &&  num == 0)
        {
            v = LLVMConstPointerNull(t->llvm_type());
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
void compile_ast_expr_string_t(const visitor_sptr_t *vis, void *aux,
                               const std::string *str)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto v = LLVMBuildGlobalStringPtr(gctx.builder, str->c_str(), "str");

    lctx.adopt_result(gctx.char_ptr_type, v);
}


//---------------------------------------------------------------------
//- expr_char
//---------------------------------------------------------------------
static
void compile_ast_expr_char_t(const visitor_sptr_t *vis, void *aux,
                             char32_t c)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = gctx.char32_t_type;

    auto v = LLVMConstInt(t->llvm_type(), c, false);

    lctx.adopt_result(t, v);
}


//=====================================================================
//- Type calculator - just expr_identifier...
//=====================================================================
static
void typecalc_ast_expr_identifier_t(const visitor_sptr_t *vis, void *aux,
                                    const std::string *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto type = lctx.find_type(name->c_str()))
    {
        assert(aux);

        auto ret = reinterpret_cast<v_type_t **>(aux);

        *ret = type;
    }
    else
    {
        throw std::runtime_error("Type not found: " + *name);
    }
}


//=====================================================================
//- Compiler visitor(s)
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

        compile_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }

    assert(compile_visitor_level_zero);

    return  compile_visitor_level_zero;
}


//---------------------------------------------------------------------
static
visitor_sptr_t typecalc_visitor_level_zero;

visitor_sptr_t
make_voidc_type_calc(void)
{
    if (!typecalc_visitor_level_zero)
    {
        voidc_visitor_t vis;

#define DEF(type) \
        vis = vis.set_void_method(v_##type##_visitor_method_tag, (void *)typecalc_##type);

        DEF(ast_expr_identifier_t)

#undef DEF

        typecalc_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }

    assert(typecalc_visitor_level_zero);

    return  typecalc_visitor_level_zero;
}



