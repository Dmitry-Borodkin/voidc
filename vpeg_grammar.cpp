#include "vpeg_grammar.h"

#include "vpeg_context.h"



//---------------------------------------------------------------------
namespace vpeg
{


//-----------------------------------------------------------------
grammar_t &grammar_t::operator=(const grammar_t &gr)
{
    _map  = gr.map;
    _hash = gr.hash;

    return *this;
}

grammar_t grammar_t::set(const string &symbol, const parser_ptr_t &parser, bool leftrec) const
{
    return  grammar_t(map.set(symbol, {parser, leftrec}));
}

const std::pair<parser_ptr_t, bool> &grammar_t::get(const string &symbol) const
{
    return  map.at(symbol);
}

void grammar_t::check_hash(void)
{
    if (hash != size_t(-1)) return;

    static size_t static_hash = 0;

    _hash = static_hash++ ;
}


std::any grammar_t::parse(const string &symbol, context_t &ctx) const
{
    context_t::variables_t saved_vars;      //- empty(!)

    std::swap(ctx.variables, saved_vars);       //- save (and clear)

    auto st = ctx.get_state();

    {   auto &svec = ctx.variables.strings;

        svec = svec.push_back({st.position, 0});    //- #0

        st.variables.strings = svec;        //- Sic!
    }

    std::any ret;

    assert(hash != size_t(-1));

    auto key = std::make_tuple(hash, st.position, symbol);

    if (ctx.memo.count(key) != 0)
    {
        auto &respos = ctx.memo[key];

        ctx.set_state(respos.second);

        ret = respos.first;
    }
    else
    {
        auto &pl = get(symbol);

        if (pl.second)      //- Left-recursive ?
        {
            auto lastres = std::any();
            auto last_st = st;

            ctx.memo[key] = {lastres, st};

            for(;;)
            {
                ctx.set_state(st);

                auto res = pl.first->parse(ctx);
                auto est = ctx.get_state();

                if (est.position <= last_st.position) break;

                lastres = res;
                last_st = est;

                ctx.memo[key] = {lastres, est};
            }

            ctx.set_state(last_st);

            ret = lastres;
        }
        else                //- NOT left-recursive
        {
            auto res = pl.first->parse(ctx);
            auto est = ctx.get_state();

            ctx.memo[key] = {res, est};

            ret = res;
        }
    }

//    if (ret.has_value())
//    {
//        printf("(%d,%d): %s\n", (int)st.position, (int)ctx.get_state().position, symbol.c_str());
//    }

    ctx.variables = saved_vars;                 //- restore saved

    return ret;
}


//---------------------------------------------------------------------
}   //- namespace vpeg


