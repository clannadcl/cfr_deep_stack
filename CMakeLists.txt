cmake_minimum_required(VERSION 3.20)

project(fisher LANGUAGES C CXX)

option(FISHER_BUILD_PYBIND "Build the Python pybind11 extension" ON)
option(FISHER_BUILD_TESTS "Build C++ unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(fisher_core STATIC
  third_party/hand-isomorphism/src/deck.c
  third_party/hand-isomorphism/src/hand_index.c
  csrc/src/algorithm/cfr.cc
  csrc/src/game/kuhn.cc
  csrc/src/game/poker/poker_cards_isomorphic_index.cc
  csrc/src/game/poker/poker_card.cc
  csrc/src/game/poker/poker_cards.cc
)

target_include_directories(fisher_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/csrc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hand-isomorphism/src
)

target_compile_options(fisher_core
  PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>
)

if(FISHER_BUILD_TESTS)
  enable_testing()

  add_executable(kuhn_cfr_test
    csrc/tests/algorithm/cfr_test.cc
  )

  target_link_libraries(kuhn_cfr_test
    PRIVATE
      fisher_core
  )

  add_test(NAME kuhn_cfr_test COMMAND kuhn_cfr_test)

  add_executable(poker_card_test
    csrc/tests/game/poker/poker_card_test.cc
  )

  target_link_libraries(poker_card_test
    PRIVATE
      fisher_core
  )

  add_test(NAME poker_card_test COMMAND poker_card_test)

  add_executable(poker_cards_test
    csrc/tests/game/poker/poker_cards_test.cc
  )

  target_link_libraries(poker_cards_test
    PRIVATE
      fisher_core
  )

  add_test(NAME poker_cards_test COMMAND poker_cards_test)

  add_executable(poker_cards_isomorphic_index_test
    csrc/tests/game/poker/poker_cards_isomorphic_index_test.cc
  )

  target_link_libraries(poker_cards_isomorphic_index_test
    PRIVATE
      fisher_core
  )

  add_test(NAME poker_cards_isomorphic_index_test
           COMMAND poker_cards_isomorphic_index_test)
endif()

if(FISHER_BUILD_PYBIND)
  find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
  find_package(pybind11 CONFIG REQUIRED)

  pybind11_add_module(_core
    csrc/src/binding/pybind.cc
  )

  target_link_libraries(_core
    PRIVATE
      fisher_core
  )

  set_target_properties(_core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/pysrc/fisher
  )
endif()
