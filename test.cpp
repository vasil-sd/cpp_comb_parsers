#include "comb_parser.h"
#include "charset.h"

#include <iostream>
#include <cstring>
#include <string>
#include <memory>

// useful shortcuts
namespace cp = comb_parser;

using result = cp::result;

// p - is basic context-less parser
using p = cp::parser<char, decltype(std::string{}.begin())>;

using cs = cp::charset::charset;

const cs alpha{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};
const cs digit{"0123456789"};
const cs hexdigit{digit + cs{"ABCDEFabcdef"}};

// parsers for parts of URI
const p decimal = p{digit};
const p hex = p{hexdigit};

const p IPv4 = repeat(decimal + p{'.'}, 3, 3) + decimal; // <d>.<d>.<d>.<d>
const p IPv6 = ([]{
    // [hex:hex:hex:hex] | [::hex:hex] | [hex:hex::hex] | [hex:hex:hex::]
    const p hextet{hex + p{':'}};
    const p v1 = repeat(hextet, 7,7) + hex;
    const p v2 = p{"::"} >> ~(repeat(hextet) + hex);
    const p v3 = repeat(hextet) + p{':'} + ~(repeat(hextet) + hex);
    return (v1 | v2 | v3);
  }());

const p FQDN = p{alpha + digit + cs{".-"}};

const p uri_host = p{'['} + IPv6 + p{']'}
                 | IPv4
                 | FQDN;

const p uri_schema = p{!cs{":/?#"}};

const p uri_path_item = p{!cs{"/?#"}};

//======================================================================================
// generic data converters
const auto to_number_conv = [](auto pos, auto end) -> cp::converter_result<int>::type {
  size_t idx;
  auto str = std::string(pos, end);
  int result = std::stoi(str, &idx);
  if (idx == str.length()) {
    return [=]{return result;};
  }
  return cp::converter_result<int>::fail; // converter may signal failure for next stage
  // next stage handler decides what to do with failed conversion, it may use
  // default value instead of converted one
};

const auto to_string_conv = [](auto pos, auto end) -> cp::converter_result<std::string>::type{
  auto str = std::string(pos, end);
  return [=]{return str;};
};
//----------------------------------------------------------------------------------------


//======================================================================
// converters specialized for parser p
// we should make specialized converters for each parser
// it is required for converters be able to pass context to
// next stage handler
const auto to_number = p::make_converter<int>(to_number_conv);
const auto to_string = p::make_converter<std::string>(to_string_conv);
//---------------------------------------------------------------------


//==============================================
//structure for holdind result of URI parsing

enum class param_type {
  number,
  string
};

struct param_s {
  param_type type;
  std::string name;
  std::string str;
  int num;
};

struct uri_info{
  std::string schema;
  std::string authority;
  int port;
  std::vector<std::string> path;
  std::vector<param_s> params;
  std::vector<std::string> flags;
  std::string fragment;
};

//----------------------------------------


//=========================================================
// defining main URI parser with context uri_info

using up = p::with_context<uri_info*>;

const auto to_number_u = up::from_converter(to_number);
const auto to_string_u = up::from_converter(to_string);

const up schema =
     up{uri_schema}                         // uplift basic parser
  % (up{"http"} | up{"https"} | up{"ftp"})  // extra filtering of protocols
  % (to_string_u %                          // using converter of matched substring to standalone string
      [](auto str, auto ui)                 // note, that if you return only one lambda, then you do not need '-> result'
        { return [=]{ ui->schema = str();  /* just store it */
        };} );

const up port =
     up{decimal}
  % (to_number_u                       // use converter
  % [](auto port, auto ui)-> result    // but if two or more possible lambdas, then '-> result' is mandatory
    { if (port) {                      // check that we converted parsed number successfuly
        int p = port();                // get result of conversion
        if (p > 0 && p < 65536) {      // check for valid range
          return [=]{ ui->port = p; }; // store port in outermost context, ie uri_info
        }}
      return cp::fail;                 // fail if could not convert substring to number of if port is out of range
    });

const up host =
    up{uri_host}
  % (to_string_u
  % [](auto str, auto ui)
        { return[=]{ ui->authority = str(); };});

const up host_port =
    up{host} + ~(up{':'} >> port); // host[:port]

const up authority =
    up{!cs{"/?#"}}            // quick top-level parser, according to RFC
  % (host_port +              // detailed parser after succeeded quick top-level parser
     up::end());              // parse to the end of chunk, i.e. match whole substring of outer parser

const up path_item =
    up{uri_path_item}                                  // uplevel basic parser
  % (to_string_u                                       // convert
  % [](auto str, auto ui) -> result
        { return [=]{ ui->path.push_back(str()); };}); // store in uir_info

const up path =
  repeat(up{'/'} >> ~path_item); // /p1/p2/p3

