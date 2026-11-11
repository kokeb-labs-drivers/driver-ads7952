/**
 * @file literal_out_of_range.cpp
 * @brief Must NOT compile: `0x100_u8` does not fit a `uint8_t`, so the literal operator calls the
 * non-`constexpr` `detail::LiteralOutOfRange()` and the literal is not a constant (MISRA C++:2023
 * Rule 7.0.6).
 *
 * EXPECT: LiteralOutOfRange
 */
#include "ads7952_detail.hpp"

int main() {
  using ads7952::detail::operator""_u8;
  return static_cast<int>(0x100_u8);
}
