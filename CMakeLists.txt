cmake_minimum_required(VERSION 3.20)

project(fisher LANGUAGES C CXX)

option(FISHER_BUILD_PYBIND "Build the Python pybind11 extension" ON)
option(FISHER_BUILD_TESTS "Build C++ unit tests" ON)
option(FISHER_USE_OPENBLAS "Use OpenBLAS CBLAS for matrix-vector kernels" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(fisher_core STATIC
  third_party/hand-isomorphism/src/deck.c
  third_party/hand-isomorphism/src/hand_index.c
  csrc/src/algorithm/cfr.cc
  csrc/src/algorithm/cfr_storage.cc
  csrc/src/algorithm/data_propagation.cc
  csrc/src/game/kuhn.cc
  csrc/src/game/poker/abstracted_action.cc
  csrc/src/game/poker/action_resolver.cc
  csrc/src/game/poker/action.cc
  csrc/src/game/poker/belief.cc
  csrc/src/game/poker/game_basic.cc
  csrc/src/game/poker/hand_evaluator.cc
  csrc/src/game/poker/terminal_cfv_calculator.cc
  csrc/src/game/poker/terminal_win_prob_matrix.cc
  csrc/src/game/poker/isomorphic_mapping.cc
  csrc/src/game/poker/node_state.cc
  csrc/src/game/poker/poker_cards_isomorphic_index.cc
  csrc/src/game/poker/poker_card.cc
  csrc/src/game/poker/poker_cards.cc
  csrc/src/game/poker/poker_hand.cc
  csrc/src/game/poker/poker_tree.cc
  csrc/src/game/poker/subgame_setup.cc
  csrc/src/game/poker/tree_abstracted_bets.cc
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

target_compile_definitions(fisher_core
  PRIVATE
    FISHER_LOOKUP_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/cache"
)

if(FISHER_USE_OPENBLAS)
  set(FISHER_OPENBLAS_HINTS
    $ENV{OPENBLAS_ROOT}
    /opt/homebrew/opt/openblas
    /usr/local/opt/openblas
  )

  find_path(OPENBLAS_INCLUDE_DIR
    NAMES cblas.h
    HINTS ${FISHER_OPENBLAS_HINTS}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
  )
  find_library(OPENBLAS_LIBRARY
    NAMES openblas
    HINTS ${FISHER_OPENBLAS_HINTS}
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
  )

  if(NOT OPENBLAS_INCLUDE_DIR)
    find_path(OPENBLAS_INCLUDE_DIR NAMES cblas.h)
  endif()
  if(NOT OPENBLAS_LIBRARY)
    find_library(OPENBLAS_LIBRARY NAMES openblas)
  endif()

  if(OPENBLAS_INCLUDE_DIR AND OPENBLAS_LIBRARY)
    message(STATUS "Using OpenBLAS: ${OPENBLAS_LIBRARY}")
    target_include_directories(fisher_core PRIVATE ${OPENBLAS_INCLUDE_DIR})
    target_link_libraries(fisher_core PUBLIC ${OPENBLAS_LIBRARY})
    target_compile_definitions(fisher_core PRIVATE FISHER_USE_OPENBLAS=1)
  else()
    message(WARNING "FISHER_USE_OPENBLAS=ON but OpenBLAS was not found; using built-in matrix-vector fallback")
  endif()
endif()

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

  add_executable(cfr_storage_test
    csrc/tests/algorithm/cfr_storage_test.cc
  )

  target_link_libraries(cfr_storage_test
    PRIVATE
      fisher_core
  )

  add_test(NAME cfr_storage_test COMMAND cfr_storage_test)

  add_executable(data_propagation_test
    csrc/tests/algorithm/data_propagation_test.cc
  )

  target_link_libraries(data_propagation_test
    PRIVATE
      fisher_core
  )

  add_test(NAME data_propagation_test COMMAND data_propagation_test)

  add_executable(action_test
    csrc/tests/game/poker/action_test.cc
  )

  target_link_libraries(action_test
    PRIVATE
      fisher_core
  )

  add_test(NAME action_test COMMAND action_test)

  add_executable(abstracted_action_test
    csrc/tests/game/poker/abstracted_action_test.cc
  )

  target_link_libraries(abstracted_action_test
    PRIVATE
      fisher_core
  )

  add_test(NAME abstracted_action_test COMMAND abstracted_action_test)

  add_executable(poker_card_test
    csrc/tests/game/poker/poker_card_test.cc
  )

  target_link_libraries(poker_card_test
    PRIVATE
      fisher_core
  )

  add_test(NAME poker_card_test COMMAND poker_card_test)

  add_executable(belief_test
    csrc/tests/game/poker/belief_test.cc
  )

  target_link_libraries(belief_test
    PRIVATE
      fisher_core
  )

  add_test(NAME belief_test COMMAND belief_test)

  add_executable(poker_hand_test
    csrc/tests/game/poker/poker_hand_test.cc
  )

  target_link_libraries(poker_hand_test
    PRIVATE
      fisher_core
  )

  add_test(NAME poker_hand_test COMMAND poker_hand_test)

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

  add_executable(game_basic_test
    csrc/tests/game/poker/game_basic_test.cc
  )

  target_link_libraries(game_basic_test
    PRIVATE
      fisher_core
  )

  add_test(NAME game_basic_test COMMAND game_basic_test)

  add_executable(hand_evaluator_test
    csrc/tests/game/poker/hand_evaluator_test.cc
  )

  target_link_libraries(hand_evaluator_test
    PRIVATE
      fisher_core
  )

  add_test(NAME hand_evaluator_test COMMAND hand_evaluator_test)

  add_executable(terminal_cfv_calculator_test
    csrc/tests/game/poker/terminal_cfv_calculator_test.cc
  )

  target_link_libraries(terminal_cfv_calculator_test
    PRIVATE
      fisher_core
  )

  add_test(NAME terminal_cfv_calculator_test
           COMMAND terminal_cfv_calculator_test)

  add_executable(terminal_win_prob_matrix_test
    csrc/tests/game/poker/terminal_win_prob_matrix_test.cc
  )

  target_link_libraries(terminal_win_prob_matrix_test
    PRIVATE
      fisher_core
  )

  add_test(NAME terminal_win_prob_matrix_test
           COMMAND terminal_win_prob_matrix_test)

  add_executable(isomorphic_mapping_test
    csrc/tests/game/poker/isomorphic_mapping_test.cc
  )

  target_link_libraries(isomorphic_mapping_test
    PRIVATE
      fisher_core
  )

  add_test(NAME isomorphic_mapping_test COMMAND isomorphic_mapping_test)

  add_executable(tree_abstracted_bets_test
    csrc/tests/game/poker/tree_abstracted_bets_test.cc
  )

  target_link_libraries(tree_abstracted_bets_test
    PRIVATE
      fisher_core
  )

  add_test(NAME tree_abstracted_bets_test
           COMMAND tree_abstracted_bets_test)

  add_executable(node_state_test
    csrc/tests/game/poker/node_state_test.cc
  )

  target_link_libraries(node_state_test
    PRIVATE
      fisher_core
  )

  add_test(NAME node_state_test COMMAND node_state_test)

  add_executable(poker_tree_test
    csrc/tests/game/poker/poker_tree_test.cc
  )

  target_link_libraries(poker_tree_test
    PRIVATE
      fisher_core
  )

  add_test(NAME poker_tree_test COMMAND poker_tree_test)

  add_executable(subgame_setup_test
    csrc/tests/game/poker/subgame_setup_test.cc
  )

  target_link_libraries(subgame_setup_test
    PRIVATE
      fisher_core
  )

  add_test(NAME subgame_setup_test COMMAND subgame_setup_test)
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
