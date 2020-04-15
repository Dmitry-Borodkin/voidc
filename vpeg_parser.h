#ifndef VPEG_PARSER_H
#define VPEG_PARSER_H

#include <memory>
#include <any>
#include <cstdint>

#include <immer/array.hpp>
#include <immer/vector.hpp>

#include "tinyutf8.hpp"


//---------------------------------------------------------------------
namespace vpeg
{

using string = utf8_string;

using value_t = void *;


//---------------------------------------------------------------------
class context_t;


//---------------------------------------------------------------------
//- Parser base class
//---------------------------------------------------------------------
class parser_t
{
public:
    virtual ~parser_t() = default;

public:
    enum kind_t
    {
        k_choice,
        k_sequence,

        k_and,
        k_not,
        k_question,
        k_star,
        k_plus,
        k_catch_variable,
        k_catch_string,

        k_identifier,
        k_backref,
        k_action,
        k_literal,
        k_character,
        k_class,
        k_dot,
    };

    const kind_t kind;

public:
    virtual std::any parse(context_t &ctx) const = 0;

protected:
    explicit parser_t(kind_t _kind)
      : kind(_kind)
    {}
};

typedef std::shared_ptr<const parser_t> parser_ptr_t;


//---------------------------------------------------------------------
//- Action base class
//---------------------------------------------------------------------
class action_t
{
public:
    virtual ~action_t() = default;

public:
    enum kind_t
    {
        k_call,
        k_return,
    };

    const kind_t kind;

public:
    virtual std::any act(context_t &ctx) const = 0;

protected:
    explicit action_t(kind_t _kind)
      : kind(_kind)
    {}
};

typedef std::shared_ptr<const action_t> action_ptr_t;


//---------------------------------------------------------------------
//- Argument base class
//---------------------------------------------------------------------
class argument_t
{
public:
    virtual ~argument_t() = default;

public:
    enum kind_t
    {
        k_identifier,
        k_backref,
        k_integer,
        k_literal,
        k_character,
    };

    const kind_t kind;

public:
    virtual std::any value(context_t &ctx) const = 0;

protected:
    explicit argument_t(kind_t _kind)
      : kind(_kind)
    {}
};

typedef std::shared_ptr<const argument_t> argument_ptr_t;


//---------------------------------------------------------------------
//- Parsers...
//---------------------------------------------------------------------
class parser_array_t : public parser_t
{
protected:
    parser_array_t(kind_t _kind, const std::initializer_list<parser_ptr_t> &list)
      : parser_t(_kind),
        array(list)
    {}

    parser_array_t(kind_t _kind, const parser_ptr_t *list, size_t count)
      : parser_t(_kind),
        array(list, list+count)
    {}

public:
    const immer::array<parser_ptr_t> array;
};


//---------------------------------------------------------------------
class choice_parser_t : public parser_array_t
{
public:
    explicit choice_parser_t(const std::initializer_list<parser_ptr_t> &list)
      : parser_array_t(k_choice, list)
    {}

    choice_parser_t(const parser_ptr_t *list, size_t count)
      : parser_array_t(k_choice, list, count)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<choice_parser_t>
mk_choice_parser(const std::initializer_list<parser_ptr_t> &list)
{
    return std::make_shared<choice_parser_t>(list);
}

inline
std::shared_ptr<choice_parser_t>
mk_choice_parser(const parser_ptr_t *list, size_t count)
{
    return std::make_shared<choice_parser_t>(list, count);
}


//---------------------------------------------------------------------
class sequence_parser_t : public parser_array_t
{
public:
    explicit sequence_parser_t(const std::initializer_list<parser_ptr_t> &list)
      : parser_array_t(k_sequence, list)
    {}

