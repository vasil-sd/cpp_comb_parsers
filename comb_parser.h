#pragma once

#include <array>
#include <functional>
#include "charset.h"
#include <tuple>

// TODO: add cut operator: parser1 cut parser2 - if parser1 succeeds then if parser2 fails, no backtracking occurs, fail whole hier. of parsers up to toplevel parser w/o context
//                         (it may be done via throw)
//       add max operator (like choice but selects max matched parser)
//       change sematics of << operator (should move pos)
//       add ** operator with semantics of current <<

namespace comb_parser {


// Parser result type, used as effect action
using result = std::function<void(void)>;

const result success{[]{}};
const result fail;

// Data converter result type

template<typename T>
struct converter_result{
  using type = std::function<T(void)>;
  static const type fail;
};

template<typename T>
const typename converter_result<T>::type converter_result<T>::fail;

template<typename T, typename Char, typename Iter, typename ...Args>
class converter;

template<typename Char = char, typename Iter = const char*, typename ...Args>
class parser;

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator~(const parser<Char, Iter, Args...>);

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator%(const parser<Char, Iter, Args...>, typename parser<Char, Iter, Args...>::parserFn);

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator|(const parser<Char, Iter, Args...>, const parser<Char, Iter, Args...>);

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator+(const parser<Char, Iter, Args...>, const parser<Char, Iter, Args...>);

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator>>(const parser<Char, Iter, Args...>, const parser<Char, Iter, Args...>);

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator<<(const parser<Char, Iter, Args...>, const parser<Char, Iter, Args...>);

template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> repeat(const parser<Char, Iter, Args...>, int from_times = 0, int to_times = -1);

template<typename Char, typename Iter, typename ...Args>
class parser : public std::function<result(Iter& pos, Iter end)> {
    
    using parserFn = std::function<result(Iter& pos, Iter end)>;

    const parserFn parser_fn;

    explicit parser(const parserFn& p) : parser_fn(p) { }
    explicit parser(parserFn&& p) : parser_fn(std::move(p)) { }

    template<typename, typename, typename...> friend class parser;
    template<typename, typename, typename, typename...> friend class converter;
public:

    template<typename Ctx>
    using with_context = parser<Char, Iter, Ctx>;

    parser() : parser_fn([]{ return success; }) {}

    parser(const parser&) = default;
    
    parser(std::function<bool(Char)>);
    parser(Char c);
    parser(const Char* arr);


    result operator()(Iter& pos, Iter end) const {
      return parser_fn(pos, end);
    }

    static parser end() {
      return parser{[](Iter& pos, Iter const end){
          return pos == end ? success : fail;
      }};
    }

    // not match, useful for look-ahaead:
    // parser1 << !parser2
    const parser operator!() const {
      return parser([p = *this](Iter& pos, Iter end)->result{
        auto start = pos;
        auto r = p(pos, end);
        if (r) { pos = start; return fail; }
        return success;
      });
    }

    const parser somewhere() const {
      return parser([p=*this](Iter& pos, Iter end)->result{
        auto start = pos;
        while(pos != end) {
          auto r = p(pos, end);
          if (r) { return r; }
          ++pos;
        }
        pos = start;
        return fail;
      });
    }


public:
    template<typename T>
    using converter_type = converter<T, Char, Iter>;

    template<typename T, typename L>
    static const converter_type<T> make_converter(L conv) {
      return converter_type<T>{static_cast<typename converter_type<T>::converter_fn>(conv)};
    }

    template<typename T, typename ...NewArgs>
    static const converter_type<T> from_converter(const converter<T, Char, Iter, NewArgs...> conv) {
       return converter_type<T>{conv.conv_fn};
    }

    friend const parser operator~<Char, Iter>(const parser);
    friend const parser operator%<Char, Iter>(const parser, parserFn);
    friend const parser operator|<Char, Iter>(const parser, const parser);
    friend const parser operator+<Char, Iter>(const parser, const parser);
    friend const parser operator>> <Char, Iter>(const parser, const parser);
    friend const parser operator<< <Char, Iter>(const parser, const parser);
    friend const parser repeat<Char, Iter>(const parser, int, int);
};

template<typename Char, typename Iter, typename Arg, typename ...Args>
class parser<Char, Iter, Arg, Args...> : public std::function<result(Iter& pos, Iter end, Arg, Args...)> {
    
    using parserFn = std::function<result(Iter& pos, Iter end, Arg, Args...)>;

    const parserFn parser_fn;

    explicit parser(const parserFn& p) : parser_fn(p) { }
    explicit parser(parserFn&& p) : parser_fn(std::move(p)) { }

    template<typename, typename, typename...> friend class parser;
    template<typename, typename, typename, typename...> friend class converter;
public:

    template<typename Ctx>
    using with_context = parser<Char, Iter, Ctx, Arg, Args...>;

    parser() : parser_fn([]{ return success; }) {}
    parser(const parser&) = default;

    parser(std::function<bool(Char)>);
    parser(Char);
    parser(const Char*);

