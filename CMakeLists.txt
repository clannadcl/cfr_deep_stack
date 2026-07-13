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
  csrc/src/algorithm/cfr_storage.cc
  csrc/src/game/kuhn.cc
  csrc/src/game/poker/abstracted_action.cc
  csrc/src/game/poker/action_resolver.cc
  csrc/src/game/poker/action.cc
  csrc/src/game/poker/belief.cc
  csrc/src/game/poker/game_basic.cc
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
