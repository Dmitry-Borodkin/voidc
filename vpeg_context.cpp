#include "vpeg_context.h"


//---------------------------------------------------------------------
namespace vpeg
{


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

