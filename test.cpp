#include "comb_parser.h"
#include "charset.h"

#include <iostream>
#include <cstring>

using T = const char*;
using F = std::function<void(T, T)>;

F print_s(const char* name){
  return [=] (T const s, T const e) {
    T c = s;
    std::cout << name;
    for(; c != e; ++c) std::cout << *c;
    std::cout << std::endl;
  };
}

namespace cp = comb_parser;

using result = cp::result;

using p = cp::parser<>;
using cs = cp::charset::charset;

const cs alpha{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"};
const cs digit{"0123456789"};
const cs hexdigit{digit + cs{"ABCDEFabcdef"}};

const p IPv4 = (p{digit} + p{'.'}).repeat(3,3) + p{digit}; // <d>.<d>.<d>.<d>
const p IPv6 = ([]{
    // [hex:hex:hex:hex] | [::hex:hex] | [hex:hex::hex] | [hex:hex:hex::]
    const p hex{hexdigit};
    const p hextet{hex + p{':'}};
    const p v1 = hextet.repeat(7,7) + hex;
    const p v2 = p{"::"} >> ~(hextet.repeat() + hex);
    const p v3 = hextet.repeat() + p{':'} + ~(hextet.repeat() + hex);
    return (v1 | v2 | v3);
  }());

const p FQDN = p{alpha + digit + cs{".-"}};
const p uri_schema = p{!cs{":/?#"}};

const auto to_number = p::make_converter<int>([](auto pos, auto end){
  int num = 0;
  for(; pos != end; num = num*10 + (*pos++) - '0') { };
  return num;
});

const auto to_string = p::make_converter<std::string>([](auto pos, auto end){
  return std::string(pos, end);
});

const p UriParser(
    F const& schema_cb,
    F const& authority_cb,
    F const& port_cb,
    F const& path_item_cb,
    F const& param_name_cb,
    F const& param_value_cb,
    F const& param_flag_cb,
    F const& params_cb,
    F const& fragment_cb
){

    // uri = [schema ':'] ["//" authority] [path] ['?' params] ['#' fragment]
    // path = ['/' path_item?]*
    // params = [param '&'?]*

    const p schema = uri_schema % (p{"http"} | p{"https"} | p{"ftp"}) %
      (to_string %
        [](std::string str) -> result
        {
          return [=]{
            std::cout << "schema: " << str << std::endl;
          };
        }
      );

    const p port = p{digit} %
      (to_number % 
        [](int port)-> result
        { 
          if (port > 0 && port < 65536) {
            return [=]{std::cout << "p=" << port << std::endl;};
          }
          return cp::fail;
        }
      );

    const p host = (p{'['} + IPv6 + p{']'} | IPv4 | FQDN) %
      [&] (auto s, auto e) {return [=, &authority_cb] { authority_cb(s, e);};};

    const p host_port = host + ~(p{':'} >> port);

    const p authority = p{!cs{"/?#"}} % (host_port + p::end() /*parse to the end of chunk, i.e. match all authority substring*/ );

    const p path_item = p{!cs{"/?#"}} %
      [&] (auto s, auto e){return [=, &path_item_cb]{ path_item_cb(s, e);};};

    const p path = (p{'/'} >> ~path_item).repeat();
    
    // var=value;var=value
    const p param_var = p{!cs{"&=;#"}} %
      [&] (auto s, auto e){return [=, &param_name_cb]{ param_name_cb(s, e);};};
    
    const p param_value = p{!cs{"&;=#"}} %
      [&] (auto s, auto e){return [=, &param_value_cb]{ param_value_cb(s, e);};};

    const p param_pair = param_var + (p{'='} >> param_value);

    const p param_flag = p{!cs{"&=;#"}} %
      [&] (auto s, auto e){return [=, &param_flag_cb]{ param_flag_cb(s, e);};};

    const p param = p{!cs{"&#;"}} % ((param_pair | param_flag) + p::end());
    const p params = (param + ~(p{'&'} | p{';'})).repeat() %
      [&] (auto s, auto e){return [=, &params_cb]{params_cb(s, e);};};
    
    const p fragment = p{!cs{}} %
      [&] (auto s, auto e){return [=, &fragment_cb]{fragment_cb(s,e);};};

    const p uri = ~(schema + p{':'}) + ~(p{"//"} >> ~authority) + ~path + ~(p{'?'} >> params) + ~(p{'#'} >> ~fragment);
    return uri;
}

int main(int, char**)
{

    const F f1 = print_s("shema: ");
    const F f2 = print_s("authority: ");
    const F f3 = print_s("port: ");
    const F f4 = print_s("path_item: ");
    const F f5 = print_s("param_name: ");
    const F f6 = print_s("param_value: ");
    const F f7 = print_s("param_flag: ");
    const F f8 = print_s("params_all: ");
    const F f9 = print_s("fragment: ");

    const auto uri = UriParser(f1,f2,f3,f4,f5,f6, f7, f8, f9);

    const char *url = "http://abc.ru:888/p1//p2///p3?arg1=213123&qwe=123123#fragment";

    auto start = url;
    auto stop = url+std::strlen(url);

    const auto res = uri(start, stop);
    if (res) {
        res(); // execute actions, i.e. print uri parts
    }
    return 0;
}   
