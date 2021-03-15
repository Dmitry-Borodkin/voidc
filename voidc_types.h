//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TYPES_H
#define VOIDC_TYPES_H

#include "voidc_visitor.h"

#include <utility>
#include <map>
#include <vector>
#include <memory>
#include <string>

#include <llvm-c/Types.h>


//---------------------------------------------------------------------
//- Visitor tags...
//---------------------------------------------------------------------
#define DEFINE_TYPE_VISITOR_METHOD_TAGS(DEF) \
    DEF(void)       /* ...                                      */    \
\
    DEF(f16)        /* "half"                                   */    \
    DEF(f32)        /* "float"                                  */    \
    DEF(f64)        /* "double"                                 */    \
    DEF(f128)       /* "fp128" (quad)                           */    \
\
    DEF(sint)       /* Signed integer                           */    \
    DEF(uint)       /* Unsigned integer                         */    \
\
    DEF(function)   /* ...                                      */    \
    DEF(pointer)    /* Typed(!) pointer ((void *) - ok!)        */    \
    DEF(reference)  /* References...                            */    \
    DEF(struct)     /* (Un-)named structs ("tuples" in fact)    */    \
    DEF(array)      /* ...                                      */    \
\
    DEF(fvector)    /* Fixed vector                             */    \
    DEF(svector)    /* Scalable vector                          */


#define DEF(tag) \
extern v_quark_t v_type_##tag##_visitor_method_tag;

extern "C"
{
VOIDC_DLLEXPORT_BEGIN_VARIABLE

    DEFINE_TYPE_VISITOR_METHOD_TAGS(DEF)

    extern visitor_sptr_t voidc_llvm_type_visitor;

VOIDC_DLLEXPORT_END
}

#undef DEF


//---------------------------------------------------------------------
//- Base class for voidc`s own types
//---------------------------------------------------------------------
struct v_type_t
{
    virtual ~v_type_t();

public:
    virtual void accept(const visitor_sptr_t &visitor, void *aux) const = 0;

public:
    virtual v_quark_t method_tag(void) const = 0;

protected:
    explicit v_type_t() {}

private:
    v_type_t(const v_type_t &) = delete;
    v_type_t &operator=(const v_type_t &) = delete;

protected:
    LLVMTypeRef cached_llvm_type = nullptr;

public:
    LLVMTypeRef llvm_type(void)
    {
        if (!cached_llvm_type)
        {
            accept(voidc_llvm_type_visitor, &cached_llvm_type);
        }

        return cached_llvm_type;
    }
};

//---------------------------------------------------------------------
#define TYPE_VISITOR_TAG(tag) \
    v_quark_t method_tag(void) const override { return v_type_##tag##_visitor_method_tag; }


//---------------------------------------------------------------------
//- "Simple" types: "void" and floating points...
//---------------------------------------------------------------------
class v_type_simple_t : public v_type_t
{
    friend class voidc_types_ctx_t;

protected:
    explicit v_type_simple_t()
      : v_type_t()
    {}

private:
    v_type_simple_t(const v_type_simple_t &) = delete;
    v_type_simple_t &operator=(const v_type_simple_t &) = delete;

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux);
    }
};

//---------------------------------------------------------------------
template<const v_quark_t &tag>
class v_type_simple_tag_t : public v_type_simple_t
{
    friend class voidc_types_ctx_t;

    explicit v_type_simple_tag_t()
      : v_type_simple_t()
    {}

    v_type_simple_tag_t(const v_type_simple_tag_t &) = delete;
    v_type_simple_tag_t &operator=(const v_type_simple_tag_t &) = delete;

public:
    v_quark_t method_tag(void) const override { return tag; }
};

//---------------------------------------------------------------------
using v_type_void_t = v_type_simple_tag_t<v_type_void_visitor_method_tag>;
using v_type_f16_t  = v_type_simple_tag_t<v_type_f16_visitor_method_tag>;
using v_type_f32_t  = v_type_simple_tag_t<v_type_f32_visitor_method_tag>;
using v_type_f64_t  = v_type_simple_tag_t<v_type_f64_visitor_method_tag>;
using v_type_f128_t = v_type_simple_tag_t<v_type_f128_visitor_method_tag>;


//---------------------------------------------------------------------
//- Integer types: signed/unsigned
//---------------------------------------------------------------------
class v_type_integer_t : public v_type_t
{
    friend class voidc_types_ctx_t;

