#include "app_state.h"
#include <iostream>
#include <string>
#include <cstdlib>

void expect(bool condition, const char* msg) {
    if (!condition) {
        std::cerr << "Test failed: " << msg << std::endl;
        std::exit(1);
    }
}

void test_takeBufferAndClear_non_empty() {
    AppState state;
    state.appendSpan(L"Hello", 5);
    expect(state.copyBuffer() == L"Hello", "Buffer should contain 'Hello'");

    std::wstring taken = state.takeBufferAndClear();
    expect(taken == L"Hello", "taken string should be 'Hello'");
    expect(state.copyBuffer().empty(), "Buffer should be empty after takeBufferAndClear");
    std::cout << "test_takeBufferAndClear_non_empty passed!" << std::endl;
}

void test_takeBufferAndClear_empty() {
    AppState state;
    expect(state.copyBuffer().empty(), "Initial buffer should be empty");

    std::wstring taken = state.takeBufferAndClear();
    expect(taken.empty(), "taken string should be empty");
    expect(state.copyBuffer().empty(), "Buffer should remain empty");
    std::cout << "test_takeBufferAndClear_empty passed!" << std::endl;
}

int main() {
    test_takeBufferAndClear_non_empty();
    test_takeBufferAndClear_empty();

    std::cout << "All AppState tests passed!" << std::endl;
    return 0;
}
