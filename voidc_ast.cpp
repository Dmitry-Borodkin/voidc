//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_ast.h"

#include "voidc_util.h"

#include <cassert>

#include <llvm-c/Analysis.h>


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
#define DEFINE_VISITOR_METHOD_TAGS(DEF) \
    DEF(ast_base_list_t) \
    DEF(ast_stmt_list_t) \
    DEF(ast_arg_list_t) \
    DEF(ast_unit_t) \
    DEF(ast_stmt_t) \
    DEF(ast_call_t) \
    DEF(ast_arg_identifier_t) \
    DEF(ast_arg_integer_t) \
    DEF(ast_arg_string_t) \
    DEF(ast_arg_char_t)

#define DEF(type) \
static v_quark_t  type##_visitor_method_tag; \
const  v_quark_t &type::visitor_method_tag = type##_visitor_method_tag;

    DEFINE_VISITOR_METHOD_TAGS(DEF)

#undef DEF


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
void ast_unit_t::compile(compile_ctx_t &cctx) const
{
    assert(cctx.args.empty());

    if (!stmt_list) return;

    std::string hdr = "unit_" + std::to_string(line) + "_" + std::to_string(column);

    std::string mod_name = hdr + "_module";
    std::string fun_name = hdr + "_action";

    cctx.module = LLVMModuleCreateWithName(mod_name.c_str());

    LLVMSetSourceFileName(cctx.module, cctx.filename.c_str(), cctx.filename.size());

    {   auto dl = LLVMCreateTargetDataLayout(cctx.target_machine);
        auto tr = LLVMGetTargetMachineTriple(cctx.target_machine);

        LLVMSetModuleDataLayout(cctx.module, dl);
        LLVMSetTarget(cctx.module, tr);

        LLVMDisposeMessage(tr);
        LLVMDisposeTargetData(dl);
    }

    LLVMTypeRef  unit_ft = LLVMFunctionType(LLVMVoidType(), nullptr, 0, false);
    LLVMValueRef unit_f  = LLVMAddFunction(cctx.module, fun_name.c_str(), unit_ft);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(unit_f, "entry");

    LLVMPositionBuilderAtEnd(cctx.builder, entry);

    stmt_list->compile(cctx);

    LLVMBuildRetVoid(cctx.builder);

    LLVMClearInsertionPosition(cctx.builder);

    //-------------------------------------------------------------
    LLVMRunPassManager(cctx.pass_manager, cctx.module);

    //-------------------------------------------------------------
    char *msg = nullptr;

    auto err = LLVMVerifyModule(cctx.module, LLVMReturnStatusAction, &msg);
    if (err)
    {
        char *txt = LLVMPrintModuleToString(cctx.module);

        printf("\n%s\n", txt);

        LLVMDisposeMessage(txt);

        printf("\n%s\n", msg);
    }

    LLVMDisposeMessage(msg);

    if (err)  exit(1);          //- Sic !!!

    //-------------------------------------------------------------
    err = LLVMTargetMachineEmitToMemoryBuffer(cctx.target_machine,
                                              cctx.module,
                                              LLVMObjectFile,
                                              &msg,
                                              &cctx.unit_buffer);

    if (err)
    {
        printf("\n%s\n", msg);

        LLVMDisposeMessage(msg);

        exit(1);                //- Sic !!!
    }

    assert(cctx.unit_buffer);


//    {   LLVMMemoryBufferRef asm_buffer = nullptr;
//
//        LLVMTargetMachineEmitToMemoryBuffer(cctx.target_machine,
//                                            cctx.module,
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


    LLVMDisposeModule(cctx.module);

    cctx.vars.clear();
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
void ast_stmt_t::compile(compile_ctx_t &cctx) const
{
    cctx.ret_name  = var_name.c_str();
    cctx.ret_value = nullptr;

    call->compile(cctx);

    if (cctx.ret_name[0])   cctx.vars[var_name] = cctx.ret_value;
}


//---------------------------------------------------------------------
//- call
//---------------------------------------------------------------------
void ast_call_t::compile(compile_ctx_t &cctx) const
{
    assert(cctx.args.empty());

    if (cctx.intrinsics.count(fun_name))
    {
        cctx.intrinsics[fun_name](&cctx, &arg_list);

        return;
    }

    LLVMValueRef f  = nullptr;
    LLVMTypeRef  ft = nullptr;

    bool ok = cctx.find_function(fun_name, ft, f);

    if (!ok)  puts(fun_name.c_str());

    assert(ok && "function not found");

    cctx.arg_types.resize(LLVMCountParamTypes(ft));

    LLVMGetParamTypes(ft, cctx.arg_types.data());

    if (arg_list) arg_list->compile(cctx);

    auto v = LLVMBuildCall(cctx.builder, f, cctx.args.data(), cctx.args.size(), cctx.ret_name);

    cctx.args.clear();
    cctx.arg_types.clear();

    cctx.ret_value = v;
}


//---------------------------------------------------------------------
//- arg_identifier
//---------------------------------------------------------------------
void ast_arg_identifier_t::compile(compile_ctx_t &cctx) const
{
    LLVMValueRef v = cctx.find_identifier(name);

    if (!v)  puts(name.c_str());

    assert(v && "identifier not found");

    auto idx = cctx.args.size();

    if (idx < cctx.arg_types.size())
    {
        auto void_ptr_type = LLVMPointerType(cctx.void_type, 0);

        auto at = cctx.arg_types[idx];
        auto vt = LLVMTypeOf(v);

        if (at != vt  &&
            at == void_ptr_type  &&
            LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
        {
            v = LLVMBuildPointerCast(cctx.builder, v, void_ptr_type, name.c_str());
        }
    }

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_integer
//---------------------------------------------------------------------
void ast_arg_integer_t::compile(compile_ctx_t &cctx) const
{
    auto idx = cctx.args.size();

    LLVMTypeRef t = cctx.int_type;

    if (idx < cctx.arg_types.size())
    {
        t = cctx.arg_types[idx];
    }

    LLVMValueRef v;

    if (LLVMGetTypeKind(t) == LLVMPointerTypeKind  &&  number == 0)
    {
        v = LLVMConstPointerNull(t);
    }
    else
    {
        v = LLVMConstInt(t, number, false);     //- ?
    }

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_string
//---------------------------------------------------------------------
static
char get_raw_character(const char * &p)
{
    assert(*p);

    char c = *p++;

    if (c == '\\')
    {
        c = *p++;

        switch(c)
        {
        case 'n':   c = '\n'; break;
        case 'r':   c = '\r'; break;
        case 't':   c = '\t'; break;
        }
    }

    return  c;
}

//---------------------------------------------------------------------
static inline
std::string make_arg_string(const std::string &vstr)
{
    const char *str = vstr.c_str();

    size_t len = 0;

    for (auto p=str; *p; ++p)
    {
        if (*p == '\\') ++p;        //- ?!?!?

        ++len;
    }

    std::string ret(len, ' ');

    char *dst = ret.data();

    for (int i=0; i<len; ++i) dst[i] = get_raw_character(str);

    return ret;
}

//---------------------------------------------------------------------
ast_arg_string_t::ast_arg_string_t(const std::string &vstr)
  : string(make_arg_string(vstr))
{}


//---------------------------------------------------------------------
void ast_arg_string_t::compile(compile_ctx_t &cctx) const
{
    auto v = LLVMBuildGlobalStringPtr(cctx.builder, string.c_str(), "str");

    auto idx = cctx.args.size();

    if (idx < cctx.arg_types.size())
    {
        auto void_ptr_type = LLVMPointerType(cctx.void_type, 0);

        auto at = cctx.arg_types[idx];

        if (at == void_ptr_type)
        {
            v = LLVMBuildPointerCast(cctx.builder, v, void_ptr_type, "void_str");
        }
    }

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
//- arg_char
//---------------------------------------------------------------------
void ast_arg_char_t::compile(compile_ctx_t &cctx) const
{
    auto v = LLVMConstInt(cctx.int_type, c, false);      //- ?

    cctx.args.push_back(v);
}


//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
VOIDC_DLLEXPORT_BEGIN_FUNCTION

//-----------------------------------------------------------------
#define DEF(name) \
    VOIDC_DEFINE_INITIALIZE_IMPL(ast_##name##_ptr_t, v_ast_initialize_##name##_impl) \
    VOIDC_DEFINE_RESET_IMPL(ast_##name##_ptr_t, v_ast_reset_##name##_impl) \
    VOIDC_DEFINE_COPY_IMPL(ast_##name##_ptr_t, v_ast_copy_##name##_impl) \
    VOIDC_DEFINE_MOVE_IMPL(ast_##name##_ptr_t, v_ast_move_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(ast_##name##_ptr_t, v_ast_std_any_get_pointer_##name##_impl) \
    VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(ast_##name##_ptr_t, v_ast_std_any_set_pointer_##name##_impl)

    DEF(base)
    DEF(base_list)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)

#undef DEF


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void v_ast_make_unit(ast_unit_ptr_t *ret, const ast_stmt_list_ptr_t *stmt_list, int line, int column)
{
    *ret = std::make_shared<const ast_unit_t>(*stmt_list, line, column);
}

void v_ast_unit_get_stmt_list(const ast_unit_ptr_t *ptr, ast_stmt_list_ptr_t *list)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    *list = r.stmt_list;
}

int v_ast_unit_get_line(const ast_unit_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    return r.line;
}

int v_ast_unit_get_column(const ast_unit_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_unit_t &>(**ptr);

    return r.column;
}

//------------------------------------------------------------------
void v_ast_make_stmt_list(ast_stmt_list_ptr_t *ret, const ast_stmt_list_ptr_t *list, const ast_stmt_ptr_t *stmt)
{
    *ret = std::make_shared<const ast_stmt_list_t>(*list, *stmt);
}

int v_ast_stmt_list_get_size(const ast_stmt_list_ptr_t *ptr)
{
    return (*ptr)->data.size();
}

void v_ast_stmt_list_get_stmt(const ast_stmt_list_ptr_t *ptr, int i, ast_stmt_ptr_t *ret)
{
    *ret = (*ptr)->data[i];
}

//------------------------------------------------------------------
void v_ast_make_stmt(ast_stmt_ptr_t *ret, const std::string *var, const ast_call_ptr_t *call)
{
    *ret = std::make_shared<const ast_stmt_t>(*var, *call);
}

void v_ast_stmt_get_var_name(const ast_stmt_ptr_t *ptr, std::string *var)
{
    auto &r = dynamic_cast<const ast_stmt_t &>(**ptr);

    *var = r.var_name;
}

void v_ast_stmt_get_call(const ast_stmt_ptr_t *ptr, ast_call_ptr_t *call)
{
    auto &r = dynamic_cast<const ast_stmt_t &>(**ptr);

    *call = r.call;
}

//------------------------------------------------------------------
void v_ast_make_call(ast_call_ptr_t *ret, const std::string *fun, const ast_arg_list_ptr_t *list)
{
    *ret = std::make_shared<const ast_call_t>(*fun, *list);
}

void v_ast_call_get_fun_name(const ast_call_ptr_t *ptr, std::string *fun)
{
    auto &r = dynamic_cast<const ast_call_t &>(**ptr);

    *fun = r.fun_name;
}

void v_ast_call_get_arg_list(const ast_call_ptr_t *ptr, ast_arg_list_ptr_t *list)
{
    auto &r = dynamic_cast<const ast_call_t &>(**ptr);

    *list = r.arg_list;
}

//------------------------------------------------------------------
void v_ast_make_arg_list(ast_arg_list_ptr_t *ret, const ast_argument_ptr_t *list, int count)
{
    *ret = std::make_shared<const ast_arg_list_t>(list, count);
}

int v_ast_arg_list_get_count(const ast_arg_list_ptr_t *ptr)
{
    return  int((*ptr)->data.size());
}

void v_ast_arg_list_get_args(const ast_arg_list_ptr_t *ptr, ast_argument_ptr_t *ret)
{
    std::copy((*ptr)->data.begin(), (*ptr)->data.end(), ret);
}

//------------------------------------------------------------------
void v_ast_make_arg_identifier(ast_argument_ptr_t *ret, const std::string *name)
{
    *ret = std::make_shared<const ast_arg_identifier_t>(*name);
}

void v_ast_arg_identifier_get_name(const ast_argument_ptr_t *ptr, std::string *name)
{
    auto &r = dynamic_cast<const ast_arg_identifier_t &>(**ptr);

    *name = r.name;
}

//------------------------------------------------------------------
void v_ast_make_arg_integer(ast_argument_ptr_t *ret, intptr_t number)
{
    *ret = std::make_shared<const ast_arg_integer_t>(number);
}

intptr_t v_ast_arg_integer_get_number(const ast_argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_arg_integer_t &>(**ptr);

    return  r.number;
}

//------------------------------------------------------------------
void v_ast_make_arg_string(ast_argument_ptr_t *ret, const std::string *string)
{
    *ret = std::make_shared<const ast_arg_string_t>(*string);
}

void v_ast_arg_string_get_string(const ast_argument_ptr_t *ptr, std::string *string)
{
    auto &r = dynamic_cast<const ast_arg_string_t &>(**ptr);

    *string = r.string;
}

//------------------------------------------------------------------
void v_ast_make_arg_char(ast_argument_ptr_t *ret, char32_t c)
{
    *ret = std::make_shared<const ast_arg_char_t>(c);
}

char32_t v_ast_arg_char_get_char(const ast_argument_ptr_t *ptr)
{
    auto &r = dynamic_cast<const ast_arg_char_t &>(**ptr);

    return  r.c;
}


//------------------------------------------------------------------
//- Generics ...
//------------------------------------------------------------------
void v_ast_make_generic(ast_generic_ptr_t *ret, const ast_generic_vtable *vtab, void *obj)
{
    *ret = std::make_shared<const ast_generic_t>(vtab, obj);
}

//--------------------------------------------------------------
void v_ast_make_unit_generic(ast_unit_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_unit_generic_t>(*gen);
}

void v_ast_make_stmt_generic(ast_stmt_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_stmt_generic_t>(*gen);
}

void v_ast_make_call_generic(ast_call_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_call_generic_t>(*gen);
}

void v_ast_make_argument_generic(ast_argument_ptr_t *ret, const ast_generic_ptr_t *gen)
{
    *ret = std::make_shared<const ast_argument_generic_t>(*gen);
}

//------------------------------------------------------------------
const ast_generic_vtable *
v_ast_generic_get_vtable(const ast_generic_ptr_t *ptr)
{
    return (*ptr)->vtable;
}

const void *
v_ast_generic_get_object(const ast_generic_ptr_t *ptr)
{
    return (*ptr)->object;
}


//------------------------------------------------------------------
//- Casts ...
//------------------------------------------------------------------
#define DEF_UPCAST(name) \
void v_ast_upcast_##name##_impl(ast_base_ptr_t *ret, const ast_##name##_ptr_t *ptr) \
{ \
    *ret = std::static_pointer_cast<const ast_base_t>(*ptr); \
}

#define DEF_DOWNCAST(name) \
void v_ast_downcast_##name##_impl(const ast_base_ptr_t *ptr, ast_##name##_ptr_t *ret) \
{ \
    *ret = std::dynamic_pointer_cast<const ast_##name##_t>(*ptr); \
}

#define DEF(name) \
    DEF_UPCAST(name) \
    DEF_DOWNCAST(name)

    DEF(base)           //- ?...
    DEF(base_list)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)

#undef DEF

#undef DEF_DOWNCAST
#undef DEF_UPCAST


//-----------------------------------------------------------------
//- Visitors ...
//-----------------------------------------------------------------
#define DEF(type) \
void v_ast_visitor_set_method_##type(visitor_ptr_t *dst, const visitor_ptr_t *src, type::visitor_method_t method) \
{ \
    auto visitor = (*src)->set_void_method(type##_visitor_method_tag, (void *)method); \
    *dst = std::make_shared<const voidc_visitor_t>(visitor); \
}

    DEFINE_VISITOR_METHOD_TAGS(DEF)

#undef DEF


//-----------------------------------------------------------------
void v_ast_accept_visitor(const ast_base_ptr_t *object, const visitor_ptr_t *visitor)
{
    (*object)->accept(*visitor);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END


//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void v_ast_static_initialize(void)
{
    static_assert((sizeof(ast_base_ptr_t) % sizeof(intptr_t)) == 0);

    auto content_type = LLVMArrayType(compile_ctx_t::intptr_t_type, sizeof(ast_base_ptr_t)/sizeof(intptr_t));

    auto gctx = LLVMGetGlobalContext();

#define DEF(name) \
    static_assert(sizeof(ast_base_ptr_t) == sizeof(ast_##name##_ptr_t)); \
    auto opaque_##name##_ptr_type = LLVMStructCreateNamed(gctx, "struct.v_ast_opaque_" #name "_ptr"); \
    LLVMStructSetBody(opaque_##name##_ptr_type, &content_type, 1, false); \
    v_add_symbol("v_ast_opaque_" #name "_ptr", compile_ctx_t::LLVMOpaqueType_type, (void *)opaque_##name##_ptr_type);

    DEF(base)
    DEF(base_list)

    DEF(unit)
    DEF(stmt_list)
    DEF(stmt)
    DEF(call)
    DEF(arg_list)
    DEF(argument)

    DEF(generic)

#undef DEF

#define DEF(type) \
    type##_visitor_method_tag = v_quark_from_string(#type);

    DEFINE_VISITOR_METHOD_TAGS(DEF)

#undef DEF
}


//---------------------------------------------------------------------
void v_ast_static_terminate(void)
{
}


