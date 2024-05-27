#pragma once
#include "ParserFamily.h"
#include <type_traits>

template <class P> 
concept Parser =
    requires { typename std::integral_constant<ParserFamily, P::family>; } and
    static_cast<int>(P::family) > static_cast<int>(ParserFamily::None) and
    static_cast<int>(P::family) < static_cast<int>(ParserFamily::END) and
    (std::is_empty_v<P>) and
    requires (void(fn)(P)) { fn({}); } and // Implicitly default constructible
    requires (Input input)
    {
        typename P::result_type;
        { P::parse(input) } -> IsResult<typename P::result_type>;
        { P::lookahead(input) } -> IsResult<void>;
    };
