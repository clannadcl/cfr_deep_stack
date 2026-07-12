#pragma once

#include <vector>

#include "game/poker/action.h"

namespace fisher::game::poker {

class NodeState;

class ActionResolver {
 public:
  static std::vector<Action> Resolve(const NodeState& state);
};

}  // namespace fisher::game::poker
