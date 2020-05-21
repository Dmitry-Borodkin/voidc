#include "vpeg_context.h"

#include "voidc_llvm.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
context_t::context_t(std::istream &_input, const grammar_t &_grammar, compile_ctx_t &cctx)
  : input(_input),
    grammar(_grammar)
{
    grammar.check_hash();

    cctx.parser_context = this;
}


//---------------------------------------------------------------------
//- Intrinsics (true)
//---------------------------------------------------------------------
static
void v_peg_get_grammar(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    cctx->build_intrinsic_call("voidc_intrinsic_peg_get_grammar", *args);
}

static
void v_peg_set_grammar(compile_ctx_t *cctx, const std::shared_ptr<const ast_arg_list_t> *args)
{
    cctx->build_intrinsic_call("voidc_intrinsic_peg_set_grammar", *args);
}


//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
static
void voidc_intrinsic_peg_get_grammar(void *void_cctx, grammar_ptr_t *ptr)
{
    auto *cctx = (compile_ctx_t *)void_cctx;

    assert(cctx->parser_context);

    *ptr = std::make_shared<const grammar_t>(cctx->parser_context->grammar);
}

static
void voidc_intrinsic_peg_set_grammar(void *void_cctx, const grammar_ptr_t *ptr)
{
    auto *cctx = (compile_ctx_t *)void_cctx;

    assert(cctx->parser_context);

    auto &grammar = cctx->parser_context->grammar;

    grammar = **ptr;

    grammar.check_hash();
}

//---------------------------------------------------------------------
}   //- extern "C"


//-----------------------------------------------------------------
void context_t::static_initialize(void)
{
    compile_ctx_t::intrinsics["v_peg_get_grammar"] = v_peg_get_grammar;
    compile_ctx_t::intrinsics["v_peg_set_grammar"] = v_peg_set_grammar;

    auto grammar_ref_type =  (LLVMTypeRef)LLVMSearchForAddressOfSymbol("v_peg_grammar_ref");

    auto &void_type = compile_ctx_t::void_type;

    LLVMTypeRef args[] =
    {
        LLVMPointerType(void_type, 0),
        grammar_ref_type
    };

    compile_ctx_t::symbol_types["voidc_intrinsic_peg_get_grammar"] = LLVMFunctionType(void_type, args, 2, false);
    LLVMAddSymbol("voidc_intrinsic_peg_get_grammar", (void *)voidc_intrinsic_peg_get_grammar);

    compile_ctx_t::symbol_types["voidc_intrinsic_peg_set_grammar"] = LLVMFunctionType(void_type, args, 2, false);
    LLVMAddSymbol("voidc_intrinsic_peg_set_grammar", (void *)voidc_intrinsic_peg_set_grammar);
}

//-----------------------------------------------------------------
void context_t::static_terminate(void)
{
}



//-----------------------------------------------------------------
std::string context_t::take_string(size_t from, size_t to) const
{
    auto src = buffer.begin() + from;

    size_t count = to - from;

    size_t len = 0;

    auto s = src;

    for (size_t i=0; i<count; ++i)
    {
        char32_t c = *s++;

        if (c <= 0x7FF)
        {
            if (c <= 0x7F)  len += 1;
            else            len += 2;
        }
        else
        {
            if (c <= 0xFFFF)  len += 3;
            else              len += 4;
        }
    }

    std::string dst(len, ' ');

    char *d = dst.data();

    s = src;

    for (size_t i=0; i<count; ++i)
    {
        char32_t c = *s++;

        int r;

        if (c <= 0x7FF)
        {
            if (c <= 0x7F)  r = 0;
            else            r = 1;
        }
        else
        {
            if (c <= 0xFFFF)  r = 2;
            else              r = 3;
        }

        if (r == 0)
        {
            d[0] = c & 0x7F;
        }
        else
        {
            for (int j = 0; j < r; ++j)
            {
                d[r-j] = 0x80 | (c & 0x3F);

                c >>= 6;
            }

            d[0] = (0x1E << (6-r)) | (c & (0x3F >> r));
        }

        d += r + 1;
    }

    return dst;
}


//-----------------------------------------------------------------
char32_t context_t::read_character(void)
{
    uint8_t c0; input.get((char &)c0);

    if (!input) return char32_t(-1);    //- Sic!

    int n;

    uint32_t r;

    if (c0 < 0xE0)
    {
        if (c0 < 0xC0)  { r = c0;           n = 0; }
        else            { r = (c0 & 0x1F);  n = 1; }
    }
    else
    {
        if (c0 < 0xF0)  { r = (c0 & 0x0F);  n = 2; }
        else            { r = (c0 & 0x07);  n = 3; }
    }

    for(; n; --n)
    {
        input.get((char &)c0);

        r = (r << 6) | (c0 & 0x3F);
    }

    return r;
}


//---------------------------------------------------------------------
}   //- namespace vpeg