    parser (const parser<Char, Iter, Args...> p)
      : parser_fn([=](Iter& pos, Iter end, Arg, Args... args){
          return p(pos, end, args...);
        }) { }

    result operator()(Iter& pos, Iter end, Arg arg, Args...args) const {
      return parser_fn(pos, end, arg, args...);
    }

    const parser<Char, Iter, Args...> operator()(std::function<Arg(Args...)> context_gen) const {
      return parser<Char, Iter, Args...>([parser_fn=parser_fn, context_gen](Iter& pos, Iter end, Args...args)->result{
        return parser_fn(pos, end, context_gen(args...), args...);
      });
    }

    static parser end() {
      return parser{[](Iter& pos, Iter const end, Arg, Args...){
          return pos == end ? success : fail;
      }};
    }

    const parser operator!() const {
      return parser([p = *this](Iter& pos, Iter end, Arg arg, Args...args)->result{
        auto start = pos;
        auto r = p(pos, end, arg, args...);
        if (r) { pos = start; return fail; }
        return success;
      });
    }

    const parser somewhere() const {
      return parser([p=*this](Iter& pos, Iter end, Arg arg, Args...args)->result{
        auto start = pos;
        while(pos != end) {
          auto r = p(pos, end, arg, args...);
          if (r) { return r; }
          ++pos;
        }
        pos = start;
        return fail;
      });
    }

public:
    template<typename T>
    using converter_type = converter<T, Char, Iter, Arg, Args...>;

    template<typename T, typename L>
    static const converter_type<T> make_converter(L conv) {
      return converter_type<T>{static_cast<typename converter_type<T>::converter_fn>(conv)};
    }

    template<typename T, typename ...NewArgs>
    static const converter_type<T> from_converter(const converter<T, Char, Iter, NewArgs...> conv) {
       return converter_type<T>{conv.conv_fn};
    }

