#include "game/poker/hand_equity_calculator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#if defined(FISHER_USE_OPENBLAS)
#include <cblas.h>
#endif

namespace fisher::game::poker {
namespace {

constexpr double kMassEpsilon = 1e-10;

struct RawHandEntry {
  int raw_index = -1;
  int iso_index = -1;
  uint8_t high_card = 0;
  uint8_t low_card = 0;
  double iso_normalization = 0.0;
};

struct StrengthItem {
  uint16_t strength = 0;
  RawHandEntry hand;
};

struct ReachStats {
  double total = 0.0;
  std::array<double, GameBasic::kDeckSize> by_card{};
  std::array<std::array<double, GameBasic::kDeckSize>, GameBasic::kDeckSize>
      by_card_pair{};

  void Reset() {
    total = 0.0;
    by_card.fill(0.0);
    for (auto& row : by_card_pair) {
      row.fill(0.0);
    }
  }

  void Add(const RawHandEntry& hand, double reach) {
    total += reach;
    by_card[hand.high_card] += reach;
    by_card[hand.low_card] += reach;
    by_card_pair[hand.high_card][hand.low_card] += reach;
    by_card_pair[hand.low_card][hand.high_card] += reach;
  }

  double Excluding(const RawHandEntry& hand) const {
    return total - by_card[hand.high_card] - by_card[hand.low_card] +
           by_card_pair[hand.high_card][hand.low_card];
  }
};

struct RiverMassStats {
  double total = 0.0;
  std::array<double, GameBasic::kDeckSize> by_card{};

  void Reset() {
    total = 0.0;
    by_card.fill(0.0);
  }

  void Add(const RawHandEntry& hand, double mass) {
    total += mass;
    by_card[hand.high_card] += mass;
    by_card[hand.low_card] += mass;
  }

