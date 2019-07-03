#include "comb_parser.h"
#include "charset.h"

#include <iostream>
#include <cstring>
#include <string>
#include <memory>

// useful shortcuts
namespace cp = comb_parser;

using result = cp::result;

using p = cp::parser<char, decltype(std::string{}.begin()) >;
using cs = cp::charset::charset;

const cs alpha{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};
const cs digit{"0123456789"};
const cs hexdigit{digit + cs{"ABCDEFabcdef"}};

// parsers for parts of URI
const p decimal = p{digit};
const p hex = p{hexdigit};

const p IPv4 = (decimal + p{'.'}).repeat(3,3) + decimal; // <d>.<d>.<d>.<d>
const p IPv6 = ([]{
    // [hex:hex:hex:hex] | [::hex:hex] | [hex:hex::hex] | [hex:hex:hex::]
    const p hextet{hex + p{':'}};
    const p v1 = hextet.repeat(7,7) + hex;
    const p v2 = p{"::"} >> ~(hextet.repeat() + hex);
    const p v3 = hextet.repeat() + p{':'} + ~(hextet.repeat() + hex);
    return (v1 | v2 | v3);
  }());

const p FQDN = p{alpha + digit + cs{".-"}};

const p uri_host = p{'['} + IPv6 + p{']'}
                 | IPv4
                 | FQDN;

const p uri_schema = p{!cs{":/?#"}};

const p uri_path_item = p{!cs{"/?#"}};

// generic data converters
const auto to_number_conv = [](auto pos, auto end) -> cp::converter_result<int>::type {
  size_t idx;
  auto str = std::string(pos, end);
  int result = std::stoi(str, &idx);
  if (idx == str.length()) {
    return [=]{return result;};
  }
  return cp::converter_result<int>::fail;
};

const auto to_string_conv = [](auto pos, auto end) -> cp::converter_result<std::string>::type{
  auto str = std::string(pos, end);
  return [=]{return str;};
};

// converters specialized for parser p
const auto to_number = p::make_converter<int>(to_number_conv);
const auto to_string = p::make_converter<std::string>(to_string_conv);

//structure for holdind result of parsing

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

// function for parsing URI
const p UriParser(uri_info& ui) {

    // uri = [schema ':'] ["//" authority] [path] ['?' params] ['#' fragment]
    // path = ['/' path_item?]*
    // params = [param '&'?]*

    const p schema = uri_schema % (p{"http"} | p{"https"} | p{"ftp"}) %
      (to_string % // using converter of matched substring to standalone string
        [&](auto str) // note, that if you return only one lambda, then you do not need '-> result'
        {
          return [=, &ui]{ ui.schema = str(); };
        }
      );
    
    const p port = decimal %
      (to_number % 
        [&](auto port)-> result // but if two or more possible lambdas, then '-> result' is mandatory
        { 
          if (port) { // check that we converted parsed number successfuly
            int p = port(); //get result of conversion
            if (p > 0 && p < 65536) { // check for valid range
              return [=, &ui]{ ui.port = p; };
            }
          }
          return cp::fail;
        }
      );

    const p host = uri_host %
      (to_string %
        [&](auto str)
        {
          return[=, &ui]{ ui.authority = str(); };
        }
      );

    const p host_port = host + ~(p{':'} >> port);

    const p authority = p{!cs{"/?#"}} // quick top-level parser
      % // detailed parser
      (host_port + p::end() /*parse to the end of chunk, i.e. match all authority substring*/ );

    const p path_item = uri_path_item %
      (to_string %
        [&](auto str) -> result
        {
          return [=, &ui]{ ui.path.push_back(str()); };
        }
      );

    const p path = (p{'/'} >> ~path_item).repeat();
    
    // Example of parsing with context shared between parsers
    
    // var=value;var=value

    using context = std::shared_ptr<param_s>;

    // parser with context
    using pc = p::with_context<context>;
    
    // get converters for pc parser
    // there are two ways:
    // 1. make from other convertor
    // 2. make from lambda
    const auto to_number_c = pc::from_converter(to_number);
    const auto to_string_c = pc::make_converter<std::string>(to_string_conv);
    
    const pc param_var = pc{p{!cs{"&=;#"}}} % (to_string_c %
      [] (auto s, auto param){
        param->name = s(); // store in context during parsing stage
        return cp::success; // return empty effect as flag of successful parsing
      });
    
    const pc param_number = pc{decimal} // convert p to pc
      % (to_number_c %
        [](auto num, auto param) -> cp::result {
          if (num) {
            return [=]{
              param->num = num();
              param->type = param_type::number;
            };
          }
          return cp::fail;
        }
      );

    const pc param_string = pc{p{!cs{"&;=#"}}}
      % (to_string_c %
        [](auto str, auto param){
          param->str = str();
          param->type = param_type::string;
          return cp::success;
        }
      );

    const pc param_pair = (param_var + (pc{p{'='}} >> (param_number | param_string))) %
      [&] (auto s, auto e, auto param) {
         return [=, &ui]{ // <-here we capture param to use it in applying effects stage
            // use context
            ui.params.push_back(*param);
         }; // <- here context will be destroyed
         // context will be destroyed on closure destruction even we do not apply affects
      };

    const p param_flag = p{!cs{"&=;#"}} %
      [&] (auto s, auto e){
        return [=, &ui]{
          ui.flags.push_back(std::string(s, e));
        };
      };

    const p param = p{!cs{"&#;"}} // high-level quick parser
      %
      ( // more detailed parser
        ( // make ordinary parser from context-parser by supplying context-generator
          param_pair([]()-> context {
              // on every parsing attempt, new context is generated
              return std::make_shared<param_s>();
              // you may return every time the same pointer, but
              // it will be used same for every parsing with constructed parser:
              // id est for every var=val parsing in every URI,
              // because this lamba is captured in internal parser closure.
            })
          | // or
          param_flag
        )
        +
        p::end() // should match whole high-level substring
      );
    const p params = (param + ~(p{'&'} | p{';'})).repeat();
    const p fragment = p{!cs{}} %
      [&] (auto s, auto e){
        return [=, &ui]{
          ui.fragment = std::string(s,e);
        };
      };

    const p uri = ~(schema + p{':'}) + ~(p{"//"} >> ~authority) + ~path + ~(p{'?'} >> params) + ~(p{'#'} >> ~fragment);
    return uri;

}

int main(int, char**)
{

    uri_info ui;

    const auto uri = UriParser(ui);

    std::string url = "http://[::1]:888/p1//p2///p3?arg1=213123&qwe=123123&asd;zxc&zzz=lkjh#fragment";

    auto start = url.begin();

    auto stop = url.end();

    const auto res = uri(start, stop);
    if (res) {
        res(); // execute actions, i.e. store corresponding items in ui structure

        using namespace std;

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
