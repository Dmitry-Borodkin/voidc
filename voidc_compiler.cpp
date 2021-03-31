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
static void compile_ast_stmt_list_t(const visitor_sptr_t *vis, void *aux, size_t count, bool start) {}
static void compile_ast_arg_list_t(const visitor_sptr_t *vis, void *aux, size_t count, bool start) {}


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

    assert(lctx.args->values.empty());

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
                        const std::string *vname, const ast_call_sptr_t *call)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;
    auto &larg = *lctx.args;

    larg.ret_type  = nullptr;
    larg.ret_value = nullptr;

    (*call)->accept(*vis, aux);

    auto const &ret_name = vname->c_str();

    if (ret_name[0])
    {
        size_t len = 0;

        LLVMGetValueName2(larg.ret_value, &len);

        if (len == 0)
        {
            LLVMSetValueName2(larg.ret_value, ret_name, vname->size());
        }

        lctx.vars = lctx.vars.set(*vname, {larg.ret_type, larg.ret_value});
    }
}


//---------------------------------------------------------------------
//- call
//---------------------------------------------------------------------
static
void compile_ast_call_t(const visitor_sptr_t *vis, void *aux,
                        const std::string *fname, const ast_arg_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;
    auto &larg = *lctx.args;

    assert(larg.values.empty());

    auto &fun_name = *fname;

    if (gctx.intrinsics.count(fun_name))
    {
        gctx.intrinsics[fun_name](vis, aux, args);

        return;
    }

    LLVMValueRef f = nullptr;
    v_type_t    *t = nullptr;

    if (!lctx.obtain_function(fun_name, t, f))
    {
        throw std::runtime_error("Function not found: " + fun_name);
    }

    auto ft = static_cast<v_type_function_t *>(t);

    auto count = ft->param_count();

    larg.types.resize(count);

    std::copy_n(ft->param_types(), count, larg.types.data());

    if (*args) (*args)->accept(*vis, aux);

    auto v = LLVMBuildCall(gctx.builder, f, larg.values.data(), larg.values.size(), "");

    lctx.clear_temporaries();

    larg.values.clear();
    larg.types.clear();

    larg.ret_type  = ft->return_type();
    larg.ret_value = v;
}


//---------------------------------------------------------------------
//- arg_identifier
//---------------------------------------------------------------------
static
void compile_ast_arg_identifier_t(const visitor_sptr_t *vis, void *aux,
                                  const std::string *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;
    auto &larg = *lctx.args;

    LLVMValueRef v = nullptr;
    v_type_t    *t = nullptr;

    if (!lctx.obtain_identifier(*name, t, v))
    {
        throw std::runtime_error("Identifier not found: " + *name);
    }

    lctx.push_argument(t, v);
}


//---------------------------------------------------------------------
//- arg_integer
//---------------------------------------------------------------------
static
void compile_ast_arg_integer_t(const visitor_sptr_t *vis, void *aux,
                               intptr_t num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;
    auto &larg = *lctx.args;

    v_type_t *t = gctx.int_type;

    auto idx = larg.values.size();

    if (idx < larg.types.size())
    {
        if (auto at = larg.types[idx])  t = at;
        else                            larg.types[idx] = t;
    }
    else
    {
        assert(idx == larg.types.size());

        larg.types.push_back(t);
    }

    assert(idx < larg.types.size());

    bool is_reference = (t->kind() == v_type_t::k_reference);

    if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

    LLVMValueRef v;

    if (t->kind() == v_type_t::k_pointer  &&  num == 0)
    {
        v = LLVMConstPointerNull(t->llvm_type());
    }
    else
    {
        v = LLVMConstInt(t->llvm_type(), num, false);       //- ?
    }

    if (is_reference) v = lctx.make_temporary(t, v);

    larg.values.push_back(v);
}


//---------------------------------------------------------------------
//- arg_string
//---------------------------------------------------------------------
static
void compile_ast_arg_string_t(const visitor_sptr_t *vis, void *aux,
                              const std::string *str)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;
    auto &larg = *lctx.args;

    auto v = LLVMBuildGlobalStringPtr(gctx.builder, str->c_str(), "str");

    auto idx = larg.values.size();

    if (idx < larg.types.size())
    {
        if (auto t = larg.types[idx])
        {
            bool is_reference = (t->kind() == v_type_t::k_reference);

            if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

            if (t == gctx.void_ptr_type)
            {
                v = LLVMBuildPointerCast(gctx.builder, v, t->llvm_type(), "void_str");
            }

            if (is_reference) v = lctx.make_temporary(t, v);
        }
        else        //- Get type "as is"...
        {
            larg.types[idx] = gctx.char_ptr_type;
        }
    }
    else
    {
        assert(idx == larg.types.size());

        larg.types.push_back(gctx.char_ptr_type);
    }

    assert(idx < larg.types.size());

    larg.values.push_back(v);
}


//---------------------------------------------------------------------
//- arg_char
//---------------------------------------------------------------------
static
void compile_ast_arg_char_t(const visitor_sptr_t *vis, void *aux,
                            char32_t c)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;
    auto &larg = *lctx.args;

    v_type_t *t = gctx.char32_t_type;

    auto idx = larg.values.size();

    if (idx < larg.types.size())
    {
        if (auto at = larg.types[idx])  t = at;
        else                            larg.types[idx] = t;
    }
    else
    {
        assert(idx == larg.types.size());

        larg.types.push_back(t);
    }

    assert(idx < larg.types.size());

    bool is_reference = (t->kind() == v_type_t::k_reference);

    if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

    auto v = LLVMConstInt(t->llvm_type(), c, false);

    if (is_reference) v = lctx.make_temporary(t, v);

    larg.values.push_back(v);
}


//=====================================================================
//- Type calculator - just arg_identifier...
//=====================================================================
static
void typecalc_ast_arg_identifier_t(const visitor_sptr_t *vis, void *aux,
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

        DEF(ast_arg_identifier_t)

#undef DEF

        typecalc_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }

    assert(typecalc_visitor_level_zero);

    return  typecalc_visitor_level_zero;
}