    sequence_parser_t(const parser_ptr_t *list, size_t count)
      : parser_array_t(k_sequence, list, count)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<sequence_parser_t>
mk_sequence_parser(const std::initializer_list<parser_ptr_t> &list)
{
    return std::make_shared<sequence_parser_t>(list);
}

inline
std::shared_ptr<sequence_parser_t>
mk_sequence_parser(const parser_ptr_t *list, size_t count)
{
    return std::make_shared<sequence_parser_t>(list, count);
}


//---------------------------------------------------------------------
class parser_unary_t : public parser_t
{
protected:
    parser_unary_t(kind_t _kind, const parser_ptr_t &_parser)
      : parser_t(_kind),
        parser(_parser)
    {}

public:
    const parser_ptr_t parser;
};


//---------------------------------------------------------------------
class and_parser_t : public parser_unary_t
{
public:
    explicit and_parser_t(const parser_ptr_t &_parser)
      : parser_unary_t(k_and, _parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<and_parser_t>
mk_and_parser(const parser_ptr_t &_parser)
{
    return std::make_shared<and_parser_t>(_parser);
}

//---------------------------------------------------------------------
class not_parser_t : public parser_unary_t
{
public:
    explicit not_parser_t(const parser_ptr_t &_parser)
      : parser_unary_t(k_not, _parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<not_parser_t>
mk_not_parser(const parser_ptr_t &_parser)
{
    return std::make_shared<not_parser_t>(_parser);
}

//---------------------------------------------------------------------
class question_parser_t : public parser_unary_t
{
public:
    explicit question_parser_t(const parser_ptr_t &_parser)
      : parser_unary_t(k_question, _parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<question_parser_t>
mk_question_parser(const parser_ptr_t &_parser)
{
    return std::make_shared<question_parser_t>(_parser);
}

//---------------------------------------------------------------------
class star_parser_t : public parser_unary_t
{
public:
    explicit star_parser_t(const parser_ptr_t &_parser)
      : parser_unary_t(k_star, _parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<star_parser_t>
mk_star_parser(const parser_ptr_t &_parser)
{
    return std::make_shared<star_parser_t>(_parser);
}

//---------------------------------------------------------------------
class plus_parser_t : public parser_unary_t
{
public:
    explicit plus_parser_t(const parser_ptr_t &_parser)
      : parser_unary_t(k_plus, _parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<plus_parser_t>
mk_plus_parser(const parser_ptr_t &_parser)
{
    return std::make_shared<plus_parser_t>(_parser);
}

//---------------------------------------------------------------------
class catch_variable_parser_t : public parser_unary_t
{
public:
    catch_variable_parser_t(const string &_name, const parser_ptr_t &_parser)
      : parser_unary_t(k_catch_variable, _parser),
        name(_name)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const string name;
};

inline
std::shared_ptr<catch_variable_parser_t>
mk_catch_variable_parser(const string &_name, const parser_ptr_t &_parser)
{
    return std::make_shared<catch_variable_parser_t>(_name, _parser);
}

//---------------------------------------------------------------------
class catch_string_parser_t : public parser_unary_t
{
public:
    explicit catch_string_parser_t(const parser_ptr_t &_parser)
      : parser_unary_t(k_catch_string, _parser)
    {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<catch_string_parser_t>
mk_catch_string_parser(const parser_ptr_t &_parser)
{
    return std::make_shared<catch_string_parser_t>(_parser);
}


//---------------------------------------------------------------------
class identifier_parser_t : public parser_t
{
public:
    explicit identifier_parser_t(const string &_ident)
      : parser_t(k_identifier),
        ident(_ident)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const string ident;
};

inline
std::shared_ptr<identifier_parser_t>
mk_identifier_parser(const string &_ident)
{
    return std::make_shared<identifier_parser_t>(_ident);
}

//---------------------------------------------------------------------
class backref_parser_t : public parser_t
{
public:
    explicit backref_parser_t(size_t _number)
      : parser_t(k_backref),
        number(_number)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const size_t number;
};

inline
std::shared_ptr<backref_parser_t>
mk_backref_parser(size_t _number)
{
    return std::make_shared<backref_parser_t>(_number);
}

//---------------------------------------------------------------------
class action_parser_t : public parser_t
{
public:
    explicit action_parser_t(const action_ptr_t &_action)
      : parser_t(k_action),
        action(_action)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const action_ptr_t action;
};

inline
std::shared_ptr<action_parser_t>
mk_action_parser(const action_ptr_t &_action)
{
    return std::make_shared<action_parser_t>(_action);
}

//---------------------------------------------------------------------
class literal_parser_t : public parser_t
{
public:
    explicit literal_parser_t(const string &_utf8)
      : parser_t(k_literal),
        utf8(_utf8)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const string utf8;
};

inline
std::shared_ptr<literal_parser_t>
mk_literal_parser(const string &_utf8)
{
    return std::make_shared<literal_parser_t>(_utf8);
}

//---------------------------------------------------------------------
class character_parser_t : public parser_t
{
public:
    explicit character_parser_t(char32_t _ucs4)
      : parser_t(k_character),
        ucs4(_ucs4)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const char32_t ucs4;
};

inline
std::shared_ptr<character_parser_t>
mk_character_parser(char32_t _ucs4)
{
    return std::make_shared<character_parser_t>(_ucs4);
}

//---------------------------------------------------------------------
class class_parser_t : public parser_t
{
public:
    using range_t = std::array<char32_t, 2>;

public:
    explicit class_parser_t(const std::initializer_list<range_t> &list)
      : parser_t(k_class),
        ranges(list)
    {}

    class_parser_t(const range_t *list, size_t count)
      : parser_t(k_class),
        ranges(list, list+count)
    {}

public:
    std::any parse(context_t &ctx) const override;

public:
    const immer::array<range_t> ranges;
};

inline
std::shared_ptr<class_parser_t>
mk_class_parser(const std::initializer_list<class_parser_t::range_t> &list)
{
    return std::make_shared<class_parser_t>(list);
}

//---------------------------------------------------------------------
class dot_parser_t : public parser_t
{
public:
    dot_parser_t() : parser_t(k_dot) {}

public:
    std::any parse(context_t &ctx) const override;
};

inline
std::shared_ptr<dot_parser_t>
mk_dot_parser(void)
{
    return std::make_shared<dot_parser_t>();
}


//---------------------------------------------------------------------
//- Actions...
//---------------------------------------------------------------------
class call_action_t : public action_t
{
public:
    call_action_t(const string &_fun, const std::initializer_list<argument_ptr_t> &list)
      : action_t(k_call),
        fun(_fun),
        args(list)
    {}

    call_action_t(const string &_fun, const argument_ptr_t *list, size_t count)
      : action_t(k_call),
        fun(_fun),
        args(list, list+count)
    {}

public:
    std::any act(context_t &ctx) const override;

public:
    const string fun;

    const immer::vector<argument_ptr_t> args;       //- WTF !?!?!
};

inline
std::shared_ptr<call_action_t>
mk_call_action(const string &fun, const std::initializer_list<argument_ptr_t> &list)
{
    return std::make_shared<call_action_t>(fun, list);
}

//---------------------------------------------------------------------
class return_action_t : public action_t
{
public:
    explicit return_action_t(const argument_ptr_t &_arg)
      : action_t(k_return),
        arg(_arg)
    {}

public:
    std::any act(context_t &ctx) const override
    {
        return arg->value(ctx);
    }

public:
    const argument_ptr_t arg;
};

inline
std::shared_ptr<return_action_t>
mk_return_action(const argument_ptr_t &arg)
{
    return std::make_shared<return_action_t>(arg);
}


//---------------------------------------------------------------------
//- Arguments...
//---------------------------------------------------------------------
class identifier_argument_t : public argument_t
{
public:
    explicit identifier_argument_t(const string &_ident)
      : argument_t(k_identifier),
        ident(_ident)
    {}

public:
    std::any value(context_t &ctx) const override;

public:
    const string ident;
};

inline
std::shared_ptr<identifier_argument_t>
mk_identifier_argument(const string &ident)
{
    return std::make_shared<identifier_argument_t>(ident);
}

//---------------------------------------------------------------------
class backref_argument_t : public argument_t
{
public:
    enum b_kind_t
    {
        bk_string,
        bk_start,
        bk_end,
    };

public:
    explicit backref_argument_t(size_t _number, b_kind_t _b_kind=bk_string)
      : argument_t(k_backref),
        number(_number),
        b_kind(_b_kind)
    {}

public:
    std::any value(context_t &ctx) const override;

public:
    const size_t   number;
    const b_kind_t b_kind;
};

inline
std::shared_ptr<backref_argument_t>
mk_backref_argument(size_t number, backref_argument_t::b_kind_t b_kind=backref_argument_t::bk_string)
{
    return std::make_shared<backref_argument_t>(number, b_kind);
}

//---------------------------------------------------------------------
class integer_argument_t : public argument_t
{
public:
    explicit integer_argument_t(intptr_t _number)
      : argument_t(k_integer),
        number(_number)
    {}

public:
    std::any value(context_t &ctx) const override
    {
        return number;
    }

public:
    const intptr_t number;
};

inline
std::shared_ptr<integer_argument_t>
mk_integer_argument(intptr_t number)
{
    return std::make_shared<integer_argument_t>(number);
}

//---------------------------------------------------------------------
class literal_argument_t : public argument_t
{
public:
    explicit literal_argument_t(const string &_utf8)
      : argument_t(k_literal),
        utf8(_utf8)
    {}

public:
    std::any value(context_t &ctx) const override
    {
        return utf8;
    }

public:
    const string utf8;
};

inline
std::shared_ptr<literal_argument_t>
mk_literal_argument(const string &utf8)
{
    return std::make_shared<literal_argument_t>(utf8);
}

//---------------------------------------------------------------------
class character_argument_t : public argument_t
{
public:
    explicit character_argument_t(char32_t _ucs4)
      : argument_t(k_character),
        ucs4(_ucs4)
    {}

public:
    std::any value(context_t &ctx) const override
    {
        return ucs4;
    }

public:
    const char32_t ucs4;
};

inline
std::shared_ptr<character_argument_t>
mk_character_argument(char32_t ucs4)
{
    return std::make_shared<character_argument_t>(ucs4);
}


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_PARSER_H

