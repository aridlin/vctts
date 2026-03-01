#include "app_state.h"
#include <iostream>
#include <string>
#include <cassert>

void test_takeBufferAndClear_non_empty() {
    AppState state;
    state.appendSpan(L"Hello", 5);
    assert(state.copyBuffer() == L"Hello");

    std::wstring taken = state.takeBufferAndClear();
    assert(taken == L"Hello");
    assert(state.copyBuffer().empty());
    std::cout << "test_takeBufferAndClear_non_empty passed!" << std::endl;
}

void test_takeBufferAndClear_empty() {
    AppState state;
    assert(state.copyBuffer().empty());

    std::wstring taken = state.takeBufferAndClear();
    assert(taken.empty());
    assert(state.copyBuffer().empty());
    std::cout << "test_takeBufferAndClear_empty passed!" << std::endl;
}

int main() {
    test_takeBufferAndClear_non_empty();
    test_takeBufferAndClear_empty();

    std::cout << "All AppState tests passed!" << std::endl;
    return 0;
}
