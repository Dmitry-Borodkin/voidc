#include "vpeg_context.h"

#include "voidc_llvm.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
context_t *context_t::current_ctx = nullptr;


//-----------------------------------------------------------------
context_t::context_t(std::istream &_input, const grammar_t &_grammar, compile_ctx_t &cctx)
  : input(_input),
    grammar(_grammar)
{
    grammar.check_hash();
}


//---------------------------------------------------------------------
extern "C"
{

//---------------------------------------------------------------------
//- Intrinsics (functions)
//---------------------------------------------------------------------
static
void v_peg_get_grammar(grammar_ptr_t *ptr)
{
    assert(context_t::current_ctx);

    *ptr = std::make_shared<const grammar_t>(context_t::current_ctx->grammar);
}

static
void v_peg_set_grammar(const grammar_ptr_t *ptr)
{
    assert(context_t::current_ctx);

    auto &grammar = context_t::current_ctx->grammar;

    grammar = **ptr;

    grammar.check_hash();
}

//---------------------------------------------------------------------
}   //- extern "C"


//-----------------------------------------------------------------
void context_t::static_initialize(void)
{
    auto fun_type = LLVMFunctionType(compile_ctx_t::void_type, &grammar_ref_type, 1, false);

    v_add_symbol("v_peg_get_grammar", fun_type, (void *)v_peg_get_grammar);
    v_add_symbol("v_peg_set_grammar", fun_type, (void *)v_peg_set_grammar);
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