    friend const parser operator~<Char, Iter, Arg, Args...>(const parser);
    friend const parser operator%<Char, Iter, Arg, Args...>(const parser, parserFn);
    friend const parser operator|<Char, Iter, Arg, Args...>(const parser, const parser);
    friend const parser operator+<Char, Iter, Arg, Args...>(const parser, const parser);
    friend const parser operator>><Char, Iter, Arg, Args...>(const parser, const parser);
    friend const parser operator<< <Char, Iter, Arg, Args...>(const parser, const parser);
    friend const parser repeat<Char, Iter, Arg, Args...>(const parser, int, int);
};

//========================
// Constructors of parser

namespace {

template<typename Char, typename Iter>
result parse_using_matcher(Iter& pos, const Iter end, std::function<bool(Char)> matcher) {
    auto start = pos;
    for (;pos != end && matcher(*pos); ++pos) { }
    return pos != start ? success : fail;
}

template<typename Char, typename Iter>
result parse_using_char(Iter& pos, const Iter end, Char c) {
    if (pos == end) return fail;
    if (*pos == c) { ++pos; return success; }
    return fail;
}

template<typename Char, typename Iter>
result parse_using_char_array(Iter& pos, const Iter end, const Char* arr) {
    auto start = pos;
    auto it = arr;
    for (; *it != 0 && pos != end && *it == *pos; ++it, ++pos) { }
    if (*it == 0) { return success; }
    pos = start;
    return fail;
}

} // namespace anonymous

template<typename Char, typename Iter, typename ... Args>
parser<Char, Iter, Args...>::parser(std::function<bool(Char)> matcher)
  : parser_fn([=](Iter& pos, Iter end){ return parse_using_matcher(pos, end, matcher); }) { }

template<typename Char, typename Iter, typename Arg, typename ... Args>
parser<Char, Iter, Arg, Args...>::parser(std::function<bool(Char)> matcher)
  : parser_fn([=](Iter& pos, Iter end, Arg, Args...){ return parse_using_matcher(pos, end, matcher); }) { }

template<typename Char, typename Iter, typename ... Args>
parser<Char, Iter, Args...>::parser(Char c)
  : parser_fn([=](Iter& pos, Iter end){ return parse_using_char(pos, end, c); }) { }

template<typename Char, typename Iter, typename Arg, typename ... Args>
parser<Char, Iter, Arg, Args...>::parser(Char c)
  : parser_fn([=](Iter& pos, Iter end, Arg, Args...){ return parse_using_char(pos, end, c); }) { }

template<typename Char, typename Iter, typename ... Args>
parser<Char, Iter, Args...>::parser(const Char* arr)
  : parser_fn([=](Iter& pos, Iter end){ return parse_using_char_array(pos, end, arr); }) { }

template<typename Char, typename Iter, typename Arg, typename ... Args>
parser<Char, Iter, Arg, Args...>::parser(const Char* arr)
  : parser_fn([=](Iter& pos, Iter end, Arg, Args...){ return parse_using_char_array(pos, end, arr); }) { }


//====================================
// Operators (combinators) for parsers

// combinator 'optional': ~parser, if parsed succefully - good, cannot parse - good too :)
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator~(const parser<Char, Iter, Args...> p) {
  return parser<Char, Iter, Args...>{[=](Iter& pos, Iter end, Args...args)->result{
    auto r = p(pos, end, args...);
    if (r) return r;
    return success;
  }};
}

// combinator 'detail/process': parser1 % parser2/action - invoke parser1 on success proceed to parser2/action with same pos/end
// attach extra processor to parser, it may be an action
// or more detailed parser
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator%(const parser<Char, Iter, Args...> p, typename parser<Char, Iter, Args...>::parserFn process) {
  return parser<Char, Iter, Args...>{[=](Iter& pos, Iter end, Args...args)->result{
    auto start = pos;
    auto r = p(pos, end, args...);
    if (r) {
      auto new_pos = start;
      auto rp = process(new_pos, pos, args...);
      if (rp) { return [=]{r();rp();}; }
      pos = start;
    }
    return fail;
  }};
}

//combinator 'choice': parser1 | parser2 - try parser1 if fail then try parser2
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator| (const parser<Char, Iter, Args...> p1, const parser<Char, Iter, Args...> p2) {
    return parser<Char, Iter, Args...>{[=] (Iter& pos, Iter end, Args...args)->result{
      auto r = p1(pos, end, args...);
      if (r) return r;
      return p2(pos, end, args...);
    }};
}

// combinator 'sequence': parser1 + parser2 - try parser1 on success, move pos and try parser2,
// if parser2 fails then return pos to initial state (before invoking parser1)
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator+ (const parser<Char, Iter, Args...> p1, const parser<Char, Iter, Args...> p2) {
  return parser<Char, Iter, Args...>{[=] (Iter& pos, Iter end, Args...args)->result{
    auto start = pos;
    auto r1 = p1(pos, end, args...);
    if (!r1) return fail;
    auto r2 = p2(pos, end, args...);
    if (!r2) { pos = start; return fail;}
    return [=]{r1();r2();};
  }};
}

// combinator 'skip': parser1 >> parser2 - if parser1 and parser2 succeeded then effect of parser2 goes to the final effect 
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator>> (const parser<Char, Iter, Args...> p1, const parser <Char, Iter, Args...> p2){
  return parser<Char, Iter, Args...>{[=] (Iter& pos, Iter end, Args...args)->result{
    auto start = pos;
    auto r1 = p1(pos, end, args...);
    if (!r1) return fail;
    auto r2 = p2(pos, end, args...);
    if (!r2) { pos = start; return fail;}
    return r2;
  }};
}

// combinator 'check next': parser1 << parser2 - parser1 considered succesful only if both
// of them succeeded, but final effect taken only from parser1
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> operator<< (const parser<Char, Iter, Args...> p1, const parser<Char, Iter, Args...>  p2){
  return parser<Char, Iter, Args...>{[=] (Iter& pos, Iter end, Args...args)->result{
    auto start = pos;
    auto r1 = p1(pos, end, args...);
    if (!r1) return fail;
    auto before_p2 = pos;
    auto r2 = p2(pos, end, args...);
    if (!r2) { pos = start; return fail;}
    pos = before_p2; // TODO: see upper TODO
    return r1;;
  }};
}

// combinator 'repeat' : repeat(parser, from_times, to_times) - try to apply parser in sequence specified times
// by default repeat 0 or more times
// alternatives: [from ... +inf) - exactly <from> or more
//               [0..to] - exactly <to> or less, down to 0
//               [from..to] - exactly <from> or more, but less or equal <to>
template<typename Char, typename Iter, typename...Args>
const parser<Char, Iter, Args...> repeat(const parser<Char, Iter, Args...> p, int from_times, int to_times) {
  return parser<Char, Iter, Args...>{[=] (Iter& pos, Iter end, Args...args)->result{
    int times = 0;
    auto start = pos;
    std::vector<result*> results;
    while(pos != end && (to_times == -1 || times <= to_times)) {
      auto r = p(pos, end, args...);
      if (!r) break;
      ++times;
      results.push_back(new result{r});
    }
    if (times >= from_times && (to_times == -1 || times <= to_times)) {
      return [=]{
        for (auto res: results){
          (*res)();
          delete res;
        }
      };
    }
    pos = start;
    for (auto res: results) delete res;
    return fail;
  }};
}


//======================
// Converter definition

template<typename T, typename Char, typename Iter, typename ...Args>
class converter{
private:
  using converter_fn = std::function<typename converter_result<T>::type(Iter pos, Iter end)>;

  converter_fn conv_fn;
  converter(converter_fn c) : conv_fn(c) { }
  template<typename, typename, typename...> friend class parser;

public:

  using parser_type = parser<Char, Iter, Args...>;

  const parser_type operator%(std::function<result(typename converter_result<T>::type, Args...)> process) const {
    return parser_type([=, c=conv_fn](Iter& pos, Iter end, Args...args)->result{
      return process(c(pos, end), args...);
    });
  }

};


}
