#pragma once

#include <cstdint>
#include <string>

namespace fisher::game::poker {

enum class PokerRank : uint8_t {
  kTwo = 0,
  kThree = 1,
  kFour = 2,
  kFive = 3,
  kSix = 4,
  kSeven = 5,
  kEight = 6,
  kNine = 7,
  kTen = 8,
  kJack = 9,
  kQueen = 10,
  kKing = 11,
  kAce = 12,
};

enum class PokerSuit : uint8_t {
  kClubs = 0,
  kDiamonds = 1,
  kHearts = 2,
  kSpades = 3,
};

class PokerCard {
 public:
  explicit PokerCard(uint8_t card);
  explicit PokerCard(const std::string& card);

  uint8_t Value() const;
  PokerRank Rank() const;
  PokerSuit Suit() const;
  std::string ToString() const;

 private:
  uint8_t card_;
};

char RankToChar(PokerRank rank);
char SuitToChar(PokerSuit suit);

}  // namespace fisher::game::poker
