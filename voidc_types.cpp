//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_types.h"

#include "voidc_target.h"
#include "voidc_dllexport.h"

#include <cstdio>
#include <cassert>
#include <algorithm>

#include <llvm/IR/DerivedTypes.h>

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
v_type_t::~v_type_t() {}


//---------------------------------------------------------------------
static void
v_type_void_obtain_llvm_type(const visitor_ptr_t *vis, void *aux)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    ret = LLVMVoidTypeInContext(gctx.llvm_ctx);
}

static void
v_type_f16_obtain_llvm_type(const visitor_ptr_t *vis, void *aux)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    ret = LLVMHalfTypeInContext(gctx.llvm_ctx);
}

static void
v_type_f32_obtain_llvm_type(const visitor_ptr_t *vis, void *aux)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    ret = LLVMFloatTypeInContext(gctx.llvm_ctx);
}

static void
v_type_f64_obtain_llvm_type(const visitor_ptr_t *vis, void *aux)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    ret = LLVMDoubleTypeInContext(gctx.llvm_ctx);
}

static void
v_type_f128_obtain_llvm_type(const visitor_ptr_t *vis, void *aux)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    ret = LLVMFP128TypeInContext(gctx.llvm_ctx);
}


//---------------------------------------------------------------------
static void
v_type_integer_obtain_llvm_type(const visitor_ptr_t *vis, void *aux,
                                unsigned bits, bool _signed)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    ret = LLVMIntTypeInContext(gctx.llvm_ctx, bits);
}

#define v_type_sint_obtain_llvm_type v_type_integer_obtain_llvm_type
#define v_type_uint_obtain_llvm_type v_type_integer_obtain_llvm_type


//---------------------------------------------------------------------
static void
v_type_function_obtain_llvm_type(const visitor_ptr_t *vis, void *aux,
                                 v_type_t *_ret, v_type_t * const *args, unsigned count, bool var_arg)
{
    auto &ret = *(LLVMTypeRef *)aux;

    const std::size_t N = count + 1;

    LLVMTypeRef ft[N];

    ft[0] = _ret->llvm_type();

    for (unsigned i=1; i<N; ++i)
    {
        ft[i] = args[i-1]->llvm_type();
    }

    ret = LLVMFunctionType(ft[0], ft+1, N-1, var_arg);
}


//---------------------------------------------------------------------
static void
v_type_refptr_obtain_llvm_type(const visitor_ptr_t *vis, void *aux,
                               v_type_t *elt, unsigned addr_space, bool reference)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    LLVMTypeRef et = nullptr;

    if (elt->method_tag() == v_type_void_visitor_method_tag)
    {
        et = gctx.opaque_void_type;
    }
    else
    {
        et = elt->llvm_type();
    }

    ret = LLVMPointerType(et, addr_space);
}

#define v_type_pointer_obtain_llvm_type   v_type_refptr_obtain_llvm_type
#define v_type_reference_obtain_llvm_type v_type_refptr_obtain_llvm_type


//---------------------------------------------------------------------
static void
v_type_struct_obtain_llvm_type(const visitor_ptr_t *vis, void *aux,
                               const char *name, bool opaque,
                               v_type_t * const *_elts, unsigned count, bool packed)
{
    auto &ret = *(LLVMTypeRef *)aux;
    auto &gctx = *voidc_global_ctx_t::target;

    LLVMTypeRef t = nullptr;

    if (name)
    {
        t = LLVMGetTypeByName(gctx.llvm_mod, name);

        if (!t) t = LLVMStructCreateNamed(gctx.llvm_ctx, name);
    }

    if (!opaque)
    {
        if (t  &&  !LLVMIsOpaqueStruct(t))
        {
            ret = t;

            return;
        }

        const std::size_t N = count;

        LLVMTypeRef elts[N];

        for (unsigned i=0; i<N; ++i)
        {
            elts[i] = _elts[i]->llvm_type();
        }

        if (t)      //- Named opaque (so far) struct - set body...
        {
            LLVMStructSetBody(t, elts, N, packed);
        }
        else        //- Unnamed...
        {
            t = LLVMStructTypeInContext(gctx.llvm_ctx, elts, N, packed);
        }
    }

    assert(t && "Unnamed struct without body");

    ret = t;
}

