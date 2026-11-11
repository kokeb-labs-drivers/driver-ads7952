/**
 * @file ads7952_detail.hpp
 * @brief Compile-time helpers of the ADS7952 driver: exact hook matching and enumeration checks.
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is `constexpr` or used only in unevaluated operands, so it adds no code to the
 * run-time path (handbook C++20 profile, KB-CPP20-006, -008, -025, -026).
 */
#ifndef ADS7952_DETAIL_HPP
#define ADS7952_DETAIL_HPP

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace ads7952::detail {

/**
 * @brief Matches a port hook against exactly one signature, `R(Args...) noexcept`, as a member
 * function of `Port` (`const` or not) (KB-CPP20-006).
 *
 * `ExactHook<Port, Sig>::Take(&Port::Hook)` is well-formed only when `&Port::Hook` names a member
 * function of exactly that type: the address of an overloaded or template hook resolves to the
 * one that matches, and a hook with a narrower or different parameter, another result type or no
 * `noexcept` has no match. No value is converted. Declared only: it appears in
 * requires-expressions, never in evaluated code.
 *
 * @tparam Port The port class.
 * @tparam Sig The hook's function type, `noexcept`; a `Sig` without `noexcept` has no definition.
 */
template <typename Port, typename Sig>
  requires std::is_class_v<Port> && std::is_function_v<Sig>
struct ExactHook;

/// The `noexcept` signatures: the only ones a hook may have.
template <typename Port, typename R, typename... Args>
  requires std::is_class_v<Port>
struct ExactHook<Port, R(Args...) noexcept> {
  /// Takes the address of a non-`const` hook of exactly this signature.
  static void Take(R (Port::*hook)(Args...) noexcept) noexcept;
  /// Takes the address of a `const` hook of exactly this signature.
  static void Take(R (Port::*hook)(Args...) const noexcept) noexcept;
};

/// A class type: what `kAlwaysFalse` depends on, the port or another class of the branch.
template <typename T>
concept ClassType = std::is_class_v<T>;

/// `false` for every class `T`: the condition of a `static_assert` in a branch that must not be
/// instantiated (KB-CPP20-008).
template <ClassType>
inline constexpr bool kAlwaysFalse = false;

/// A scoped or unscoped enumeration: what the enumeration helpers take (KB-CPP20-005, -026).
template <typename E>
concept Enumeration = std::is_enum_v<E>;

/**
 * @brief The value of an enumerator in its underlying type, our stand-in for C++23
 * `std::to_underlying` (KB-CPP20-002, -026).
 */
template <Enumeration E>
[[nodiscard]] constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept {
  return static_cast<std::underlying_type_t<E>>(e);
}

/**
 * @brief The last enumerator of a contiguous enumeration that starts at 0; specialised next to each
 * enumeration that `IsEnumerator()` checks (KB-CPP20-026).
 */
template <Enumeration E>
struct EnumLast;

/**
 * @brief Whether `e` holds an enumerator of a contiguous enumeration that starts at 0. A value made
 * by `static_cast` from an integer can be anything its underlying type holds (KB-CPP20-025).
 */
template <Enumeration E>
[[nodiscard]] constexpr bool IsEnumerator(E e) noexcept {
  return static_cast<uint32_t>(ToUnderlying(e)) <=
         static_cast<uint32_t>(ToUnderlying(EnumLast<E>::kValue));
}

/**
 * @brief Not `constexpr`: a literal operator below calls it for a value its type cannot hold, so
 * that literal is not a constant expression and fails to compile. Never called at run time.
 */
inline void LiteralOutOfRange() noexcept {}

/**
 * @brief A `uint8_t` integer constant, `3_u8`: an argument of exactly the parameter's type, with no
 * implicit conversion of an `int` literal (MISRA C++:2023 Rule 7.0.6). A value above 0xFF does not
 * compile.
 */
consteval uint8_t operator""_u8(unsigned long long value) noexcept {
  if (value > 0xFFU) {
    LiteralOutOfRange();
  }
  return static_cast<uint8_t>(value);
}

/// A `uint16_t` integer constant, `0x0FFF_u16`; a value above 0xFFFF does not compile (Rule 7.0.6).
consteval uint16_t operator""_u16(unsigned long long value) noexcept {
  if (value > 0xFFFFU) {
    LiteralOutOfRange();
  }
  return static_cast<uint16_t>(value);
}

/**
 * @brief A `uint32_t` integer constant, `1000_u32`: on targets where `uint32_t` is `unsigned long`
 * a `U` literal is still another type (Rule 7.0.6). A value above 0xFFFF'FFFF does not compile.
 */
consteval uint32_t operator""_u32(unsigned long long value) noexcept {
  if (value > 0xFFFF'FFFFU) {
    LiteralOutOfRange();
  }
  return static_cast<uint32_t>(value);
}

static_assert(0xFF_u8 == 0xFFU && 0xFFFF_u16 == 0xFFFFU && 0xFFFF'FFFF_u32 == 0xFFFF'FFFFU);

} // namespace ads7952::detail

#endif // ADS7952_DETAIL_HPP
