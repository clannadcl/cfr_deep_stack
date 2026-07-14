#include "algorithm/data_propagation.h"

#include <cstddef>
#include <stdexcept>

#include "game/poker/game_basic.h"
#include "game/poker/poker_cards_isomorphic_index.h"

namespace fisher::algorithm {
namespace {

void ValidateAdjacentBoardTransition(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child) {
  const int parent_cards = game::poker::BoardCardsForRound(parent.Round());
  const int child_cards = game::poker::BoardCardsForRound(child.Round());
  if (child_cards != parent_cards + 1) {
    throw std::invalid_argument("Child board must be the next street");
  }
  if (parent.RawBoard().Size() >= child.RawBoard().Size()) {
    throw std::invalid_argument("Child board must contain more cards");
  }
  for (std::size_t index = 0; index < parent.RawBoard().Size(); ++index) {
    if (parent.RawBoard().Cards()[index].Value() !=
        child.RawBoard().Cards()[index].Value()) {
      throw std::invalid_argument(
          "Child board must contain parent board as prefix");
    }
  }
}

void ValidateInputSize(const std::vector<float>& values, int expected_size,
                       const char* message) {
  if (values.size() != static_cast<std::size_t>(expected_size)) {
    throw std::invalid_argument(message);
  }
}

}  // namespace

void PropagateReachForward(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child,
    const std::vector<float>& parent_reach,
    std::vector<float>* child_reach) {
  if (child_reach == nullptr) {
    throw std::invalid_argument("Child reach output cannot be null");
  }
  ValidateAdjacentBoardTransition(parent, child);
  ValidateInputSize(parent_reach, parent.NumIsoHands(),
                    "Parent reach size mismatch");

  child_reach->assign(static_cast<std::size_t>(child.NumIsoHands()), 0.0f);
  for (int raw_index = 0; raw_index < game::poker::GameBasic::kNumHands;
       ++raw_index) {
    const int parent_iso = parent.RawToIso(raw_index);
    const int child_iso = child.RawToIso(raw_index);
    if (parent_iso < 0 || child_iso < 0) {
      continue;
    }
    (*child_reach)[static_cast<std::size_t>(child_iso)] +=
        parent_reach[static_cast<std::size_t>(parent_iso)] /
        static_cast<float>(parent.RawHandCount(parent_iso));
  }
}

void PropagateCfvBackward(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child,
    const std::vector<float>& child_cfv,
    std::vector<float>* parent_cfv) {
  if (parent_cfv == nullptr) {
    throw std::invalid_argument("Parent CFV output cannot be null");
  }
  ValidateAdjacentBoardTransition(parent, child);
  ValidateInputSize(child_cfv, child.NumIsoHands(), "Child CFV size mismatch");

  parent_cfv->assign(static_cast<std::size_t>(parent.NumIsoHands()), 0.0f);
  for (int raw_index = 0; raw_index < game::poker::GameBasic::kNumHands;
       ++raw_index) {
    const int parent_iso = parent.RawToIso(raw_index);
    const int child_iso = child.RawToIso(raw_index);
    if (parent_iso < 0 || child_iso < 0) {
      continue;
    }
    (*parent_cfv)[static_cast<std::size_t>(parent_iso)] +=
        child_cfv[static_cast<std::size_t>(child_iso)] /
        static_cast<float>(child.RawHandCount(child_iso));
  }
}

}  // namespace fisher::algorithm
