#pragma once

#include <array>
#include <functional>
#include "charset.h"

// TODO think about contexted parsers

namespace comb_parser {

using result = std::function<void(void)>;

const result success{[]{}};
const result fail;

template<typename Char = char, typename Iter = const char*>
class parser : public std::function<std::function<void(void)>(Iter& pos, Iter end)> {

    using parserFn = std::function<result(Iter& pos, Iter end)>;
    const parserFn parser_fn;

    parser(const parserFn& p) : parser_fn(p) { }
    parser(parserFn&& p) : parser_fn(std::move(p)) { }

public:

    result operator()(Iter& pos, Iter end) const {
      return parser_fn(pos, end);
    }

    static parser end() {
      return parser{[](Iter& pos, Iter const end){
          return pos == end ? success : fail;
      }};
    }

    parser(std::function<bool(Char)> matcher)
      : parser_fn([=](Iter& pos, Iter const end){
          auto start = pos;
          for (;pos != end && matcher(*pos); ++pos) { }
          return pos != start ? success : fail;
        })
    { }

    parser(const Char* arr)
      : parser_fn([=](Iter& pos, Iter const end){
          auto start = pos;
          auto it = arr;
          for (; *it != 0 && pos != end && *it == *pos; ++it, ++pos) { }
          if (*it == 0) { return success; }
          pos = start;
          return fail;
        })
    { }

    parser(Char c)
      : parser_fn([=](Iter& pos, Iter const end){
          if (pos == end) return fail;
          if (*pos == c) { ++pos; return success; }
          return fail;
        })
    { }

    // attach extra processor to parser, it may be an action
    // or more detailed parser
    const parser operator%(const parserFn process) const {
      return parser([=, p = *this](Iter& pos, Iter end)->result{
        auto start = pos;
        auto r = p(pos, end);
        if (r) {
          auto new_pos = start;
          auto rp = process(new_pos, pos);
          if (rp) { return [=]{r();rp();}; }
          pos = start;
        }
        return fail;
      });
    }

    // choice (parser1 | parser2 | parser3)
    const parser operator| (const parser p2) const {
      return parser([=, p1 = *this] (Iter& pos, Iter end)->result{
        auto r = p1(pos, end);
        if (r) return r;
        return p2(pos, end);
      });
    }

    // sequence parser1, parser2, parser3
    const parser operator+ (const parser p2) const {
      return parser([=, p1 = *this] (Iter& pos, Iter end)->result{
        auto start = pos;
        auto r1 = p1(pos, end);
        if (!r1) return fail;
        auto r2 = p2(pos, end);
        if (!r2) { pos = start; return fail;}
        return [=]{r1();r2();};
      });
    }

    // skip parser1 >> parser2, parse parser1, then drop it and parse parser2
    const parser operator>> (const parser p2) const {
      return parser([=, p1 = *this] (Iter& pos, Iter end)->result{
        auto start = pos;
        auto r1 = p1(pos, end);
        if (!r1) return fail;
        auto r2 = p2(pos, end);
        if (!r2) { pos = start; return fail;}
        return r2;
      });
    }

    // lookahead parser1 << parser2, parser1 succeeds only if parser2 returns success too
    const parser operator<< (const parser p2) const {
      return parser([=, p1 = *this] (Iter& pos, Iter end)->result{
        auto start = pos;
        auto r1 = p1(pos, end);
        if (!r1) return fail;
        auto before_p2 = pos;
        auto r2 = p2(pos, end);
        if (!r2) { pos = start; return fail;}
        pos = before_p2;
        return r1;;
      });
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

    // optional: ~parser, if parsed succefully - good, cannot parse - good too :)
    const parser operator~() const {
      return parser([p = *this] (Iter& pos, Iter end)->result{
        auto r = p(pos, end);
        if (r) return r;
        return success;
      });
    }

    // parser.repeat(), by default repeat 0 or more times
    // alternatives: [from ... +inf) - exactly <from> or more
    //               [0..to] - exactly <to> or less, down to 0
    //               [from..to] - exactly <from> or more, but less or equal <to>
    const parser repeat(int from_times = 0, int to_times = -1) const {
      return parser([p=*this, from_times, to_times] (Iter& pos, Iter end)->result{
        int times = 0;
        auto start = pos;
        std::vector<result*> results;
        while(pos != end && (to_times == -1 || times <= to_times)) {
          auto r = p(pos, end);
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

private:
    template<typename T>
    using converter_fn = std::function<T(Iter pos, Iter end)>;

public:
    template<typename T>
    class converter{
    private:
      converter_fn<T> conv_fn;
      converter(converter_fn<T> c) : conv_fn(c) { }
      friend class parser;
    public:
      const parser operator%(std::function<result(T)> process) const {
        return parser([=, c=conv_fn](Iter& pos, Iter end)->result{
          return process(c(pos, end));
        });
      }
    };

    template<typename T, typename L>
    static const converter<T> make_converter(L conv) {
      return converter<T>{static_cast<converter_fn<T>>(conv)};
    }
};

}
