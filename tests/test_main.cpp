#include "test_framework.hpp"

void runJsonTests();
void runUtilTests();
void runJwtTests();
void runCommandBuilderTests();

int main() {
    runJsonTests();
    runUtilTests();
    runJwtTests();
    runCommandBuilderTests();

    printf("%d checks, %d failures\n", testfw::gChecks, testfw::gFailures);
    return testfw::gFailures == 0 ? 0 : 1;
}