void
v_type_struct_t::set_body(v_type_t **elts, unsigned count, bool packed)
{
    auto &gctx = *voidc_global_ctx_t::target;

    assert(is_opaque() && "Struct body already set");

    auto *st = gctx.make_struct_type(elts, count, packed);

    body_key = st->body_key;

    if (cached_llvm_type)
    {
        cached_llvm_type = nullptr;     //- Sic!

        llvm_type();
    }
}


//---------------------------------------------------------------------
static void
v_type_array_obtain_llvm_type(const visitor_ptr_t *vis, void *aux,
                              v_type_t *elt, uint64_t length)
{
    auto &ret = *(LLVMTypeRef *)aux;

    auto et = elt->llvm_type();

    using namespace llvm;

    ret = wrap(ArrayType::get(unwrap(et), length));
}


//---------------------------------------------------------------------
static void
v_type_vector_obtain_llvm_type(const visitor_ptr_t *vis, void *aux,
                               v_type_t *elt, unsigned size, bool scalable)
{
    auto &ret = *(LLVMTypeRef *)aux;

    auto et = elt->llvm_type();

    using namespace llvm;

    if (scalable)   ret = wrap(ScalableVectorType::get(unwrap(et), size));
    else            ret = wrap(FixedVectorType::get(unwrap(et), size));
}

#define v_type_fvector_obtain_llvm_type v_type_vector_obtain_llvm_type
#define v_type_svector_obtain_llvm_type v_type_vector_obtain_llvm_type


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
voidc_types_ctx_t::voidc_types_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : llvm_ctx(ctx),
    llvm_mod(LLVMModuleCreateWithNameInContext("empty_mod", ctx)),
    opaque_void_type(LLVMStructCreateNamed(ctx, "struct.v_target_opaque_void")),

    void_type(new v_type_void_t()),
    f16_type (new v_type_f16_t()),
    f32_type (new v_type_f32_t()),
    f64_type (new v_type_f64_t()),
    f128_type(new v_type_f128_t()),

    bool_type     (make_uint_type(1)),
    char_type     (make_sint_type(8)),
    short_type    (make_sint_type(16)),
    int_type      (make_sint_type(8*int_size)),
    unsigned_type (make_uint_type(8*int_size)),
    long_type     (make_sint_type(8*long_size)),
    long_long_type(make_sint_type(64)),
    intptr_t_type (make_sint_type(8*ptr_size)),
    size_t_type   (make_uint_type(8*ptr_size)),
    char32_t_type (make_uint_type(32)),
    uint64_t_type (make_uint_type(64))
{
}

voidc_types_ctx_t::~voidc_types_ctx_t()
{
//  for (auto &it : sint_types)  printf("s: %d, %d\n", it.second->is_signed(), it.second->width());
//  for (auto &it : uint_types)  printf("u: %d, %d\n", it.second->is_signed(), it.second->width());
}


//---------------------------------------------------------------------
v_type_sint_t *
voidc_types_ctx_t::make_sint_type(unsigned bits)
{
    auto [it, nx] = sint_types.try_emplace(bits, nullptr);

    if (nx) it->second.reset(new v_type_sint_t(it->first));

    return  it->second.get();
}

v_type_uint_t *
voidc_types_ctx_t::make_uint_type(unsigned bits)
{
    auto [it, nx] = uint_types.try_emplace(bits, nullptr);

    if (nx) it->second.reset(new v_type_uint_t(it->first));

    return  it->second.get();
}


//---------------------------------------------------------------------
v_type_function_t *
voidc_types_ctx_t::make_function_type(v_type_t *ret, v_type_t **args, unsigned count, bool var_arg)
{
    const unsigned N = count + 1;

    v_type_t *ft_data[N];

    ft_data[0] = ret;

    std::copy_n(args, count, ft_data+1);

    v_type_function_t::key_t key = { {ft_data, ft_data+N}, var_arg };

    auto [it, nx] = function_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_function_t(it->first));

    return  it->second.get();
}


