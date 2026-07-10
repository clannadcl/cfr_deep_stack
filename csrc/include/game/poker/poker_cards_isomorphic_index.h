#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#define _Bool bool
#include <hand_index.h>
#undef _Bool
}

#include "game/poker/poker_card.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"

namespace fisher::game::poker {

enum class PokerRound : uint8_t {
  kPreflop = 0,
  kFlop = 1,
  kTurn = 2,
  kRiver = 3,
};

struct HoleBoardCards {
  PokerCards hole_cards;
  PokerCards board;
};

int BoardCardsForRound(PokerRound round);
PokerRound RoundFromBoardSize(std::size_t board_cards);

class PokerCardsIsomorphicBoardIndex {
 public:
  PokerCardsIsomorphicBoardIndex();
  ~PokerCardsIsomorphicBoardIndex();

  PokerCardsIsomorphicBoardIndex(const PokerCardsIsomorphicBoardIndex&) =
      delete;
  PokerCardsIsomorphicBoardIndex& operator=(
      const PokerCardsIsomorphicBoardIndex&) = delete;

  hand_index_t GetIndex(const PokerCards& board) const;
  PokerCards GetBoard(PokerRound round, hand_index_t index) const;
  PokerCards GetNewBoardCards(const PokerCards& previous_board,
                              hand_index_t next_round_index) const;
  hand_index_t RoundSize(PokerRound round) const;

 private:
  std::vector<PokerCard> UnindexRaw(PokerRound round, hand_index_t index) const;

  std::array<hand_indexer_t, 4> indexers_;
  std::array<bool, 4> initialized_;
};

class PokerCardsIsomorphicHoleBoardIndex {
 public:
  PokerCardsIsomorphicHoleBoardIndex();
  ~PokerCardsIsomorphicHoleBoardIndex();

  PokerCardsIsomorphicHoleBoardIndex(
      const PokerCardsIsomorphicHoleBoardIndex&) = delete;
  PokerCardsIsomorphicHoleBoardIndex& operator=(
      const PokerCardsIsomorphicHoleBoardIndex&) = delete;

  hand_index_t GetIndex(const PokerCards& hole_cards,
                        const PokerCards& board) const;
  hand_index_t GetIndex(const PokerHand& hand, const PokerCards& board) const;
  HoleBoardCards GetCards(PokerRound round, hand_index_t index) const;
  hand_index_t RoundSize(PokerRound round) const;

 private:
  std::vector<PokerCard> UnindexRaw(PokerRound round, hand_index_t index) const;

  std::array<hand_indexer_t, 4> indexers_;
  std::array<bool, 4> initialized_;
};

}  // namespace fisher::game::poker