  double Excluding(const RawHandEntry& hand, double self_mass) const {
    return total - by_card[hand.high_card] - by_card[hand.low_card] +
           self_mass;
  }
};

std::string MatrixKey(const IsomorphicMapping& mapping) {
  return mapping.RawBoard().ToString() + "#" +
         std::to_string(mapping.NumIsoHands());
}

std::vector<RawHandEntry> BuildRawHandEntries(
    const GameBasic& game_basic, const IsomorphicMapping& mapping) {
  std::vector<RawHandEntry> entries;
  entries.reserve(GameBasic::kNumHands);
  for (int raw_index = 0; raw_index < GameBasic::kNumHands; ++raw_index) {
    const int iso_index = mapping.RawToIso(raw_index);
    if (iso_index == IsomorphicMapping::kInvalidIsoIndex) {
      continue;
    }
    const PokerHand& hand = game_basic.HandFromIndex(raw_index);
    entries.push_back(RawHandEntry{
        raw_index,
        iso_index,
        hand.HighCard().Value(),
        hand.LowCard().Value(),
        1.0 / static_cast<double>(mapping.RawHandCount(iso_index)),
    });
  }
  return entries;
}

void BuildOpponentReachStatsInto(const std::vector<RawHandEntry>& hands,
                                 const float* opponent_reach,
                                 ReachStats* stats) {
  stats->Reset();
  for (const RawHandEntry& hand : hands) {
    const double iso_reach = opponent_reach[hand.iso_index];
    if (iso_reach == 0.0) {
      continue;
    }
    stats->Add(hand, iso_reach * hand.iso_normalization);
  }
}

void ComputeValidMassInto(const std::vector<RawHandEntry>& hands,
                          int num_iso_hands,
                          const ReachStats& opponent_stats,
                          std::vector<double>* valid_mass) {
  valid_mass->assign(static_cast<std::size_t>(num_iso_hands), 0.0);
  for (const RawHandEntry& hand : hands) {
    (*valid_mass)[static_cast<std::size_t>(hand.iso_index)] +=
        opponent_stats.Excluding(hand) * hand.iso_normalization;
  }
}

std::array<uint8_t, PokerHandEvaluator::kSevenCards> SevenCards(
    const PokerCards& board, const RawHandEntry& hand) {
  if (board.Size() != 5) {
    throw std::invalid_argument("River equity requires 5 board cards");
  }

  std::array<uint8_t, PokerHandEvaluator::kSevenCards> cards{};
  for (std::size_t index = 0; index < board.Size(); ++index) {
    cards[index] = board.Cards()[index].Value();
  }
  cards[5] = hand.high_card;
  cards[6] = hand.low_card;
  return cards;
}

std::vector<StrengthItem> BuildSortedStrengthItems(
    const PokerCards& board, const std::vector<RawHandEntry>& hands,
    const SevenCardLookupTable& evaluator) {
  std::vector<StrengthItem> items;
  items.reserve(hands.size());
  for (const RawHandEntry& hand : hands) {
    const std::array<uint8_t, PokerHandEvaluator::kSevenCards> cards =
        SevenCards(board, hand);
    items.push_back(StrengthItem{evaluator.Evaluate7(cards.data()), hand});
  }
  std::sort(items.begin(), items.end(),
            [](const StrengthItem& left, const StrengthItem& right) {
              if (left.strength != right.strength) {
                return left.strength < right.strength;
              }
              return left.hand.raw_index < right.hand.raw_index;
            });
  return items;
}

std::vector<std::pair<std::size_t, std::size_t>> BuildStrengthGroups(
    const std::vector<StrengthItem>& items) {
  std::vector<std::pair<std::size_t, std::size_t>> groups;
  std::size_t group_begin = 0;
  while (group_begin < items.size()) {
    std::size_t group_end = group_begin + 1;
    while (group_end < items.size() &&
           items[group_end].strength == items[group_begin].strength) {
      ++group_end;
    }
    groups.emplace_back(group_begin, group_end);
    group_begin = group_end;
  }
  return groups;
}

#if defined(FISHER_USE_OPENBLAS)
void InitializeOpenBlasSingleThread() {
  static const bool initialized = [] {
    openblas_set_num_threads(1);
    return true;
  }();
  (void)initialized;
}
#endif

void MultiplyEquityDeltaByReachInto(
    const TerminalWinProbMatrix& matrix, const float* opponent_reach,
    std::vector<float>* delta_mass) {
  const int num_iso_hands = matrix.NumIsoHands();
  delta_mass->assign(static_cast<std::size_t>(num_iso_hands), 0.0f);

#if defined(FISHER_USE_OPENBLAS)
  InitializeOpenBlasSingleThread();
  cblas_sgemv(CblasRowMajor, CblasNoTrans, num_iso_hands, num_iso_hands, 1.0f,
              matrix.EquityDeltaData().data(), num_iso_hands, opponent_reach,
              1, 0.0f, delta_mass->data(), 1);
#else
  const std::vector<float>& equity_delta = matrix.EquityDeltaData();
  for (int hero_iso = 0; hero_iso < num_iso_hands; ++hero_iso) {
    const float* row =
        equity_delta.data() +
        static_cast<std::size_t>(hero_iso * num_iso_hands);
    double sum = 0.0;
    for (int opponent_iso = 0; opponent_iso < num_iso_hands; ++opponent_iso) {
      sum += static_cast<double>(opponent_reach[opponent_iso]) *
             static_cast<double>(row[opponent_iso]);
    }
    (*delta_mass)[static_cast<std::size_t>(hero_iso)] =
        static_cast<float>(sum);
  }
#endif
}

float RangeEquity(const std::vector<float>& equity_mass,
                  const std::vector<double>& valid_mass,
                  const float* player_reach) {
  double numerator = 0.0;
  double denominator = 0.0;
  for (std::size_t hand = 0; hand < equity_mass.size(); ++hand) {
    const double reach = player_reach[hand];
    numerator += reach * static_cast<double>(equity_mass[hand]);
    denominator += reach * valid_mass[hand];
  }
  return denominator > kMassEpsilon ? static_cast<float>(numerator / denominator)
                                    : 0.0f;
}

}  // namespace

struct HandEquityCalculator::RawHandsCache {
  std::vector<RawHandEntry> hands;
};

struct HandEquityCalculator::RiverShowdownCache {
  const RawHandsCache* raw_hands = nullptr;
  std::vector<StrengthItem> sorted_items;
  std::vector<std::pair<std::size_t, std::size_t>> strength_groups;
};

HandEquityCalculator::HandEquityCalculator(
    const GameBasic& game_basic, const SevenCardLookupTable& evaluator)
    : game_basic_(game_basic), evaluator_(evaluator) {}

HandEquityCalculator::~HandEquityCalculator() = default;

HandEquityResult HandEquityCalculator::Calculate(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* player_reach, const float* opponent_reach) {
  if (player < 0 || player >= GameBasic::kNumPlayers) {
    throw std::invalid_argument("Equity player must be 0 or 1");
  }
  if (node.Board().ToString() != mapping.RawBoard().ToString()) {
    throw std::invalid_argument("Equity node board must match mapping board");
  }
  if (node.Street() == PokerRound::kRiver) {
    return CalculateRiver(node, mapping, player_reach, opponent_reach);
  }
  if (node.Street() == PokerRound::kFlop || node.Street() == PokerRound::kTurn) {
    return CalculateRunout(node, mapping, player_reach, opponent_reach);
  }
  throw std::invalid_argument("Equity calculation requires a postflop board");
}

HandEquityResult HandEquityCalculator::CalculateRiver(
    const NodeState& node, const IsomorphicMapping& mapping,
    const float* player_reach, const float* opponent_reach) {
  const RiverShowdownCache& cache = RiverCacheFor(mapping);
  const int num_iso_hands = mapping.NumIsoHands();

  ReachStats opponent_stats;
  BuildOpponentReachStatsInto(cache.raw_hands->hands, opponent_reach,
                              &opponent_stats);
  std::vector<double> valid_mass;
  ComputeValidMassInto(cache.raw_hands->hands, num_iso_hands, opponent_stats,
                       &valid_mass);

  std::vector<float> equity_mass(static_cast<std::size_t>(num_iso_hands),
                                 0.0f);
  RiverMassStats mass;
  mass.Reset();
  for (const auto& group : cache.strength_groups) {
    const std::size_t group_begin = group.first;
    const std::size_t group_end = group.second;

    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = cache.sorted_items[index].hand;
      const double reach =
          static_cast<double>(opponent_reach[hand.iso_index]) *
          hand.iso_normalization;
      if (reach != 0.0) {
        mass.Add(hand, reach * 0.5);
      }
    }

    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = cache.sorted_items[index].hand;
      const double self_mass =
          static_cast<double>(opponent_reach[hand.iso_index]) *
          hand.iso_normalization * 0.5;
      equity_mass[static_cast<std::size_t>(hand.iso_index)] +=
          static_cast<float>(mass.Excluding(hand, self_mass) *
                             hand.iso_normalization);
    }

    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = cache.sorted_items[index].hand;
      const double reach =
          static_cast<double>(opponent_reach[hand.iso_index]) *
          hand.iso_normalization;
      if (reach != 0.0) {
        mass.Add(hand, reach * 0.5);
      }
    }
  }

  HandEquityResult result;
  result.equity.assign(static_cast<std::size_t>(num_iso_hands), 0.0f);
  for (int hand = 0; hand < num_iso_hands; ++hand) {
    const std::size_t index = static_cast<std::size_t>(hand);
    if (valid_mass[index] > kMassEpsilon) {
      result.equity[index] =
          static_cast<float>(static_cast<double>(equity_mass[index]) /
                             valid_mass[index]);
    }
  }
  result.range_equity = RangeEquity(equity_mass, valid_mass, player_reach);
  (void)node;
  return result;
}