//---------------------------------------------------------------------
v_type_pointer_t *
voidc_types_ctx_t::make_pointer_type(v_type_t *et, unsigned addr_space)
{
    v_type_pointer_t::key_t key = { et, addr_space };

    auto [it, nx] = pointer_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_pointer_t(it->first));

    return  it->second.get();
}

//---------------------------------------------------------------------
v_type_reference_t *
voidc_types_ctx_t::make_reference_type(v_type_t *et, unsigned addr_space)
{
    v_type_reference_t::key_t key = { et, addr_space };

    auto [it, nx] = reference_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_reference_t(it->first));

    return  it->second.get();
}


//---------------------------------------------------------------------
v_type_struct_t *
voidc_types_ctx_t::make_struct_type(const std::string &name)
{
    auto [it, nx] = named_struct_types.try_emplace(name, nullptr);

    if (nx) it->second.reset(new v_type_struct_t(it->first));

    return  it->second.get();
}

v_type_struct_t *
voidc_types_ctx_t::make_struct_type(v_type_t **elts, unsigned count, bool packed)
{
    v_type_struct_t::body_key_t key = { {elts, elts+count}, packed };

    auto [it, nx] = anon_struct_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_struct_t(it->first));

    return  it->second.get();
}


//---------------------------------------------------------------------
v_type_array_t *
voidc_types_ctx_t::make_array_type(v_type_t *et, uint64_t count)
{
    v_type_array_t::key_t key = { et, count };

    auto [it, nx] = array_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_array_t(it->first));

    return  it->second.get();
}


//---------------------------------------------------------------------
v_type_fvector_t *
voidc_types_ctx_t::make_fvector_type(v_type_t *et, unsigned count)
{
    v_type_fvector_t::key_t key = { et, count };

    auto [it, nx] = fvector_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_fvector_t(it->first));

    return  it->second.get();
}

v_type_svector_t *
voidc_types_ctx_t::make_svector_type(v_type_t *et, unsigned count)
{
    v_type_svector_t::key_t key = { et, count };

    auto [it, nx] = svector_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_svector_t(it->first));

    return  it->second.get();
}


//-----------------------------------------------------------------
v_type_generic_t *
voidc_types_ctx_t::make_generic_type(const type_generic_vtable *vtab, void **elts, unsigned count)
{
    v_type_generic_t::key_t key = { vtab, { elts, elts+count } };

    auto [it, nx] = generic_types.try_emplace(key, nullptr);

    if (nx) it->second.reset(new v_type_generic_t(it->first));

    return  it->second.get();
}


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
void voidc_types_static_initialize(void)
{
#define DEF(tag) \
    v_type_##tag##_visitor_method_tag = v_quark_from_string("v_type_" #tag "_visitor_method_tag");

    DEFINE_TYPE_VISITOR_METHOD_TAGS(DEF)

#undef DEF

    voidc_visitor_t vis;

#define DEF(tag) \
    vis = vis.set_void_method(v_type_##tag##_visitor_method_tag, (void *)v_type_##tag##_obtain_llvm_type);

    DEFINE_TYPE_VISITOR_METHOD_TAGS(DEF)

#undef DEF

    voidc_llvm_type_visitor = std::make_shared<const voidc_visitor_t>(vis);
}

//-----------------------------------------------------------------
void voidc_types_static_terminate(void)
{
}


//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION


//---------------------------------------------------------------------
v_quark_t
v_type_get_method_tag(v_type_t *type)
{
    return type->method_tag();
}

LLVMTypeRef
v_type_get_llvm_type(v_type_t *type)
{
    return type->llvm_type();
}

v_type_t *
v_type_get_scalar_type(v_type_t *type)
{
    if (auto *p = dynamic_cast<v_type_vector_t *>(type))
    {
        return p->element_type();
    }

    return type;
}


//---------------------------------------------------------------------
v_type_t *
v_f16_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f16_type();
}

