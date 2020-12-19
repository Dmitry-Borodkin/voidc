//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_compiler.h"

#include "voidc_types.h"
#include "voidc_target.h"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
visitor_ptr_t voidc_compiler;


//---------------------------------------------------------------------
//- AST Visitor - Compiler (level 0) ...
//---------------------------------------------------------------------
static void compile_ast_stmt_list_t(const visitor_ptr_t *vis, void *aux, size_t count, bool start) {}
static void compile_ast_arg_list_t(const visitor_ptr_t *vis, void *aux, size_t count, bool start) {}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_ptr_t *vis, void *aux,
                        const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    if (!*stmt_list)  return;

    auto saved_target = voidc_global_ctx_t::target;

    voidc_global_ctx_t::target = voidc_global_ctx_t::voidc;

    auto &gctx = *voidc_global_ctx_t::target;                           //- target == voidc (!)
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);     //- Sic!

    assert(lctx.args.empty());

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
void compile_ast_stmt_t(const visitor_ptr_t *vis, void *aux,
                        const std::string *vname, const ast_call_ptr_t *call)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &var_name = *vname;

    lctx.ret_name  = var_name.c_str();
    lctx.ret_type  = nullptr;
    lctx.ret_value = nullptr;

    (*call)->accept(*vis, aux);

    if (lctx.ret_name[0])   lctx.vars = lctx.vars.set(var_name, {lctx.ret_type, lctx.ret_value});
}


//---------------------------------------------------------------------
//- call
//---------------------------------------------------------------------
static
void compile_ast_call_t(const visitor_ptr_t *vis, void *aux,
                        const std::string *fname, const ast_arg_list_ptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    assert(lctx.args.empty());

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

    lctx.arg_types.resize(count);

    std::copy_n(ft->param_types(), count, lctx.arg_types.data());

    if (*args) (*args)->accept(*vis, aux);

    auto v = LLVMBuildCall(gctx.builder, f, lctx.args.data(), lctx.args.size(), lctx.ret_name);

    lctx.args.clear();
    lctx.arg_types.clear();

    lctx.ret_type  = ft->return_type();
    lctx.ret_value = v;
}


//---------------------------------------------------------------------
//- arg_identifier
//---------------------------------------------------------------------
static
void compile_ast_arg_identifier_t(const visitor_ptr_t *vis, void *aux,
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

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        auto at = lctx.arg_types[idx];

        if (at != t  &&
            at == gctx.void_ptr_type  &&
            t->method_tag() == v_type_pointer_visitor_method_tag)
        {
            v = LLVMBuildPointerCast(gctx.builder, v, at->llvm_type(), name->c_str());
        }
    }
    else
    {
        assert(idx == lctx.arg_types.size());

        lctx.arg_types.push_back(t);
    }

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_integer
//---------------------------------------------------------------------
static
void compile_ast_arg_integer_t(const visitor_ptr_t *vis, void *aux,
                               intptr_t num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t *t = gctx.int_type;

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        t = lctx.arg_types[idx];
    }
    else
    {
        assert(idx == lctx.arg_types.size());

        lctx.arg_types.push_back(t);
    }

    LLVMValueRef v;

    if (t->method_tag() == v_type_pointer_visitor_method_tag  &&  num == 0)
    {
        v = LLVMConstPointerNull(t->llvm_type());
    }
    else
    {
        v = LLVMConstInt(t->llvm_type(), num, false);       //- ?
    }

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_string
//---------------------------------------------------------------------
static
void compile_ast_arg_string_t(const visitor_ptr_t *vis, void *aux,
                              const std::string *str)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto v = LLVMBuildGlobalStringPtr(gctx.builder, str->c_str(), "str");

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        auto at = lctx.arg_types[idx];

        if (at == gctx.void_ptr_type)
        {
            v = LLVMBuildPointerCast(gctx.builder, v, at->llvm_type(), "void_str");
        }
    }
    else
    {
        assert(idx == lctx.arg_types.size());

        lctx.arg_types.push_back(gctx.char_ptr_type);
    }

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_char
//---------------------------------------------------------------------
static
void compile_ast_arg_char_t(const visitor_ptr_t *vis, void *aux,
                            char32_t c)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t *t = gctx.char32_t_type;

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        t = lctx.arg_types[idx];
    }
    else
    {
        assert(idx == lctx.arg_types.size());

        lctx.arg_types.push_back(t);
    }

    auto v = LLVMConstInt(t->llvm_type(), c, false);

    lctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_type
//---------------------------------------------------------------------
static
void compile_ast_arg_type_t(const visitor_ptr_t *vis, void *aux,
                            v_type_t *type)
{
    assert(false && "This can not happen!");
}


//---------------------------------------------------------------------
//- Compiler visitor
//---------------------------------------------------------------------
static
visitor_ptr_t compile_visitor_level_zero;

visitor_ptr_t
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






