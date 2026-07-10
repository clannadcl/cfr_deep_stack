#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/poker/poker_card.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_cards_isomorphic_index.h"
#include "game/poker/poker_hand.h"

namespace fisher::game::poker {

struct RakeConfig {
  bool enabled = false;
  double percentage = 0.0;
  double cap = 0.0;
};

bool IsPokerCardsCollided(const PokerCards& first, const PokerCards& second);

class GameBasic {
 public:
  static constexpr int kNumPlayers = 2;
  static constexpr int kNumHoleCards = 2;
  static constexpr int kDeckSize = 52;
  static constexpr int kNumHands = 1326;

  explicit GameBasic(RakeConfig rake_config = RakeConfig{});

  int NumPlayers() const;
  int NumHoleCards() const;
  const std::vector<PokerCard>& Deck() const;
  const std::vector<PokerHand>& AllHands() const;
  const RakeConfig& Rake() const;

  const PokerCardsIsomorphicBoardIndex& BoardIndexer() const;
  const PokerCardsIsomorphicHoleBoardIndex& HoleBoardIndexer() const;

  const PokerHand& HandFromIndex(int index) const;
  int HandIndex(const PokerHand& hand) const;
  int HandIndex(const PokerCards& hand) const;

  PokerRound BoardRound(const PokerCards& board) const;
  void ValidateBoard(const PokerCards& board) const;
  void ValidateHand(const PokerHand& hand) const;
  void ValidateHand(const PokerCards& hand) const;
  void ValidateHandAndBoard(const PokerHand& hand,
                            const PokerCards& board) const;
  void ValidateHandAndBoard(const PokerCards& hand,
                            const PokerCards& board) const;

  std::vector<int> ValidHands(const PokerCards& board) const;
  hand_index_t BoardIsomorphicIndex(const PokerCards& board) const;
  hand_index_t HoleBoardIsomorphicIndex(const PokerHand& hand,
                                        const PokerCards& board) const;

 private:
  static int HandMapKey(PokerCard high_card, PokerCard low_card);
  void ValidateRakeConfig() const;

  std::vector<PokerCard> deck_;
  std::vector<PokerHand> all_hands_;
  std::array<int, kDeckSize * kDeckSize> flat_hand_index_by_card_pair_;
  RakeConfig rake_config_;
  PokerCardsIsomorphicBoardIndex board_indexer_;
  PokerCardsIsomorphicHoleBoardIndex hole_board_indexer_;
};

}  // namespace fisher::game::poker
