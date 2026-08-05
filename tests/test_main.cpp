#include "test_framework.hpp"

void runJsonTests();
void runUtilTests();
void runJwtTests();
void runCommandBuilderTests();
void runUrlFetchTests();

int main() {
    runJsonTests();
    runUtilTests();
    runJwtTests();
    runCommandBuilderTests();
    runUrlFetchTests();

    printf("%d checks, %d failures\n", testfw::gChecks, testfw::gFailures);
    return testfw::gFailures == 0 ? 0 : 1;
}
