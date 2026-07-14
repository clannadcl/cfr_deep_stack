#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_cards_isomorphic_index.h"

namespace fisher::game::poker {

class IsomorphicMapping {
 public:
  static constexpr int kInvalidIsoIndex = -1;

  IsomorphicMapping(const GameBasic& game_basic, const PokerCards& board,
                    const std::vector<bool>& root_possible_hands);

  const PokerCards& RawBoard() const;
  const PokerCards& IsoBoard() const;
  PokerRound Round() const;
  const std::array<int, 4>& SuitMapping() const;

  int NumIsoHands() const;
  int RawToIso(int raw_hand_index) const;
  int IsoToRepresentativeRaw(int iso_index) const;
  int RawHandCount(int iso_index) const;

  const std::vector<int>& RawIndexToIsoIndex() const;
  const std::vector<int>& IsoIndexToRawIndex() const;
  const std::vector<int>& RawHandCountForEachIsoHand() const;

 private:
  void ValidateRawHandIndex(int raw_hand_index) const;
  void ValidateIsoIndex(int iso_index) const;

  PokerCards raw_board_;
  PokerCards iso_board_;
  PokerRound round_;
  std::array<int, 4> suit_mapping_;
  std::vector<int> raw_index_to_iso_index_;
  std::vector<int> iso_index_to_raw_index_;
  std::vector<int> raw_hand_count_for_each_iso_hand_;
};

class IsomorphicMappingTable {
 public:
  IsomorphicMappingTable(GameBasic game_basic, const PokerBelief& root_belief);

  const IsomorphicMapping& Get(const PokerCards& board);
  bool Contains(const PokerCards& board) const;

 private:
  static std::vector<bool> BuildRootPossibleHands(
      const PokerBelief& root_belief);

  GameBasic game_basic_;
  std::vector<bool> root_possible_hands_;
  std::unordered_map<std::string, std::unique_ptr<IsomorphicMapping>> mappings_;
};

}  // namespace fisher::game::poker
