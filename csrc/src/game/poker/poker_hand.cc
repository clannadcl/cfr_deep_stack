#include "game/poker/poker_hand.h"

#include <stdexcept>
#include <vector>

namespace fisher::game::poker {
namespace {

PokerCard CardAtForHand(const PokerCards& cards, std::size_t index) {
  if (cards.Size() != 2) {
    throw std::invalid_argument("PokerHand requires exactly 2 cards");
  }
  return cards.Cards()[index];
}

}  // namespace

PokerHand::PokerHand(PokerCard first, PokerCard second)
    : high_card_(first.Value() > second.Value() ? first : second),
      low_card_(first.Value() > second.Value() ? second : first) {
  if (first.Value() == second.Value()) {
    throw std::invalid_argument("PokerHand cannot contain duplicate cards");
  }
}

PokerHand::PokerHand(const PokerCards& cards)
    : PokerHand(CardAtForHand(cards, 0), CardAtForHand(cards, 1)) {}

PokerHand::PokerHand(const std::string& cards) : PokerHand(PokerCards(cards)) {}

PokerCard PokerHand::HighCard() const { return high_card_; }

PokerCard PokerHand::LowCard() const { return low_card_; }

std::array<PokerCard, 2> PokerHand::Cards() const {
  return {high_card_, low_card_};
}

PokerCards PokerHand::ToPokerCards() const {
  return PokerCards(std::vector<PokerCard>{high_card_, low_card_});
}

std::string PokerHand::ToString() const { return ToPokerCards().ToString(); }

bool PokerHand::Contains(PokerCard card) const {
  return high_card_.Value() == card.Value() ||
         low_card_.Value() == card.Value();
}

bool PokerHand::HasCollision(const PokerCards& cards) const {
  for (PokerCard card : cards.Cards()) {
    if (Contains(card)) {
      return true;
    }
  }
  return false;
}

}  // namespace fisher::game::poker
