#include <exception>
#include <iostream>

void SmithingTests();

int RunSmithingSuite() {
    try {
        SmithingTests();
        std::cout << "Smithing tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "Smithing tests failed: " << error.what() << '\n';
        return 1;
    }
}