//========================================================================
// Here we can see use of several context simultaneously (lyered contexts)
// Use one more context (local to parser) with outermost uri_info

// build parser for params
// var=value;var=value;flag1;flag2

// usere of uri parser won't see this internal context
using local_context = std::shared_ptr<param_s>;

// parser with one more context in addition to uri_info in
// up parser
// (contexts are layered)
using pc = up::with_context<local_context>;
    
// get converters for pc parser
// there are two possibilities:
// 1. make from other convertor
// 2. make from lambda
// unfortunately we shoul make converters for each parser type :(
const auto to_number_c = pc::from_converter(to_number);
const auto to_string_c = pc::make_converter<std::string>(to_string_conv);


const pc param_var =
    pc{!cs{"&=;#"}}                // instead of uplifting basic parser, we may build one directly from cahrset
  % (to_string_c                   // using converter
  % [] (auto s, auto param, ...){  // ellipsis means that we do not bother deeper (outer) contexts
    param->name = s();             // store in context during parsing stage
    return cp::success;            // return empty effect cp::success as flag of successful parsing
  });
    
const pc param_number =
    pc{decimal}                                   // uplift basic parser
  % (to_number_c                                  // convert to number
  % [](auto num, auto param, ...) -> cp::result{  // because of two return paths with different lambdas we should help compiler with type deduction
      if (num) {                                  // check result of conversion
        return [=]{
          param->num = num();                     // store num
          param->type = param_type::number;       // and tag
        };}
      return cp::fail;});                         // fail parsing if cannot convert to num

const pc param_string = // parameter either number or string
    pc{!cs{"&;=#"}}
  % (to_string_c
  % [](auto str, auto param, ...){
      param->str = str();
      param->type = param_type::string;
      return cp::success;}); // strings are always parsed succesfully

const pc param_pair =
    (param_var + (pc{'='} >> (param_number | param_string))) // name=[number|string], note that numbers parsed in first place
  % [] (auto s, auto e, auto param, auto ui) {               // here we need all contexts
     return [=]{                                             // here we capture all contexts to use them in applying effects stage
        ui->params.push_back(*param);                        // store result in outermost context during stage of effects applying
     };                                                      // here local_context will be destroyed
    };                                                       // local_context will be destroyed on closure destruction even we do not apply affects

const up param_flag =
    up{!cs{"&=;#"}}                                              // As in RFC
  % [] (auto s, auto e, auto ui){                                // using uri_info context
        return [=]{ ui->flags.push_back(std::string(s, e));};};  // just store flag on effects applying stage

const up param =
      up{!cs{"&#;"}}                               // high-level quick parser
    % (                                            // begin more detailed parser
        (param_pair([](...)-> local_context {      // downlift pc to up parser by supplying context-generator (pop off innermost context)
              return std::make_shared<param_s>();  // on every parsing attempt, new context is generated
              // you may return every time the same pointer, but
              // it will be used every same for every parsing
              // id est for every var=val parsing in every URI,
              // because this lamba is captured in internal parser closure.
        })
       | // or
         param_flag
      )
      + up::end());   // match whole high-level substring
      
const up params =
    repeat(param + ~(up{'&'} | up{';'})); // param_or_flag1&param_or_flag2....

const up fragment =
    up{!cs{}}                         // cs{} - do not match any char, !cs{} - match any/every char
  % [] (auto s, auto e, auto ui){
        return [=]{ ui->fragment = std::string(s,e);};};


// final URI parser
const up uri = ~(schema + up{':'}) + ~(up{"//"} >> ~authority) + ~path + ~(up{'?'} >> params) + ~(up{'#'} >> ~fragment);

int main(int, char**)
{

    uri_info ui;

    std::string url = "http://[::1]:888/p1//p2///p3?arg1=213123&qwe=123123&asd;zxc&zzz=lkjh#fragment";

    auto start = url.begin();

    auto stop = url.end();

    const auto res = uri([&](){return &ui;})(start, stop);
    //                   ^- supply context   |
    //                                       \- supply iterators
    if (res) { // check if parsing was succesful
        res(); // execute actions, i.e. store corresponding items in ui structure

        using namespace std;

        // print 'em all!

        cout << "schema: " << ui.schema << endl;
        cout << "host: " << ui.authority << endl;
        cout << "port: " << ui.port << endl;
        cout << "path: ";
        for (auto p: ui.path) {
          cout << " / " << p;
        }
        cout << endl;
        cout << "params: " << endl;
        for (auto p: ui.params) {
          switch(p.type) {
            case param_type::string: cout << "  " << p.name << " : string = " << p.str << endl; break;
            case param_type::number: cout << "  " << p.name << " : int = " << p.num << endl; break;
          }
        }
        cout << "flags: " << endl;
        for (auto f: ui.flags) cout << "  " << f << endl;
        cout << "fragment: " << ui.fragment << endl;
    }
    return 0;
}   
