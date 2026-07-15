#include "game/poker/hand_evaluator.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fisher::game::poker {
namespace {

constexpr int kRanks = 13;
constexpr int kSuits = 4;
constexpr int kCategoryShift = 26;
constexpr uint32_t kCacheMagic = 0x46374556;  // F7EV
constexpr uint32_t kCacheVersion = 1;
constexpr hand_index_t kExpectedSevenCardIsoSize = 6009159;
constexpr uint16_t kExpectedSevenCardStrengths = 4824;

struct CacheHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t table_size;
  uint16_t num_strengths;
  uint16_t entry_size;
};

uint32_t KeepNMostSignificantBits(uint32_t value, int count) {
  uint32_t result = 0;
  for (int index = 0; index < count; ++index) {
    if (value == 0) {
      break;
    }
    const uint32_t bit = uint32_t{1} << (31 - __builtin_clz(value));
    value ^= bit;
    result |= bit;
  }
  return result;
}

uint32_t FindStraight(uint32_t rankset) {
  constexpr uint32_t kWheel = 0b1'0000'0000'1111;
  const uint32_t straight =
      rankset & (rankset << 1) & (rankset << 2) & (rankset << 3) &
      (rankset << 4);
  if (straight != 0) {
    return KeepNMostSignificantBits(straight, 1);
  }
  if ((rankset & kWheel) == kWheel) {
    return uint32_t{1} << 3;
  }
  return 0;
}

std::string DefaultCachePath() {
#ifdef FISHER_LOOKUP_CACHE_DIR
  return std::string(FISHER_LOOKUP_CACHE_DIR) + "/seven_card_lookup_v1.bin";
#else
  return "seven_card_lookup_v1.bin";
#endif
}

void ValidateUniqueSevenCards(
    const uint8_t cards[PokerHandEvaluator::kSevenCards]) {
  for (int left = 0; left < PokerHandEvaluator::kSevenCards; ++left) {
    if (cards[left] >= 52) {
      throw std::invalid_argument("Evaluate7 card id must be in [0, 51]");
    }
    for (int right = left + 1; right < PokerHandEvaluator::kSevenCards;
         ++right) {
      if (cards[left] == cards[right]) {
        throw std::invalid_argument("Evaluate7 cards must be unique");
      }
    }
  }
}

std::array<uint8_t, PokerHandEvaluator::kSevenCards> ToCardIds(
    const PokerCards& cards) {
  if (cards.Size() != PokerHandEvaluator::kSevenCards) {
    throw std::invalid_argument("Evaluate7 requires exactly 7 cards");
  }

  std::array<uint8_t, PokerHandEvaluator::kSevenCards> ids{};
  const std::vector<PokerCard>& card_vector = cards.Cards();
  for (int index = 0; index < PokerHandEvaluator::kSevenCards; ++index) {
    ids[static_cast<std::size_t>(index)] =
        card_vector[static_cast<std::size_t>(index)].Value();
  }
  return ids;
}

}  // namespace

uint32_t PokerHandEvaluator::Evaluate7Raw(const PokerCards& cards) {
  const std::array<uint8_t, kSevenCards> ids = ToCardIds(cards);
  return Evaluate7Raw(ids.data());
}

uint32_t PokerHandEvaluator::Evaluate7Raw(
    const std::array<PokerCard, kSevenCards>& cards) {
  std::array<uint8_t, kSevenCards> ids{};
  for (int index = 0; index < kSevenCards; ++index) {
    ids[static_cast<std::size_t>(index)] =
        cards[static_cast<std::size_t>(index)].Value();
  }
  return Evaluate7Raw(ids.data());
}

