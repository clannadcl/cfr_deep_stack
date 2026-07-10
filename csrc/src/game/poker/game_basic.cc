#include "game/poker/game_basic.h"

#include <stdexcept>
#include <vector>

namespace fisher::game::poker {

bool IsPokerCardsCollided(const PokerCards& first, const PokerCards& second) {
  return first.HasCollision(second);
}

GameBasic::GameBasic(RakeConfig rake_config) : rake_config_(rake_config) {
  ValidateRakeConfig();

  deck_ = PokerCards::GenerateDeck().Cards();
  flat_hand_index_by_card_pair_.fill(-1);
  all_hands_.reserve(kNumHands);

  for (uint8_t low = 0; low < kDeckSize - 1; ++low) {
    for (uint8_t high = low + 1; high < kDeckSize; ++high) {
      PokerHand hand{PokerCard(high), PokerCard(low)};
      const int index = static_cast<int>(all_hands_.size());
      all_hands_.push_back(hand);
      flat_hand_index_by_card_pair_[HandMapKey(hand.HighCard(),
                                               hand.LowCard())] = index;
    }
  }

  if (static_cast<int>(all_hands_.size()) != kNumHands) {
    throw std::runtime_error("Unexpected Hold'em hand count");
  }
}

int GameBasic::NumPlayers() const { return kNumPlayers; }

int GameBasic::NumHoleCards() const { return kNumHoleCards; }

const std::vector<PokerCard>& GameBasic::Deck() const { return deck_; }

const std::vector<PokerHand>& GameBasic::AllHands() const {
  return all_hands_;
}

const RakeConfig& GameBasic::Rake() const { return rake_config_; }

const PokerCardsIsomorphicBoardIndex& GameBasic::BoardIndexer() const {
  return board_indexer_;
}

const PokerCardsIsomorphicHoleBoardIndex& GameBasic::HoleBoardIndexer() const {
  return hole_board_indexer_;
}

const PokerHand& GameBasic::HandFromIndex(int index) const {
  if (index < 0 || index >= static_cast<int>(all_hands_.size())) {
    throw std::invalid_argument("Poker hand index is out of range");
  }
  return all_hands_[static_cast<std::size_t>(index)];
}

int GameBasic::HandIndex(const PokerHand& hand) const {
  ValidateHand(hand);
  const int index =
      flat_hand_index_by_card_pair_[HandMapKey(hand.HighCard(),
                                               hand.LowCard())];
  if (index < 0) {
    throw std::runtime_error("Poker hand is missing from index map");
  }
  return index;
}

int GameBasic::HandIndex(const PokerCards& hand) const {
  return HandIndex(PokerHand(hand));
}

PokerRound GameBasic::BoardRound(const PokerCards& board) const {
  ValidateBoard(board);
  return RoundFromBoardSize(board.Size());
}

void GameBasic::ValidateBoard(const PokerCards& board) const {
  RoundFromBoardSize(board.Size());
}

void GameBasic::ValidateHand(const PokerHand& hand) const {
  if (hand.HighCard().Value() == hand.LowCard().Value()) {
    throw std::invalid_argument("Poker hand cannot contain duplicate cards");
  }
}

void GameBasic::ValidateHand(const PokerCards& hand) const {
  ValidateHand(PokerHand(hand));
}

void GameBasic::ValidateHandAndBoard(const PokerHand& hand,
                                     const PokerCards& board) const {
  ValidateHand(hand);
  ValidateBoard(board);
  if (hand.HasCollision(board)) {
    throw std::invalid_argument("Poker hand and board collide");
  }
}

void GameBasic::ValidateHandAndBoard(const PokerCards& hand,
                                     const PokerCards& board) const {
  ValidateHandAndBoard(PokerHand(hand), board);
}

std::vector<int> GameBasic::ValidHands(const PokerCards& board) const {
  ValidateBoard(board);

  std::vector<int> valid_hands;
  valid_hands.reserve(all_hands_.size());
  for (std::size_t index = 0; index < all_hands_.size(); ++index) {
    if (!all_hands_[index].HasCollision(board)) {
      valid_hands.push_back(static_cast<int>(index));
    }
  }
  return valid_hands;
}

hand_index_t GameBasic::BoardIsomorphicIndex(const PokerCards& board) const {
  ValidateBoard(board);
  if (board.Empty()) {
    throw std::invalid_argument("Board isomorphic index requires board cards");
  }
  return board_indexer_.GetIndex(board);
}

hand_index_t GameBasic::HoleBoardIsomorphicIndex(
    const PokerHand& hand, const PokerCards& board) const {
  ValidateHandAndBoard(hand, board);
  return hole_board_indexer_.GetIndex(hand, board);
}

int GameBasic::HandMapKey(PokerCard high_card, PokerCard low_card) {
  return static_cast<int>(high_card.Value()) * kDeckSize +
         static_cast<int>(low_card.Value());
}

void GameBasic::ValidateRakeConfig() const {
  if (rake_config_.percentage < 0.0 || rake_config_.percentage > 1.0) {
    throw std::invalid_argument("Rake percentage must be in [0, 1]");
  }
  if (rake_config_.cap < 0.0) {
    throw std::invalid_argument("Rake cap must be non-negative");
  }
}

}  // namespace fisher::game::poker
