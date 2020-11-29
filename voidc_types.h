//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TYPES_H
#define VOIDC_TYPES_H

#include <utility>
#include <map>
#include <vector>
#include <memory>
#include <string>

#include <llvm-c/Types.h>


//---------------------------------------------------------------------
class voidc_types_ctx_t;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
struct v_type_t
{
    virtual ~v_type_t();

    enum kind_t
    {
        k_void,         //- ...

        k_f16,          //- "half"
        k_f32,          //- "float"
        k_f64,          //- "double"
        k_f128,         //- "fp128"

        k_sint,         //- Signed integer
        k_uint,         //- Unsigned integer

        k_function,     //- ...
        k_pointer,      //- Typed(!) pointer ((void *) - ok!)
        k_struct,       //- (Un-)named structs ("tuples" in fact)
        k_array,        //- ...

        k_fvector,      //- Fixed vector
        k_svector       //- Scalable vector
    };

    virtual kind_t kind(void) const = 0;

public:
    LLVMTypeRef llvm_type(void)
    {
        if (!cached_llvm_type)  cached_llvm_type = obtain_llvm_type();

        return cached_llvm_type;
    }

protected:
    voidc_types_ctx_t &context;

    explicit v_type_t(voidc_types_ctx_t &ctx)
      : context(ctx)
    {}

protected:
    virtual LLVMTypeRef obtain_llvm_type(void) const = 0;

private:
    LLVMTypeRef cached_llvm_type = nullptr;

private:
    v_type_t(const v_type_t &) = delete;
    v_type_t &operator=(const v_type_t &) = delete;
};

//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
struct v_type_tag_t : public v_type_t
{
    enum { type_tag = tag };

    kind_t kind(void) const override
    {
        return tag;
    }

protected:
    explicit v_type_tag_t(voidc_types_ctx_t &ctx)
      : v_type_t(ctx)
    {}

private:
    v_type_tag_t(const v_type_tag_t &) = delete;
    v_type_tag_t &operator=(const v_type_tag_t &) = delete;
};


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
#define DEFINE_SIMPLE_TYPE(key) \
class v_type_##key##_t : public v_type_tag_t<v_type_t::k_##key> \
{ \
    friend class voidc_types_ctx_t; \
\
    explicit v_type_##key##_t(voidc_types_ctx_t &ctx) \
      : v_type_tag_t(ctx) \
    {} \
\
    LLVMTypeRef obtain_llvm_type(void) const override; \
\
    v_type_##key##_t(const v_type_##key##_t &) = delete; \
    v_type_##key##_t &operator=(const v_type_##key##_t &) = delete; \
};

DEFINE_SIMPLE_TYPE(void)
DEFINE_SIMPLE_TYPE(f16)
DEFINE_SIMPLE_TYPE(f32)
DEFINE_SIMPLE_TYPE(f64)
DEFINE_SIMPLE_TYPE(f128)

#undef DEFINE_SIMPLE_TYPE


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
class v_type_integer_base_t : public v_type_tag_t<tag>
{
    friend class voidc_types_ctx_t;

    const unsigned &bits;

    explicit v_type_integer_base_t(voidc_types_ctx_t &ctx, const unsigned &_bits)
      : v_type_tag_t<tag>(ctx),
        bits(_bits)
    {}

    LLVMTypeRef obtain_llvm_type(void) const override;

    v_type_integer_base_t(const v_type_integer_base_t &) = delete;
    v_type_integer_base_t &operator=(const v_type_integer_base_t &) = delete;
};

using v_type_sint_t = v_type_integer_base_t<v_type_t::k_sint>;
using v_type_uint_t = v_type_integer_base_t<v_type_t::k_uint>;