v_type_t *
v_f32_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f32_type();
}

v_type_t *
v_f64_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f64_type();
}

v_type_t *
v_f128_type(void)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_f128_type();
}

bool
v_type_is_floating_point(v_type_t *type)
{
    auto tag = type->method_tag();

    return (tag == v_type_f16_visitor_method_tag  ||
            tag == v_type_f32_visitor_method_tag  ||
            tag == v_type_f64_visitor_method_tag  ||
            tag == v_type_f128_visitor_method_tag);
}

unsigned
v_type_floating_point_get_width(v_type_t *type)
{
    auto tag = type->method_tag();

    if (tag == v_type_f16_visitor_method_tag)   return 16;
    if (tag == v_type_f32_visitor_method_tag)   return 32;
    if (tag == v_type_f64_visitor_method_tag)   return 64;
    if (tag == v_type_f128_visitor_method_tag)  return 128;

    return 0;   //- ?
}


//---------------------------------------------------------------------
v_type_t *
v_sint_type(unsigned bits)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_sint_type(bits);
}

v_type_t *
v_uint_type(unsigned bits)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_uint_type(bits);
}

bool
v_type_is_integer(v_type_t *type)
{
    return bool(dynamic_cast<v_type_integer_t *>(type));
}

bool
v_type_integer_is_signed(v_type_t *type)
{
    return static_cast<v_type_integer_t *>(type)->is_signed();
}

unsigned
v_type_integer_get_width(v_type_t *type)
{
    return static_cast<v_type_integer_t *>(type)->width();
}


//---------------------------------------------------------------------
v_type_t *
v_function_type(v_type_t *ret, v_type_t **args, unsigned count, bool var_arg)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_function_type(ret, args, count, var_arg);
}

bool
v_type_is_function(v_type_t *type)
{
    return bool(dynamic_cast<v_type_function_t *>(type));
}

bool
v_type_function_is_var_arg(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->is_var_arg();
}

v_type_t *
v_type_function_get_return_type(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->return_type();
}

unsigned
v_type_function_get_param_count(v_type_t *type)
{
    return static_cast<v_type_function_t *>(type)->param_count();
}

void
v_type_function_get_param_types(v_type_t *type, v_type_t **params)
{
    auto fp = static_cast<v_type_function_t *>(type);

    std::copy_n(fp->param_types(), fp->param_count(), params);
}


//---------------------------------------------------------------------
v_type_t *
v_pointer_type(v_type_t *elt, unsigned addr_space)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_pointer_type(elt, addr_space);
}

v_type_t *
v_reference_type(v_type_t *elt, unsigned addr_space)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_reference_type(elt, addr_space);
}

bool
v_type_is_pointer(v_type_t *type)
{
    return bool(dynamic_cast<v_type_pointer_t *>(type));        //- ?
}

bool
v_type_is_reference(v_type_t *type)
{
    return bool(dynamic_cast<v_type_reference_t *>(type));      //- ?
}

v_type_t *
v_type_refptr_get_element_type(v_type_t *type)
{
    return static_cast<v_type_refptr_t *>(type)->element_type();
}

unsigned
v_type_refptr_get_address_space(v_type_t *type)
{
    return static_cast<v_type_refptr_t *>(type)->address_space();
}


//---------------------------------------------------------------------
v_type_t *
v_struct_type_named(const char *name)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_struct_type(name);
}

v_type_t *
v_struct_type(v_type_t **elts, unsigned count, bool packed)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_struct_type(elts, count, packed);
}

bool
v_type_is_struct(v_type_t *type)
{
    return bool(dynamic_cast<v_type_struct_t *>(type));
}

const char *
v_type_struct_get_name(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->name();
}

bool
v_type_struct_is_opaque(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->is_opaque();
}

void
v_type_struct_set_body(v_type_t *type, v_type_t **elts, unsigned count, bool packed)
{
    static_cast<v_type_struct_t *>(type)->set_body(elts, count, packed);
}