    const unsigned &bits;

protected:
    explicit v_type_integer_t(const unsigned &_bits)
      : v_type_t(),
        bits(_bits)
    {}

private:
    v_type_integer_t(const v_type_integer_t &) = delete;
    v_type_integer_t &operator=(const v_type_integer_t &) = delete;

public:
    bool is_signed(void) const { return (method_tag() == v_type_sint_visitor_method_tag); }

    unsigned width(void) const { return bits; }

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     unsigned bits, bool _signed);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, width(), is_signed());
    }
};

//---------------------------------------------------------------------
template<const v_quark_t &tag>
class v_type_integer_tag_t : public v_type_integer_t
{
    friend class voidc_types_ctx_t;

    explicit v_type_integer_tag_t(const unsigned &_bits)
      : v_type_integer_t(_bits)
    {}

    v_type_integer_tag_t(const v_type_integer_tag_t &) = delete;
    v_type_integer_tag_t &operator=(const v_type_integer_tag_t &) = delete;

public:
    v_quark_t method_tag(void) const override { return tag; }
};

//---------------------------------------------------------------------
using v_type_sint_t = v_type_integer_tag_t<v_type_sint_visitor_method_tag>;
using v_type_uint_t = v_type_integer_tag_t<v_type_uint_visitor_method_tag>;


//---------------------------------------------------------------------
//- Function types
//---------------------------------------------------------------------
class v_type_function_t : public v_type_t
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<std::vector<v_type_t *>, bool>;

    const key_t &key;

    explicit v_type_function_t(const key_t &_key)
      : v_type_t(),
        key(_key)
    {}

    v_type_function_t(const v_type_function_t &) = delete;
    v_type_function_t &operator=(const v_type_function_t &) = delete;

public:
    bool is_var_arg(void) const { return key.second; }

    v_type_t *return_type(void) const { return key.first[0]; }

    unsigned param_count(void) const { return unsigned(key.first.size() - 1); }

    v_type_t * const *param_types(void) const { return key.first.data() + 1; }

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     v_type_t *ret, v_type_t * const *args, unsigned count, bool var_arg);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, return_type(), param_types(), param_count(), is_var_arg());
    }

    TYPE_VISITOR_TAG(function)
};


//---------------------------------------------------------------------
//- Pointer/reference types
//---------------------------------------------------------------------
class v_type_refptr_t : public v_type_t
{
    friend class voidc_types_ctx_t;

protected:
    using key_t = std::pair<v_type_t *, unsigned>;

    const key_t &key;

    explicit v_type_refptr_t(const key_t &_key)
      : v_type_t(),
        key(_key)
    {}

private:
    v_type_refptr_t(const v_type_refptr_t &) = delete;
    v_type_refptr_t &operator=(const v_type_refptr_t &) = delete;

public:
    v_type_t *element_type(void) const { return key.first; }

    unsigned address_space(void) const { return key.second; }

    bool is_reference(void) const { return (method_tag() == v_type_reference_visitor_method_tag); }

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     v_type_t *elt, unsigned addr_space, bool reference);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, element_type(), address_space(), is_reference());
    }
};

//---------------------------------------------------------------------
template<const v_quark_t &tag>
class v_type_refptr_tag_t : public v_type_refptr_t
{
    friend class voidc_types_ctx_t;

    explicit v_type_refptr_tag_t(const v_type_refptr_t::key_t &_key)
      : v_type_refptr_t(_key)
    {}

    v_type_refptr_tag_t(const v_type_refptr_tag_t &) = delete;
    v_type_refptr_tag_t &operator=(const v_type_refptr_tag_t &) = delete;

public:
    v_quark_t method_tag(void) const override { return tag; }
};

//---------------------------------------------------------------------
using v_type_pointer_t   = v_type_refptr_tag_t<v_type_pointer_visitor_method_tag>;
using v_type_reference_t = v_type_refptr_tag_t<v_type_reference_visitor_method_tag>;


//---------------------------------------------------------------------
//- Structure types: named/unnamed...
//---------------------------------------------------------------------
class v_type_struct_t : public v_type_t
{
    friend class voidc_types_ctx_t;

    using name_key_t = std::string;
    using body_key_t = std::pair<std::vector<v_type_t *>, bool>;

    const name_key_t *name_key = nullptr;
    const body_key_t *body_key = nullptr;

    explicit v_type_struct_t(const name_key_t &_name)
      : v_type_t(),
        name_key(&_name)
    {}

    explicit v_type_struct_t(const body_key_t &_body)
      : v_type_t(),
        body_key(&_body)
    {}

