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

using p = comb_parser::parser<>;
using cs = comb_parser::charset::charset;

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

const p UriParser(
    F const& schema_cb,
    F const& authority_cb,
    F const& port_cb,
    F const& path_item_cb,
    F const& params_cb,
    F const& fragment_cb
){

    // uri = [schema ':'] ["//" authority] [path] ['?' params] ['#' fragment]
    // path = ['/' path_item?]*
    // params = [param '&'?]*

    const p schema = p{!cs{":/?#"}} %
      [&] (auto s, auto e){ return [=,&schema_cb]{schema_cb(s, e);};};

    const p port = p{digit} %
      [&] (auto s, auto e) {return [=, &port_cb]{ port_cb(s,e);};};

    const p host = (p{'['} + IPv6 + p{']'} | IPv4 | FQDN) %
      [&] (auto s, auto e) {return [=, &authority_cb] { authority_cb(s, e);};};

    const p host_port = host + ~(p{':'} >> port);

    const p authority = p{!cs{"/?#"}} % (host_port + p::end() /*parse to the end of chunk, i.e. match all authority substring*/ );

    const p path_item = p{!cs{"/?#"}} %
      [&] (auto s, auto e){return [=, &path_item_cb]{ path_item_cb(s, e);};};

    const p path = (p{'/'} >> ~path_item).repeat();

    const p param = p{!cs{"&#"}};
    const p params = (param + ~p{'&'}).repeat() %
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
    const F f5 = print_s("params: ");
    const F f6 = print_s("fragment: ");

    const auto uri = UriParser(f1,f2,f3,f4,f5,f6);

    const char *url = "http://abc.ru:888/p1//p2///p3?arg1=213123&qwe=123123#fragment";

    auto start = url;
    auto stop = url+std::strlen(url);

    const auto result = uri(start, stop);
    if (result) {
        result(); // execute actions, i.e. print uri parts
    }

    return 0;
}   
