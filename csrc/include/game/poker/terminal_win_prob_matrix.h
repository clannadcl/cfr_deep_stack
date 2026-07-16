#pragma once

#include <vector>

#include "game/poker/game_basic.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_cards.h"

namespace fisher::game::poker {

class TerminalWinProbMatrix {
 public:
  TerminalWinProbMatrix(const GameBasic& game_basic, const PokerCards& board,
                        const IsomorphicMapping& mapping,
                        const SevenCardLookupTable& evaluator);

  int NumIsoHands() const;
  float WinProb(int hero_iso, int opponent_iso) const;
  float LoseProb(int hero_iso, int opponent_iso) const;
  float EquityDelta(int hero_iso, int opponent_iso) const;
  const PokerCards& Board() const;
  const std::vector<float>& WinProbData() const;
  const std::vector<float>& EquityDeltaData() const;

 private:
  void Build(const GameBasic& game_basic, const IsomorphicMapping& mapping,
             const SevenCardLookupTable& evaluator);
  void BuildEquityDelta();
  void ValidateIsoIndex(int iso_index) const;

  PokerCards board_;
  PokerRound round_;
  int num_iso_hands_ = 0;
  std::vector<float> win_prob_;
  std::vector<float> equity_delta_;
};

}  // namespace fisher::game::poker
