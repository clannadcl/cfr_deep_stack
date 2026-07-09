#include "game/poker/poker_card.h"

#include <stdexcept>

namespace fisher::game::poker {
namespace {

constexpr char kRankChars[] = "23456789TJQKA";
constexpr char kSuitChars[] = "cdhs";

uint8_t ParseRank(char rank) {
  for (uint8_t index = 0; index < 13; ++index) {
    if (kRankChars[index] == rank) return index;
  }
  throw std::invalid_argument("Invalid poker card rank");
}

uint8_t ParseSuit(char suit) {
  for (uint8_t index = 0; index < 4; ++index) {
    if (kSuitChars[index] == suit) return index;
  }
  throw std::invalid_argument("Invalid poker card suit");
}

}  // namespace

PokerCard::PokerCard(uint8_t card) : card_(card) {
  if (card_ >= 52) {
    throw std::invalid_argument("PokerCard value must be in [0, 51]");
  }
}

PokerCard::PokerCard(const std::string& card) : card_(0) {
  if (card.size() != 2) {
    throw std::invalid_argument("PokerCard string must have length 2");
  }

  const uint8_t rank = ParseRank(card[0]);
  const uint8_t suit = ParseSuit(card[1]);
  card_ = static_cast<uint8_t>(rank * 4 + suit);
}

uint8_t PokerCard::Value() const { return card_; }

PokerRank PokerCard::Rank() const {
  return static_cast<PokerRank>(card_ / 4);
}

PokerSuit PokerCard::Suit() const {
  return static_cast<PokerSuit>(card_ % 4);
}

std::string PokerCard::ToString() const {
  return {RankToChar(Rank()), SuitToChar(Suit())};
}

char RankToChar(PokerRank rank) {
  const auto index = static_cast<uint8_t>(rank);
  if (index >= 13) {
    throw std::invalid_argument("Invalid poker rank");
  }
  return kRankChars[index];
}

char SuitToChar(PokerSuit suit) {
  const auto index = static_cast<uint8_t>(suit);
  if (index >= 4) {
    throw std::invalid_argument("Invalid poker suit");
  }
  return kSuitChars[index];
}

}  // namespace fisher::game::poker
