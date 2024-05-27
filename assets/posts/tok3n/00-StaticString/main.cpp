#include "StaticString.h"
#include <iostream>

template <StaticString str>
void foo()
{
    for (char c : str.data)
        std::cout << c;
    std::cout << "\n";
};

int main()
{
    foo<"abc">();  // > abc
    foo<"wxyz">(); // > wxyz
}