    v_type_struct_t(const v_type_struct_t &) = delete;
    v_type_struct_t &operator=(const v_type_struct_t &) = delete;

public:
    const char *name(void) const { return (name_key ? name_key->c_str() : nullptr); }

    bool is_opaque(void) const { return !body_key; }

    void set_body(v_type_t **elts, unsigned count, bool packed=false);

    unsigned element_count(void) const { return unsigned(body_key->first.size()); }

    v_type_t * const *element_types(void) const { return body_key->first.data(); }

    bool is_packed(void) const { return body_key->second; }

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     const char *name, bool opaque,
                                     v_type_t * const *elts, unsigned count, bool packed);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        v_type_t * const *elts = (is_opaque() ? nullptr : element_types());
        unsigned         count = (is_opaque() ? 0       : element_count());
        bool            packed = (is_opaque() ? false   : is_packed());

        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, name(), is_opaque(), elts, count, packed);
    }

    TYPE_VISITOR_TAG(struct)
};


//---------------------------------------------------------------------
//- Array types
//---------------------------------------------------------------------
class v_type_array_t : public v_type_t
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<v_type_t *, uint64_t>;

    const key_t &key;

    explicit v_type_array_t(const key_t &_key)
      : v_type_t(),
        key(_key)
    {}

    v_type_array_t(const v_type_array_t &) = delete;
    v_type_array_t &operator=(const v_type_array_t &) = delete;

public:
    v_type_t *element_type(void) const { return key.first; }

    uint64_t length(void) const { return key.second; }

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     v_type_t *elt, uint64_t length);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, element_type(), length());
    }

    TYPE_VISITOR_TAG(array)
};


//---------------------------------------------------------------------
//- Vector types: fixed/scalable
//---------------------------------------------------------------------
class v_type_vector_t : public v_type_t
{
    friend class voidc_types_ctx_t;

protected:
    using key_t = std::pair<v_type_t *, unsigned>;

    const key_t &key;

    explicit v_type_vector_t(const key_t &_key)
      : v_type_t(),
        key(_key)
    {}

private:
    v_type_vector_t(const v_type_vector_t &) = delete;
    v_type_vector_t &operator=(const v_type_vector_t &) = delete;

public:
    v_type_t *element_type(void) const { return key.first; }

    unsigned size(void) const { return key.second; }

    bool is_scalable(void) const { return (method_tag() == v_type_svector_visitor_method_tag); }

public:
    typedef void (*visitor_method_t)(const visitor_sptr_t *vis, void *aux,
                                     v_type_t *elt, unsigned size, bool scalable);

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        visitor->visit<visitor_method_t>(method_tag(), visitor, aux, element_type(), size(), is_scalable());
    }
};

//---------------------------------------------------------------------
template<const v_quark_t &tag>
class v_type_vector_tag_t : public v_type_vector_t
{
    friend class voidc_types_ctx_t;

    explicit v_type_vector_tag_t(const v_type_vector_t::key_t &_key)
      : v_type_vector_t(_key)
    {}

    v_type_vector_tag_t(const v_type_vector_tag_t &) = delete;
    v_type_vector_tag_t &operator=(const v_type_vector_tag_t &) = delete;

public:
    v_quark_t method_tag(void) const override { return tag; }
};

//---------------------------------------------------------------------
using v_type_fvector_t = v_type_vector_tag_t<v_type_fvector_visitor_method_tag>;
using v_type_svector_t = v_type_vector_tag_t<v_type_svector_visitor_method_tag>;


//---------------------------------------------------------------------
//- Generic types
//---------------------------------------------------------------------
extern "C"
{

struct type_generic_vtable
{
    void (*destroy)(void * const *elts, unsigned count);

    void (*accept)(void * const *elts, unsigned count, const visitor_sptr_t *visitor, void *aux);

    v_quark_t visitor_method_tag;
};

//---------------------------------------------------------------------
}   //- extern "C"


//---------------------------------------------------------------------
class v_type_generic_t : public v_type_t
{
    friend class voidc_types_ctx_t;

    using key_t = std::pair<const type_generic_vtable *, std::vector<void *>>;

    const key_t &key;

    explicit v_type_generic_t(const key_t &_key)
      : v_type_t(),
        key(_key)
    {}

    v_type_generic_t(const v_type_generic_t &) = delete;
    v_type_generic_t &operator=(const v_type_generic_t &) = delete;

public:
    const type_generic_vtable *vtable(void) const { return key.first; }

    unsigned element_count(void) const { return unsigned(key.second.size()); }

