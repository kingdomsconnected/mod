#include <exception>
#include <iostream>

void SoulEvaluatorTests();
void AlchemyCatalogTests();
void AlchemyEntryContinuationTests();
void AlchemyProtocolTests();
void AlchemyThermalTests();
void AlchemyCharacterEvaluatorTests();
void AlchemyModuleTests();

int RunAlchemySuite() {
    try {
        SoulEvaluatorTests();
        AlchemyCatalogTests();
        AlchemyEntryContinuationTests();
        AlchemyProtocolTests();
        AlchemyThermalTests();
        AlchemyCharacterEvaluatorTests();
        AlchemyModuleTests();
        std::cout << "Alchemy tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "Alchemy tests failed: " << error.what() << '\n';
        return 1;
    }
}
