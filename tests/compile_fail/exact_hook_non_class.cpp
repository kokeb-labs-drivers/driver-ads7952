/**
 * @file exact_hook_non_class.cpp
 * @brief Must NOT compile: `detail::ExactHook` matches hooks of a port class only, so a non-class
 * "port" is rejected by its constraint (handbook KB-CPP20-005).
 *
 * EXPECT: ExactHook
 */
#include "ads7952_detail.hpp"

int main() {
  return sizeof(ads7952::detail::ExactHook<int, bool() noexcept>*) > 0U ? 0 : 1;
}
