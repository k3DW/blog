#include "AllOf.h"
#include "AnyOf.h"
#include "NoneOf.h"
#include <iostream>

int main()
{
    {
        AnyOf<"ab"> parser;
        if (parser.parse("abc"))
            std::cout << "Success - abc\n";
        if (parser.parse("bca"))
            std::cout << "Success - bca\n";
        if (not parser.parse("cab"))
            std::cout << "Failure - cab\n";
        std::cout << "\n";

        static_assert(parser.parse("abc"));
        static_assert(parser.parse("bca"));
        static_assert(not parser.parse("cab"));
    }

    {
        NoneOf<"ab"> parser;
        if (not parser.parse("abc"))
            std::cout << "Failure - abc\n";
        if (not parser.parse("bca"))
            std::cout << "Failure - bca\n";
        if (parser.parse("cab"))
            std::cout << "Success - cab\n";
        std::cout << "\n";

        static_assert(not parser.parse("abc"));
        static_assert(not parser.parse("bca"));
        static_assert(parser.parse("cab"));
    }

    {
        AllOf<"ab"> parser;
        if (parser.parse("abc"))
            std::cout << "Success - abc\n";
        if (not parser.parse("bca"))
            std::cout << "Failure - bca\n";
        if (not parser.parse("cab"))
            std::cout << "Failure - cab\n";
        std::cout << "\n";

        static_assert(parser.parse("abc"));
        static_assert(not parser.parse("bca"));
        static_assert(not parser.parse("cab"));
    }
}