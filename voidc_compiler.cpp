//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_compiler.h"

#include "voidc_target.h"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
visitor_ptr_t voidc_compiler;


//---------------------------------------------------------------------
//- AST Visitor - Compiler (level 0) ...
//---------------------------------------------------------------------
static void compile_ast_stmt_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}
static void compile_ast_arg_list_t(const visitor_ptr_t *vis, size_t count, bool start) {}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_ptr_t *vis, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    auto &gctx = *voidc_global_ctx_t::voidc;
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);

    assert(lctx.args.empty());

    if (!*stmt_list)  return;

    auto saved_module = lctx.module;

    lctx.prepare_unit_action(line, column);

    (*stmt_list)->accept(*vis);

    lctx.finish_unit_action();

    lctx.module = saved_module;
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_ast_stmt_t(const visitor_ptr_t *vis, const std::string *vname, const ast_call_ptr_t *call)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto &var_name = *vname;

    lctx.ret_name  = var_name.c_str();
    lctx.ret_value = nullptr;

    (*call)->accept(*vis);

    if (lctx.ret_name[0])   lctx.vars = lctx.vars.set(var_name, lctx.ret_value);
}


//---------------------------------------------------------------------
//- call
//---------------------------------------------------------------------
static
void compile_ast_call_t(const visitor_ptr_t *vis, const std::string *fname, const ast_arg_list_ptr_t *args)
{
    auto &builder = voidc_global_ctx_t::builder;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

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
    auto &lctx = *gctx.local_ctx;

    LLVMValueRef v = lctx.find_identifier(*name);
    if (!v)
    {
        throw std::runtime_error("Identifier not found: " + *name);
    }

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        auto at = lctx.arg_types[idx];
        auto vt = LLVMTypeOf(v);

        auto void_ptr_type = voidc_global_ctx_t::voidc->void_ptr_type;

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
    auto &lctx = *gctx.local_ctx;

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
    auto &lctx = *gctx.local_ctx;

    auto v = LLVMBuildGlobalStringPtr(builder, str->c_str(), "str");

    auto idx = lctx.args.size();

    if (idx < lctx.arg_types.size())
    {
        auto at = lctx.arg_types[idx];

        auto void_ptr_type = voidc_global_ctx_t::voidc->void_ptr_type;

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
    auto &lctx = *gctx.local_ctx;

    auto v = LLVMConstInt(gctx.int_type, c, false);      //- int ?

    lctx.args.push_back(v);
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