uint32_t PokerHandEvaluator::Evaluate7Raw(const uint8_t cards[kSevenCards]) {
  ValidateUniqueSevenCards(cards);

  uint32_t rankset = 0;
  std::array<uint32_t, kSuits> rankset_by_suit{};
  std::array<uint32_t, 5> rankset_of_count{};
  std::array<int, kRanks> rank_count{};

  for (int index = 0; index < kSevenCards; ++index) {
    const uint8_t card = cards[index];
    const int rank = card / kSuits;
    const int suit = card % kSuits;
    rankset |= uint32_t{1} << rank;
    rankset_by_suit[static_cast<std::size_t>(suit)] |= uint32_t{1} << rank;
    ++rank_count[static_cast<std::size_t>(rank)];
  }

  for (int rank = 0; rank < kRanks; ++rank) {
    rankset_of_count[static_cast<std::size_t>(
                         rank_count[static_cast<std::size_t>(rank)])] |=
        uint32_t{1} << rank;
  }

  int flush_suit = -1;
  for (int suit = 0; suit < kSuits; ++suit) {
    if (__builtin_popcount(rankset_by_suit[static_cast<std::size_t>(suit)]) >=
        5) {
      flush_suit = suit;
    }
  }

  const uint32_t straight = FindStraight(rankset);
  if (flush_suit >= 0) {
    const uint32_t straight_flush =
        FindStraight(rankset_by_suit[static_cast<std::size_t>(flush_suit)]);
    if (straight_flush != 0) {
      return (uint32_t{8} << kCategoryShift) | straight_flush;
    }
    return (uint32_t{5} << kCategoryShift) |
           KeepNMostSignificantBits(
               rankset_by_suit[static_cast<std::size_t>(flush_suit)], 5);
  }

  if (rankset_of_count[4] != 0) {
    const uint32_t kicker =
        KeepNMostSignificantBits(rankset ^ rankset_of_count[4], 1);
    return (uint32_t{7} << kCategoryShift) | (rankset_of_count[4] << 13) |
           kicker;
  }
  if (__builtin_popcount(rankset_of_count[3]) == 2) {
    const uint32_t trips = KeepNMostSignificantBits(rankset_of_count[3], 1);
    const uint32_t pair = rankset_of_count[3] ^ trips;
    return (uint32_t{6} << kCategoryShift) | (trips << 13) | pair;
  }
  if (rankset_of_count[3] != 0 && rankset_of_count[2] != 0) {
    const uint32_t pair = KeepNMostSignificantBits(rankset_of_count[2], 1);
    return (uint32_t{6} << kCategoryShift) | (rankset_of_count[3] << 13) |
           pair;
  }
  if (straight != 0) {
    return (uint32_t{4} << kCategoryShift) | straight;
  }
  if (rankset_of_count[3] != 0) {
    const uint32_t kickers = KeepNMostSignificantBits(rankset_of_count[1], 2);
    return (uint32_t{3} << kCategoryShift) | (rankset_of_count[3] << 13) |
           kickers;
  }
  if (__builtin_popcount(rankset_of_count[2]) >= 2) {
    const uint32_t pairs = KeepNMostSignificantBits(rankset_of_count[2], 2);
    const uint32_t kicker = KeepNMostSignificantBits(rankset ^ pairs, 1);
    return (uint32_t{2} << kCategoryShift) | (pairs << 13) | kicker;
  }
  if (rankset_of_count[2] != 0) {
    const uint32_t kickers = KeepNMostSignificantBits(rankset_of_count[1], 3);
    return (uint32_t{1} << kCategoryShift) | (rankset_of_count[2] << 13) |
           kickers;
  }
  return KeepNMostSignificantBits(rankset, 5);
}

SevenCardLookupTable::SevenCardLookupTable()
    : SevenCardLookupTable(DefaultCachePath()) {}

SevenCardLookupTable::SevenCardLookupTable(std::string cache_path)
    : cache_path_(std::move(cache_path)) {
  const uint8_t cards_per_round[] = {PokerHandEvaluator::kSevenCards};
  initialized_ = hand_indexer_init(1, cards_per_round, &indexer_);
  if (!initialized_) {
    throw std::runtime_error("Failed to initialize 7-card isomorphic indexer");
  }
  if (!LoadTable()) {
    BuildTable();
    SaveTable();
  }
}

SevenCardLookupTable::~SevenCardLookupTable() {
  if (initialized_) {
    hand_indexer_free(&indexer_);
  }
}

hand_index_t SevenCardLookupTable::Size() const {
  return hand_indexer_size(&indexer_, 0);
}

uint16_t SevenCardLookupTable::NumStrengths() const { return num_strengths_; }

hand_index_t SevenCardLookupTable::IsomorphicIndex(
    const PokerCards& cards) const {
  const std::array<uint8_t, PokerHandEvaluator::kSevenCards> ids =
      ToCardIds(cards);
  return hand_index_last(&indexer_, ids.data());
}

uint16_t SevenCardLookupTable::Evaluate7(const PokerCards& cards) const {
  return StrengthByIndex(IsomorphicIndex(cards));
}

uint16_t SevenCardLookupTable::StrengthByIndex(hand_index_t index) const {
  ValidateIndex(index);
  return strength_by_index_[static_cast<std::size_t>(index)];
}

