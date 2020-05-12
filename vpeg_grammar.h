#ifndef VPEG_GRAMMAR_H
#define VPEG_GRAMMAR_H

#include <utility>

#include <immer/map.hpp>

#include "vpeg_parser.h"


//---------------------------------------------------------------------
namespace vpeg
{


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class grammar_t
{
public:
    grammar_t()  = default;
    ~grammar_t() = default;

public:
    grammar_t &operator=(const grammar_t &gr);

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    grammar_t set(const std::string &symbol, const parser_ptr_t &parser, bool leftrec=false) const;

    const std::pair<parser_ptr_t, bool> &get(const std::string &symbol) const;

public:
    std::any parse(const std::string &symbol, context_t &ctx) const;

public:
//    const size_t &hash = _hash;

    void check_hash(void);

public:
    const immer::map<std::string, std::pair<parser_ptr_t, bool>> &map = _map;

//private:
//    size_t _hash = size_t(-1);
    size_t hash = size_t(-1);

private:
    immer::map<std::string, std::pair<parser_ptr_t, bool>> _map;

private:
    explicit grammar_t(const immer::map<std::string, std::pair<parser_ptr_t, bool>> &m)
      : _map(m)
    {}
};



//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_GRAMMAR_H

