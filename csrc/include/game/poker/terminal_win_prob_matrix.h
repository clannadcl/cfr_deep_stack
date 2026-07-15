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

 private:
  void Build(const GameBasic& game_basic, const IsomorphicMapping& mapping,
             const SevenCardLookupTable& evaluator);
  void AccumulateRawPair(const GameBasic& game_basic,
                         const SevenCardLookupTable& evaluator,
                         int hero_raw, int opponent_raw, int hero_iso,
                         int opponent_iso, float normalization);
  void ValidateIsoIndex(int iso_index) const;
  std::vector<uint8_t> RemainingDeckForPair(const PokerHand& hero,
                                            const PokerHand& opponent) const;
  float RawWinProbability(const SevenCardLookupTable& evaluator,
                          const PokerHand& hero,
                          const PokerHand& opponent,
                          const std::vector<uint8_t>& remaining_deck) const;

  PokerCards board_;
  PokerRound round_;
  int num_iso_hands_ = 0;
  std::vector<float> win_prob_;
};

}  // namespace fisher::game::poker
