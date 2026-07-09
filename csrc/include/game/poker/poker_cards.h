#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "game/poker/poker_card.h"

namespace fisher::game::poker {

class PokerCards {
 public:
  PokerCards() = default;
  explicit PokerCards(const std::string& cards);
  explicit PokerCards(const std::vector<PokerCard>& cards);
  explicit PokerCards(const std::vector<uint8_t>& cards);

  static PokerCards GenerateDeck();

  const std::vector<PokerCard>& Cards() const;
  std::size_t Size() const;
  bool Empty() const;
  bool Contains(PokerCard card) const;
  bool HasCollision(const PokerCards& other) const;
  std::string ToString() const;

  void Add(PokerCard card);
  PokerCards Merge(const PokerCards& other) const;
  PokerCards Difference(const PokerCards& other) const;
  std::vector<PokerCards> Combinations(int k) const;

 private:
  void SortAndValidateUnique();
  void AppendCombinations(int start, int remaining,
                          std::vector<PokerCard>* current,
                          std::vector<PokerCards>* output) const;

  std::vector<PokerCard> cards_;
};

}  // namespace fisher::game::poker