    void * const *elements(void) const { return key.second.data(); }

public:
    ~v_type_generic_t() override
    {
        vtable()->destroy(elements(), element_count());
    }

    void accept(const visitor_sptr_t &visitor, void *aux) const override
    {
        vtable()->accept(elements(), element_count(), &visitor, aux);
    }

    v_quark_t method_tag(void) const override { return vtable()->visitor_method_tag; }
};


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


v_type_t *v_type_get_scalar_type(v_type_t *type);

bool      v_type_is_floating_point(v_type_t *type);

unsigned  v_type_floating_point_get_width(v_type_t *type);


VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


//=====================================================================
//- Context of types...
//=====================================================================
class voidc_types_ctx_t
{

public:
    voidc_types_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size);
    ~voidc_types_ctx_t();

public:
    const LLVMContextRef llvm_ctx;          //- Sic!
    const LLVMModuleRef  llvm_mod;          //- Empty module just for LLVMGetTypeByName calls

    const LLVMTypeRef opaque_void_type;     //- For (void *) ...

public:
    v_type_void_t      *make_void_type(void) { return void_type.get(); }

    v_type_f16_t       *make_f16_type(void)  { return f16_type.get(); }
    v_type_f32_t       *make_f32_type(void)  { return f32_type.get(); }
    v_type_f64_t       *make_f64_type(void)  { return f64_type.get(); }
    v_type_f128_t      *make_f128_type(void) { return f128_type.get(); }

    v_type_sint_t      *make_sint_type(unsigned bits);
    v_type_uint_t      *make_uint_type(unsigned bits);

    v_type_function_t  *make_function_type(v_type_t *ret, v_type_t **args, unsigned count, bool var_args=false);

    v_type_pointer_t   *make_pointer_type  (v_type_t *et, unsigned addr_space=0);
    v_type_reference_t *make_reference_type(v_type_t *et, unsigned addr_space=0);

    v_type_struct_t    *make_struct_type(const std::string &name);
    v_type_struct_t    *make_struct_type(v_type_t **elts, unsigned count, bool packed=false);

    v_type_array_t     *make_array_type(v_type_t *et, uint64_t count);

    v_type_fvector_t   *make_fvector_type(v_type_t *et, unsigned count);
    v_type_svector_t   *make_svector_type(v_type_t *et, unsigned count);

    v_type_generic_t   *make_generic_type(const type_generic_vtable *vtab, void **elts, unsigned count);

private:
    std::unique_ptr<v_type_void_t> void_type;

    std::unique_ptr<v_type_f16_t>  f16_type;
    std::unique_ptr<v_type_f32_t>  f32_type;
    std::unique_ptr<v_type_f64_t>  f64_type;
    std::unique_ptr<v_type_f128_t> f128_type;

    std::map<unsigned, std::unique_ptr<v_type_sint_t>> sint_types;
    std::map<unsigned, std::unique_ptr<v_type_uint_t>> uint_types;

    std::map<v_type_function_t::key_t, std::unique_ptr<v_type_function_t>> function_types;

    std::map<v_type_pointer_t::key_t,   std::unique_ptr<v_type_pointer_t>>   pointer_types;
    std::map<v_type_reference_t::key_t, std::unique_ptr<v_type_reference_t>> reference_types;

    std::map<v_type_struct_t::name_key_t, std::unique_ptr<v_type_struct_t>> named_struct_types;
    std::map<v_type_struct_t::body_key_t, std::unique_ptr<v_type_struct_t>> anon_struct_types;

    std::map<v_type_array_t::key_t, std::unique_ptr<v_type_array_t>> array_types;

    std::map<v_type_fvector_t::key_t, std::unique_ptr<v_type_fvector_t>> fvector_types;
    std::map<v_type_svector_t::key_t, std::unique_ptr<v_type_svector_t>> svector_types;

    std::map<v_type_generic_t::key_t, std::unique_ptr<v_type_generic_t>> generic_types;

public:
    v_type_uint_t * const bool_type;
    v_type_sint_t * const char_type;
    v_type_sint_t * const short_type;
    v_type_sint_t * const int_type;
    v_type_uint_t * const unsigned_type;
    v_type_sint_t * const long_type;
    v_type_sint_t * const long_long_type;
    v_type_sint_t * const intptr_t_type;
    v_type_uint_t * const size_t_type;
    v_type_uint_t * const char32_t_type;
    v_type_uint_t * const uint64_t_type;
};


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
void voidc_types_static_initialize(void);
void voidc_types_static_terminate(void);


#endif  //- VOIDC_TYPES_H
