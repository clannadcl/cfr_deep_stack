#include "game/poker/poker_cards.h"

#include <stdexcept>

namespace fisher::game::poker {

PokerCards::PokerCards(const std::string& cards) {
  if (cards.size() % 2 != 0) {
    throw std::invalid_argument("PokerCards string length must be even");
  }

  cards_.reserve(cards.size() / 2);
  for (std::size_t index = 0; index < cards.size(); index += 2) {
    cards_.emplace_back(cards.substr(index, 2));
  }
  ValidateUnique();
}

PokerCards::PokerCards(const std::vector<PokerCard>& cards) : cards_(cards) {
  ValidateUnique();
}

PokerCards::PokerCards(const std::vector<uint8_t>& cards) {
  cards_.reserve(cards.size());
  for (uint8_t card : cards) {
    cards_.emplace_back(card);
  }
  ValidateUnique();
}

PokerCards PokerCards::GenerateDeck() {
  std::vector<PokerCard> deck;
  deck.reserve(52);
  for (uint8_t card = 0; card < 52; ++card) {
    deck.emplace_back(card);
  }
  return PokerCards(deck);
}

const std::vector<PokerCard>& PokerCards::Cards() const { return cards_; }

std::size_t PokerCards::Size() const { return cards_.size(); }

bool PokerCards::Empty() const { return cards_.empty(); }

bool PokerCards::Contains(PokerCard card) const {
  for (PokerCard current : cards_) {
    if (current.Value() == card.Value()) return true;
  }
  return false;
}

bool PokerCards::HasCollision(const PokerCards& other) const {
  for (PokerCard card : cards_) {
    if (other.Contains(card)) {
      return true;
    }
  }
  return false;
}

std::string PokerCards::ToString() const {
  std::string output;
  output.reserve(cards_.size() * 2);
  for (PokerCard card : cards_) {
    output += card.ToString();
  }
  return output;
}

void PokerCards::Add(PokerCard card) {
  if (Contains(card)) {
    throw std::invalid_argument("Cannot add duplicate poker card");
  }
  cards_.push_back(card);
}

PokerCards PokerCards::Merge(const PokerCards& other) const {
  if (HasCollision(other)) {
    throw std::invalid_argument("Cannot merge poker cards with collision");
  }

  std::vector<PokerCard> merged = cards_;
  merged.insert(merged.end(), other.cards_.begin(), other.cards_.end());
  return PokerCards(merged);
}

PokerCards PokerCards::Difference(const PokerCards& other) const {
  std::vector<PokerCard> difference;
  difference.reserve(cards_.size());
  for (PokerCard card : cards_) {
    if (!other.Contains(card)) {
      difference.push_back(card);
    }
  }
  return PokerCards(difference);
}

std::vector<PokerCards> PokerCards::Combinations(int k) const {
  if (k < 0 || static_cast<std::size_t>(k) > cards_.size()) {
    throw std::invalid_argument("Combination size is out of range");
  }

  std::vector<PokerCards> output;
  std::vector<PokerCard> current;
  current.reserve(static_cast<std::size_t>(k));
  AppendCombinations(/*start=*/0, k, &current, &output);
  return output;
}

void PokerCards::ValidateUnique() const {
  for (std::size_t left = 0; left < cards_.size(); ++left) {
    for (std::size_t right = left + 1; right < cards_.size(); ++right) {
      if (cards_[left].Value() == cards_[right].Value()) {
        throw std::invalid_argument("PokerCards cannot contain duplicate cards");
      }
    }
  }
}

void PokerCards::AppendCombinations(int start, int remaining,
                                    std::vector<PokerCard>* current,
                                    std::vector<PokerCards>* output) const {
  if (remaining == 0) {
    output->emplace_back(*current);
    return;
  }

  const int max_start = static_cast<int>(cards_.size()) - remaining;
  for (int index = start; index <= max_start; ++index) {
    current->push_back(cards_[static_cast<std::size_t>(index)]);
    AppendCombinations(index + 1, remaining - 1, current, output);
    current->pop_back();
  }
}

}  // namespace fisher::game::poker