HandEquityResult HandEquityCalculator::CalculateRunout(
    const NodeState& node, const IsomorphicMapping& mapping,
    const float* player_reach, const float* opponent_reach) {
  const RawHandsCache& raw_hands = RawHandsFor(mapping);
  ReachStats opponent_stats;
  BuildOpponentReachStatsInto(raw_hands.hands, opponent_reach, &opponent_stats);

  const int num_iso_hands = mapping.NumIsoHands();
  std::vector<double> valid_mass;
  ComputeValidMassInto(raw_hands.hands, num_iso_hands, opponent_stats,
                       &valid_mass);

  const TerminalWinProbMatrix& matrix = MatrixFor(mapping);
  std::vector<float> delta_mass;
  MultiplyEquityDeltaByReachInto(matrix, opponent_reach, &delta_mass);

  std::vector<float> equity_mass(static_cast<std::size_t>(num_iso_hands),
                                 0.0f);
  HandEquityResult result;
  result.equity.assign(static_cast<std::size_t>(num_iso_hands), 0.0f);
  for (int hand = 0; hand < num_iso_hands; ++hand) {
    const std::size_t index = static_cast<std::size_t>(hand);
    equity_mass[index] =
        static_cast<float>(0.5 * valid_mass[index] +
                           0.5 * static_cast<double>(delta_mass[index]));
    if (valid_mass[index] > kMassEpsilon) {
      result.equity[index] =
          static_cast<float>(static_cast<double>(equity_mass[index]) /
                             valid_mass[index]);
    }
  }
  result.range_equity = RangeEquity(equity_mass, valid_mass, player_reach);
  (void)node;
  return result;
}

