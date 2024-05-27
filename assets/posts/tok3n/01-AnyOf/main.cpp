#include "AnyOf.h"
#include <iostream>

int main()
{
    AnyOf<"ab"> parser;
    if (parser.parse("abc"))
        std::cout << "Success - abc\n";
    if (parser.parse("bca"))
        std::cout << "Success - bca\n";
    if (not parser.parse("cab"))
        std::cout << "Failure - cab\n";

    static_assert(parser.parse("abc"));
    static_assert(parser.parse("bca"));
    static_assert(not parser.parse("cab"));
}