extern template LLVMTypeRef v_type_sint_t::obtain_llvm_type(void) const;
extern template LLVMTypeRef v_type_uint_t::obtain_llvm_type(void) const;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class v_type_function_t : public v_type_tag_t<v_type_t::k_function>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<std::vector<v_type_t *>, bool>;

    const key_t &key;

    explicit v_type_function_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t(ctx),
        key(_key)
    {}

    LLVMTypeRef obtain_llvm_type(void) const override;

    v_type_function_t(const v_type_function_t &) = delete;
    v_type_function_t &operator=(const v_type_function_t &) = delete;
};


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class v_type_pointer_t : public v_type_tag_t<v_type_t::k_pointer>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_type_t *, unsigned>;

    const key_t &key;

    explicit v_type_pointer_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t(ctx),
        key(_key)
    {}

    LLVMTypeRef obtain_llvm_type(void) const override;

    v_type_pointer_t(const v_type_pointer_t &) = delete;
    v_type_pointer_t &operator=(const v_type_pointer_t &) = delete;
};


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class v_type_struct_t : public v_type_tag_t<v_type_t::k_struct>
{
    friend class voidc_types_ctx_t;

    using name_key_t = std::string;
    using body_key_t = std::pair<std::vector<v_type_t *>, bool>;

    const name_key_t *name = nullptr;
    const body_key_t *body = nullptr;

    explicit v_type_struct_t(voidc_types_ctx_t &ctx, const name_key_t &_name)
      : v_type_tag_t(ctx),
        name(&_name)
    {}

    explicit v_type_struct_t(voidc_types_ctx_t &ctx, const body_key_t &_body)
      : v_type_tag_t(ctx),
        body(&_body)
    {}

    LLVMTypeRef obtain_llvm_type(void) const override;

    v_type_struct_t(const v_type_struct_t &) = delete;
    v_type_struct_t &operator=(const v_type_struct_t &) = delete;
};


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class v_type_array_t : public v_type_tag_t<v_type_t::k_array>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_type_t *, uint64_t>;

    const key_t &key;

    explicit v_type_array_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t(ctx),
        key(_key)
    {}

    LLVMTypeRef obtain_llvm_type(void) const override;

    v_type_array_t(const v_type_array_t &) = delete;
    v_type_array_t &operator=(const v_type_array_t &) = delete;
};


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
class v_type_vector_base_t : public v_type_tag_t<tag>
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_type_t *, unsigned>;

    const key_t &key;

    explicit v_type_vector_base_t(voidc_types_ctx_t &ctx, const key_t &_key)
      : v_type_tag_t<tag>(ctx),
        key(_key)
    {}

    LLVMTypeRef obtain_llvm_type(void) const override;

    v_type_vector_base_t(const v_type_vector_base_t &) = delete;
    v_type_vector_base_t &operator=(const v_type_vector_base_t &) = delete;
};

using v_type_fvector_t = v_type_vector_base_t<v_type_t::k_fvector>;
using v_type_svector_t = v_type_vector_base_t<v_type_t::k_svector>;

template<> extern LLVMTypeRef v_type_fvector_t::obtain_llvm_type(void) const;
template<> extern LLVMTypeRef v_type_svector_t::obtain_llvm_type(void) const;


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_types_ctx_t
{

public:
    voidc_types_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size);
    ~voidc_types_ctx_t() = default;

public:
    const LLVMContextRef llvm_ctx;
    const LLVMModuleRef  llvm_mod;

    const LLVMTypeRef opaque_void_type;     //- For (void *) ...

public:
    v_type_void_t *get_void_type(void)  { return void_type.get(); }

    v_type_f16_t  *get_f16_type(void)   { return f16_type.get(); }
    v_type_f32_t  *get_f32_type(void)   { return f32_type.get(); }
    v_type_f64_t  *get_f64_type(void)   { return f64_type.get(); }
    v_type_f128_t *get_f128_type(void)  { return f128_type.get(); }

    v_type_sint_t *get_sint_type(unsigned bits);
    v_type_uint_t *get_uint_type(unsigned bits);

    v_type_function_t *get_function_type(v_type_t *ret, v_type_t **args, unsigned count, bool var_args=false);
    v_type_pointer_t  *get_pointer_type(v_type_t *et, unsigned addr_space=0);

    v_type_struct_t *get_struct_type(const std::string &name);
    v_type_struct_t *get_struct_type(v_type_t **elts, unsigned count, bool packed=false);

    v_type_array_t *get_array_type(v_type_t *et, uint64_t count);

    v_type_fvector_t *get_fvector_type(v_type_t *et, unsigned count);
    v_type_svector_t *get_svector_type(v_type_t *et, unsigned count);

private:
    std::unique_ptr<v_type_void_t> void_type;

    std::unique_ptr<v_type_f16_t>  f16_type;
    std::unique_ptr<v_type_f32_t>  f32_type;
    std::unique_ptr<v_type_f64_t>  f64_type;
    std::unique_ptr<v_type_f128_t> f128_type;

    std::map<unsigned, std::unique_ptr<v_type_sint_t>> sint_types;
    std::map<unsigned, std::unique_ptr<v_type_uint_t>> uint_types;

    std::map<v_type_function_t::key_t, std::unique_ptr<v_type_function_t>> function_types;
    std::map<v_type_pointer_t::key_t,  std::unique_ptr<v_type_pointer_t>>  pointer_types;

    std::map<v_type_struct_t::name_key_t, std::unique_ptr<v_type_struct_t>> named_struct_types;
    std::map<v_type_struct_t::body_key_t, std::unique_ptr<v_type_struct_t>> anon_struct_types;

    std::map<v_type_array_t::key_t, std::unique_ptr<v_type_array_t>> array_types;

    std::map<v_type_fvector_t::key_t, std::unique_ptr<v_type_fvector_t>> fvector_types;
    std::map<v_type_svector_t::key_t, std::unique_ptr<v_type_svector_t>> svector_types;

public:
    v_type_uint_t * const bool_type;
    v_type_sint_t * const char_type;
    v_type_sint_t * const short_type;
    v_type_sint_t * const int_type;
    v_type_sint_t * const long_type;
    v_type_sint_t * const long_long_type;
    v_type_sint_t * const intptr_t_type;
    v_type_uint_t * const size_t_type;
    v_type_uint_t * const char32_t_type;
};


#endif  //- VOIDC_TYPES_H
