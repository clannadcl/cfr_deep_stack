#pragma once

#include <array>
#include <string>

#include "game/poker/poker_card.h"
#include "game/poker/poker_cards.h"

namespace fisher::game::poker {

class PokerHand {
 public:
  PokerHand(PokerCard first, PokerCard second);
  explicit PokerHand(const PokerCards& cards);
  explicit PokerHand(const std::string& cards);

  PokerCard HighCard() const;
  PokerCard LowCard() const;
  std::array<PokerCard, 2> Cards() const;
  PokerCards ToPokerCards() const;
  std::string ToString() const;
  bool Contains(PokerCard card) const;
  bool HasCollision(const PokerCards& cards) const;

 private:
  PokerCard high_card_;
  PokerCard low_card_;
};

}  // namespace fisher::game::poker
