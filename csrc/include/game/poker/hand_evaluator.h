#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#define _Bool bool
#include <hand_index.h>
#undef _Bool
}

#include "game/poker/poker_card.h"
#include "game/poker/poker_cards.h"

namespace fisher::game::poker {

class PokerHandEvaluator {
 public:
  static constexpr int kSevenCards = 7;

  static uint32_t Evaluate7Raw(const PokerCards& cards);
  static uint32_t Evaluate7Raw(const std::array<PokerCard, kSevenCards>& cards);
  static uint32_t Evaluate7Raw(const uint8_t cards[kSevenCards]);
};

class SevenCardLookupTable {
 public:
  SevenCardLookupTable();
  explicit SevenCardLookupTable(std::string cache_path);
  ~SevenCardLookupTable();

  SevenCardLookupTable(const SevenCardLookupTable&) = delete;
  SevenCardLookupTable& operator=(const SevenCardLookupTable&) = delete;

  hand_index_t Size() const;
  uint16_t NumStrengths() const;
  hand_index_t IsomorphicIndex(const PokerCards& cards) const;
  hand_index_t IsomorphicIndex(
      const uint8_t cards[PokerHandEvaluator::kSevenCards]) const;
  uint16_t Evaluate7(const PokerCards& cards) const;
  uint16_t Evaluate7(
      const uint8_t cards[PokerHandEvaluator::kSevenCards]) const;
  uint16_t StrengthByIndex(hand_index_t index) const;
  const std::vector<uint16_t>& StrengthTable() const;

 private:
  bool LoadTable();
  void BuildTable();
  void SaveTable() const;
  void ValidateIndex(hand_index_t index) const;

  hand_indexer_t indexer_;
  bool initialized_ = false;
  std::string cache_path_;
  std::vector<uint16_t> strength_by_index_;
  uint16_t num_strengths_ = 0;
};

}  // namespace fisher::game::poker