const std::vector<uint16_t>& SevenCardLookupTable::StrengthTable() const {
  return strength_by_index_;
}

bool SevenCardLookupTable::LoadTable() {
  if (cache_path_.empty()) {
    return false;
  }

  std::ifstream input(cache_path_, std::ios::binary);
  if (!input) {
    return false;
  }

  CacheHeader header{};
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!input || header.magic != kCacheMagic ||
      header.version != kCacheVersion ||
      header.table_size != static_cast<uint64_t>(Size()) ||
      header.table_size != kExpectedSevenCardIsoSize ||
      header.num_strengths != kExpectedSevenCardStrengths ||
      header.entry_size != sizeof(uint16_t)) {
    return false;
  }

  std::vector<uint16_t> table(static_cast<std::size_t>(header.table_size));
  input.read(reinterpret_cast<char*>(table.data()),
             static_cast<std::streamsize>(table.size() * sizeof(uint16_t)));
  if (!input) {
    return false;
  }

  char extra = 0;
  input.read(&extra, 1);
  if (input.gcount() != 0) {
    return false;
  }

  strength_by_index_ = std::move(table);
  num_strengths_ = header.num_strengths;
  return true;
}

void SevenCardLookupTable::BuildTable() {
  const hand_index_t size = Size();
  if (size > static_cast<hand_index_t>(std::vector<uint16_t>().max_size())) {
    throw std::runtime_error("7-card lookup table is too large");
  }

  strength_by_index_.assign(static_cast<std::size_t>(size), 0);
  std::vector<uint32_t> raw_by_index(static_cast<std::size_t>(size), 0);
  std::vector<uint32_t> unique_raw_values;
  unique_raw_values.reserve(static_cast<std::size_t>(size));

  std::array<uint8_t, PokerHandEvaluator::kSevenCards> cards{};
  for (hand_index_t index = 0; index < size; ++index) {
    if (!hand_unindex(&indexer_, 0, index, cards.data())) {
      throw std::runtime_error("Failed to unindex 7-card isomorphic hand");
    }
    const uint32_t raw_value = PokerHandEvaluator::Evaluate7Raw(cards.data());
    raw_by_index[static_cast<std::size_t>(index)] = raw_value;
    unique_raw_values.push_back(raw_value);
  }

  std::sort(unique_raw_values.begin(), unique_raw_values.end());
  unique_raw_values.erase(
      std::unique(unique_raw_values.begin(), unique_raw_values.end()),
      unique_raw_values.end());
  if (unique_raw_values.size() > UINT16_MAX) {
    throw std::runtime_error("Too many 7-card hand strengths for uint16_t");
  }
  num_strengths_ = static_cast<uint16_t>(unique_raw_values.size());
  if (size != kExpectedSevenCardIsoSize ||
      num_strengths_ != kExpectedSevenCardStrengths) {
    throw std::runtime_error("Unexpected 7-card lookup table dimensions");
  }

  for (hand_index_t index = 0; index < size; ++index) {
    const auto iter =
        std::lower_bound(unique_raw_values.begin(), unique_raw_values.end(),
                         raw_by_index[static_cast<std::size_t>(index)]);
    strength_by_index_[static_cast<std::size_t>(index)] =
        static_cast<uint16_t>(iter - unique_raw_values.begin());
  }
}

void SevenCardLookupTable::SaveTable() const {
  if (cache_path_.empty()) {
    return;
  }

  const std::filesystem::path path(cache_path_);
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  const std::filesystem::path tmp_path =
      path.parent_path() / (path.filename().string() + ".tmp");
  std::ofstream output(tmp_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return;
  }

  const CacheHeader header{
      kCacheMagic,
      kCacheVersion,
      static_cast<uint64_t>(strength_by_index_.size()),
      num_strengths_,
      static_cast<uint16_t>(sizeof(uint16_t)),
  };
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.write(reinterpret_cast<const char*>(strength_by_index_.data()),
               static_cast<std::streamsize>(strength_by_index_.size() *
                                            sizeof(uint16_t)));
  output.close();
  if (!output) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return;
  }

  std::error_code ignored;
  std::filesystem::rename(tmp_path, path, ignored);
  if (ignored) {
    std::filesystem::remove(path, ignored);
    std::filesystem::rename(tmp_path, path, ignored);
  }
}

void SevenCardLookupTable::ValidateIndex(hand_index_t index) const {
  if (index >= Size()) {
    throw std::invalid_argument("7-card lookup index is out of range");
  }
}

}  // namespace fisher::game::poker