const HandEquityCalculator::RawHandsCache& HandEquityCalculator::RawHandsFor(
    const IsomorphicMapping& mapping) {
  const std::string key = MatrixKey(mapping);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  auto iterator = raw_hands_cache_.find(key);
  if (iterator != raw_hands_cache_.end()) {
    return *iterator->second;
  }

  auto cache = std::make_unique<RawHandsCache>();
  cache->hands = BuildRawHandEntries(game_basic_, mapping);
  const RawHandsCache* pointer = cache.get();
  raw_hands_cache_.emplace(key, std::move(cache));
  return *pointer;
}

const TerminalWinProbMatrix& HandEquityCalculator::MatrixFor(
    const IsomorphicMapping& mapping) {
  const std::string key = MatrixKey(mapping);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  auto iterator = matrix_cache_.find(key);
  if (iterator != matrix_cache_.end()) {
    return *iterator->second;
  }

  auto matrix = std::make_unique<TerminalWinProbMatrix>(
      game_basic_, mapping.RawBoard(), mapping, evaluator_);
  const TerminalWinProbMatrix* pointer = matrix.get();
  matrix_cache_.emplace(key, std::move(matrix));
  return *pointer;
}

const HandEquityCalculator::RiverShowdownCache&
HandEquityCalculator::RiverCacheFor(const IsomorphicMapping& mapping) {
  const RawHandsCache& raw_hands = RawHandsFor(mapping);
  const std::string key = MatrixKey(mapping);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  auto iterator = river_cache_.find(key);
  if (iterator != river_cache_.end()) {
    return *iterator->second;
  }

  auto cache = std::make_unique<RiverShowdownCache>();
  cache->raw_hands = &raw_hands;
  cache->sorted_items =
      BuildSortedStrengthItems(mapping.RawBoard(), raw_hands.hands,
                               evaluator_);
  cache->strength_groups = BuildStrengthGroups(cache->sorted_items);

  const RiverShowdownCache* pointer = cache.get();
  river_cache_.emplace(key, std::move(cache));
  return *pointer;
}

}  // namespace fisher::game::poker
