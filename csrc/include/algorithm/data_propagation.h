#pragma once

#include <vector>

#include "game/poker/isomorphic_mapping.h"

namespace fisher::algorithm {

void PropagateReachForward(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child,
    const std::vector<float>& parent_reach,
    std::vector<float>* child_reach);

void PropagateCfvBackward(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child,
    const std::vector<float>& child_cfv,
    std::vector<float>* parent_cfv);

}  // namespace fisher::algorithm
