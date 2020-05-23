#ifndef VOIDC_UTIL_H
#define VOIDC_UTIL_H

#include "voidc_llvm.h"

#include <string>
#include <map>
#include <algorithm>
#include <any>

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace utility
{

void static_initialize(void);
void static_terminate(void);

using function_dict_t = std::map<LLVMTypeRef, std::string>;

extern function_dict_t initialize_dict;
extern function_dict_t reset_dict;

extern function_dict_t copy_dict;
extern function_dict_t move_dict;

extern function_dict_t kind_dict;

extern function_dict_t std_any_get_value_dict;
extern function_dict_t std_any_get_pointer_dict;
extern function_dict_t std_any_set_value_dict;
extern function_dict_t std_any_set_pointer_dict;

//- ...


//-----------------------------------------------------------------
//- Init/reset
//-----------------------------------------------------------------
template<typename val_t>
void v_initialize_impl(val_t *ptr, int count)
{
    std::uninitialized_default_construct_n(ptr, size_t(count));
}

template<typename val_t>
void register_init_reset_impl_helper(LLVMTypeRef val_t_type, const char *fun_name,
                                     void *fun_ptr, function_dict_t &dict
                                    )
{
    LLVMTypeRef args[2];

    args[0] = LLVMPointerType(val_t_type, 0);
    args[1] = compile_ctx_t::int_type;

    v_add_symbol(fun_name, LLVMFunctionType(compile_ctx_t::void_type, args, 2, false), fun_ptr);

    dict[val_t_type] = fun_name;
}

template<typename val_t>
void register_initialize_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    register_init_reset_impl_helper<val_t>(val_t_type, fun_name,
                                           (void *)v_initialize_impl<val_t>, initialize_dict
                                          );
}

//-----------------------------------------------------------------
template<typename val_t>
void v_reset_impl(val_t *ptr, int count)
{
    for (int i=0; i<count; ++i) ptr[i].reset();
}

template<typename val_t>
void register_reset_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    register_init_reset_impl_helper<val_t>(val_t_type, fun_name,
                                           (void *)v_reset_impl<val_t>, reset_dict
                                          );
}


//-----------------------------------------------------------------
//- Copy/move
//-----------------------------------------------------------------
template<typename val_t>
void v_copy_impl(val_t *dst, const val_t *src, int count)
{
    std::copy(src, src+count, dst);
}

template<typename val_t>
void register_copy_move_impl_helper(LLVMTypeRef val_t_type, const char *fun_name,
                                    void *fun_ptr, function_dict_t &dict
                                   )
{
    LLVMTypeRef args[3];

    args[0] = LLVMPointerType(val_t_type, 0);
    args[1] = args[0];
    args[2] = compile_ctx_t::int_type;

    v_add_symbol(fun_name, LLVMFunctionType(compile_ctx_t::void_type, args, 3, false), fun_ptr);

    dict[val_t_type] = fun_name;
}

template<typename val_t>
void register_copy_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    register_copy_move_impl_helper<val_t>(val_t_type, fun_name,
                                          (void *)v_copy_impl<val_t>, copy_dict
                                         );
}

//-----------------------------------------------------------------
template<typename val_t>
void v_move_impl(val_t *dst, val_t *src, int count)
{
    std::move(src, src+count, dst);
}

template<typename val_t>
void register_move_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    register_copy_move_impl_helper<val_t>(val_t_type, fun_name,
                                          (void *)v_move_impl<val_t>, move_dict
                                         );
}


//-----------------------------------------------------------------
//- std::any ...
//-----------------------------------------------------------------
template<typename val_t>
val_t v_std_any_get_value_impl(const std::any *src)
{
    return std::any_cast<val_t>(*src);
}

extern LLVMTypeRef opaque_std_any_type;

template<typename val_t>
void register_std_any_get_value_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    auto std_any_ref_type = LLVMPointerType(opaque_std_any_type, 0);

    v_add_symbol(fun_name,
                 LLVMFunctionType(val_t_type, &std_any_ref_type, 1, false),
                 (void *)v_std_any_get_value_impl<val_t>
                );

    std_any_get_value_dict[val_t_type] = fun_name;
}

//-----------------------------------------------------------------
template<typename val_t>
val_t *v_std_any_get_pointer_impl(std::any *src)
{
    return std::any_cast<val_t>(src);
}

template<typename val_t>
void register_std_any_get_pointer_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    auto std_any_ref_type = LLVMPointerType(opaque_std_any_type, 0);
    auto ptr_t_type       = LLVMPointerType(val_t_type, 0);

    v_add_symbol(fun_name,
                 LLVMFunctionType(ptr_t_type, &std_any_ref_type, 1, false),
                 (void *)v_std_any_get_pointer_impl<val_t>
                );

    std_any_get_pointer_dict[val_t_type] = fun_name;
}

//-----------------------------------------------------------------
template<typename val_t>
void v_std_any_set_value_impl(std::any *dst, val_t v)
{
    *dst = v;
}

template<typename val_t>
void register_std_any_set_value_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    LLVMTypeRef args[2];

    args[0] = LLVMPointerType(opaque_std_any_type, 0);
    args[1] = val_t_type;

    v_add_symbol(fun_name,
                 LLVMFunctionType(compile_ctx_t::void_type, args, 2, false),
                 (void *)v_std_any_set_value_impl<val_t>
                );

    std_any_set_value_dict[val_t_type] = fun_name;
}

//-----------------------------------------------------------------
template<typename val_t>
void v_std_any_set_pointer_impl(std::any *dst, val_t *p)
{
    *dst = *p;
}

template<typename val_t>
void register_std_any_set_pointer_impl(LLVMTypeRef val_t_type, const char *fun_name)
{
    LLVMTypeRef args[2];

    args[0] = LLVMPointerType(opaque_std_any_type, 0);
    args[1] = LLVMPointerType(val_t_type, 0);

    v_add_symbol(fun_name,
                 LLVMFunctionType(compile_ctx_t::void_type, args, 2, false),
                 (void *)v_std_any_set_pointer_impl<val_t>
                );

    std_any_set_pointer_dict[val_t_type] = fun_name;
}


//-----------------------------------------------------------------
//- std::string...
//-----------------------------------------------------------------
extern LLVMTypeRef opaque_std_string_type;




//- ...


//---------------------------------------------------------------------
}   //- namespace utility



#endif  //- VOIDC_UTIL_H