unsigned
v_type_struct_get_element_count(v_type_t *type)
{
    return static_cast<v_type_struct_t *>(type)->element_count();
}

void
v_type_struct_get_element_types(v_type_t *type, v_type_t **elts)
{
    auto sp = static_cast<v_type_struct_t *>(type);

    std::copy_n(sp->element_types(), sp->element_count(), elts);
}

v_type_t *
v_type_struct_get_type_at_index(v_type_t *type, unsigned idx)
{
    return static_cast<v_type_struct_t *>(type)->element_types()[idx];
}


//---------------------------------------------------------------------
v_type_t *
v_array_type(v_type_t *elt, uint64_t count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_array_type(elt, count);
}

bool
v_type_is_array(v_type_t *type)
{
    return bool(dynamic_cast<v_type_array_t *>(type));
}

v_type_t *
v_type_array_get_element_type(v_type_t *type)
{
    return static_cast<v_type_array_t *>(type)->element_type();
}

uint64_t
v_type_array_get_length(v_type_t *type)
{
    return static_cast<v_type_array_t *>(type)->length();
}


//---------------------------------------------------------------------
v_type_t *
v_fvector_type(v_type_t *elt, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_fvector_type(elt, count);
}

v_type_t *
v_svector_type(v_type_t *elt, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_svector_type(elt, count);
}

bool
v_type_is_vector(v_type_t *type)
{
    return bool(dynamic_cast<v_type_vector_t *>(type));
}

v_type_t *
v_type_vector_get_element_type(v_type_t *type)
{
    return static_cast<v_type_vector_t *>(type)->element_type();
}

unsigned
v_type_vector_get_size(v_type_t *type)
{
    return static_cast<v_type_vector_t *>(type)->size();
}

bool
v_type_vector_is_scalable(v_type_t *type)
{
    return static_cast<v_type_vector_t *>(type)->is_scalable();
}


//---------------------------------------------------------------------
v_type_t *
v_generic_type(const type_generic_vtable *vtab, void **elts, unsigned count)
{
    auto &gctx = *voidc_global_ctx_t::target;

    return gctx.make_generic_type(vtab, elts, count);
}

const type_generic_vtable *
v_type_generic_get_vtable(v_type_t *type)
{
    return static_cast<v_type_generic_t *>(type)->vtable();
}

unsigned
v_type_generic_get_element_count(v_type_t *type)
{
    return static_cast<v_type_generic_t *>(type)->element_count();
}

void
v_type_generic_get_elements(v_type_t *type, void **elts)
{
    auto gp = static_cast<v_type_generic_t *>(type);

    std::copy_n(gp->elements(), gp->element_count(), elts);
}

void *
v_type_generic_get_element_at_index(v_type_t *type, unsigned idx)
{
    return static_cast<v_type_generic_t *>(type)->elements()[idx];
}


//---------------------------------------------------------------------
//- Visitors ...
//---------------------------------------------------------------------
#define DEF(tag) \
v_quark_t v_type_##tag##_visitor_method_tag;

    DEFINE_TYPE_VISITOR_METHOD_TAGS(DEF)

#undef DEF


#define DEF(tag) \
void \
v_visitor_set_method_type_##tag##_t(visitor_ptr_t *dst, const visitor_ptr_t *src, v_type_##tag##_t::visitor_method_t method) \
{ \
    auto visitor = (*src)->set_void_method(v_type_##tag##_visitor_method_tag, (void *)method); \
    *dst = std::make_shared<const voidc_visitor_t>(visitor); \
}

    DEFINE_TYPE_VISITOR_METHOD_TAGS(DEF)

#undef DEF


//---------------------------------------------------------------------
void
v_type_accept_visitor(v_type_t *type, const visitor_ptr_t *visitor, void *aux)
{
    type->accept(*visitor, aux);
}


//---------------------------------------------------------------------
visitor_ptr_t voidc_llvm_type_visitor;


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


