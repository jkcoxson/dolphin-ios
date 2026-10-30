add_subdirectory(fmt)
dolphin_disable_warnings(fmt)

if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 21)
  # fmt 10.2.1's chrono formatter fails immediate-function evaluation with
  # Apple Clang 21 (Xcode 26.6). Use fmt's constexpr compatibility path;
  # explicit FMT_STRING format strings still receive compile-time validation.
  target_compile_definitions(fmt PUBLIC FMT_CONSTEVAL=constexpr)
endif()
