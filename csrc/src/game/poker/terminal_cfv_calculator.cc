#include "game/poker/terminal_cfv_calculator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#if defined(FISHER_USE_OPENBLAS)
#include <cblas.h>
#endif

namespace fisher::game::poker {
namespace {

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

void ValidatePlayer(int player) {
  if (player < 0 || player >= GameBasic::kNumPlayers) {
    throw std::invalid_argument("Terminal CFV player must be 0 or 1");
  }
}

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

ReachStats BuildOpponentReachStats(const std::vector<RawHandEntry>& hands,
                                   const float* opponent_reach) {
  ReachStats stats;
  for (const RawHandEntry& hand : hands) {
    const double iso_reach = opponent_reach[hand.iso_index];
    if (iso_reach == 0.0) {
      continue;
    }
    stats.Add(hand, iso_reach * hand.iso_normalization);
  }
  return stats;
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
    throw std::invalid_argument("River showdown requires 5 board cards");
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

ReachStats BuildStatsForRange(const std::vector<StrengthItem>& items,
                              std::size_t begin, std::size_t end,
                              const float* opponent_reach) {
  ReachStats stats;
  for (std::size_t index = begin; index < end; ++index) {
    const RawHandEntry& hand = items[index].hand;
    const double iso_reach = opponent_reach[hand.iso_index];
    if (iso_reach == 0.0) {
      continue;
    }
    stats.Add(hand, iso_reach * hand.iso_normalization);
  }
  return stats;
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
    const TerminalWinProbMatrix& matrix,
    const float* opponent_reach, std::vector<float>* delta_mass) {
  const int num_iso_hands = matrix.NumIsoHands();
  delta_mass->assign(static_cast<std::size_t>(num_iso_hands), 0.0f);

#if defined(FISHER_USE_OPENBLAS)
  InitializeOpenBlasSingleThread();
  cblas_sgemv(CblasRowMajor, CblasNoTrans, num_iso_hands, num_iso_hands, 1.0f,
              matrix.EquityDeltaData().data(), num_iso_hands,
              opponent_reach, 1, 0.0f, delta_mass->data(), 1);
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

void MultiplyEquityDeltaByReachBatchInto(
    const TerminalWinProbMatrix& matrix, const std::vector<const float*>& reach,
    std::vector<float>* delta_mass) {
  const int num_iso_hands = matrix.NumIsoHands();
  const int batch_size = static_cast<int>(reach.size());
  delta_mass->assign(
      static_cast<std::size_t>(num_iso_hands * batch_size), 0.0f);
  if (batch_size == 0) {
    return;
  }

  std::vector<float> reach_matrix(
      static_cast<std::size_t>(num_iso_hands * batch_size), 0.0f);
  for (int column = 0; column < batch_size; ++column) {
    for (int hand = 0; hand < num_iso_hands; ++hand) {
      reach_matrix[static_cast<std::size_t>(hand * batch_size + column)] =
          reach[static_cast<std::size_t>(column)][hand];
    }
  }

#if defined(FISHER_USE_OPENBLAS)
  InitializeOpenBlasSingleThread();
  cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, num_iso_hands,
              batch_size, num_iso_hands, 1.0f,
              matrix.EquityDeltaData().data(), num_iso_hands,
              reach_matrix.data(), batch_size, 0.0f, delta_mass->data(),
              batch_size);
#else
  const std::vector<float>& equity_delta = matrix.EquityDeltaData();
  for (int hero_iso = 0; hero_iso < num_iso_hands; ++hero_iso) {
    const float* row =
        equity_delta.data() +
        static_cast<std::size_t>(hero_iso * num_iso_hands);
    for (int column = 0; column < batch_size; ++column) {
      double sum = 0.0;
      for (int opponent_iso = 0; opponent_iso < num_iso_hands;
           ++opponent_iso) {
        sum += static_cast<double>(reach[static_cast<std::size_t>(column)]
                                        [opponent_iso]) *
               static_cast<double>(row[opponent_iso]);
      }
      (*delta_mass)[static_cast<std::size_t>(hero_iso * batch_size + column)] =
          static_cast<float>(sum);
    }
  }
#endif
}

}  // namespace

struct TerminalCfvCalculator::RawHandsCache {
  std::vector<RawHandEntry> hands;
};

struct TerminalCfvCalculator::RunoutShowdownCache {
  const RawHandsCache* raw_hands = nullptr;
};

struct TerminalCfvCalculator::RiverShowdownCache {
  const RawHandsCache* raw_hands = nullptr;
  std::vector<StrengthItem> sorted_items;
  std::vector<std::pair<std::size_t, std::size_t>> strength_groups;
};

TerminalCfvCalculator::TerminalCfvCalculator(
    const GameBasic& game_basic, const SevenCardLookupTable& evaluator)
    : game_basic_(game_basic), evaluator_(evaluator) {}

TerminalCfvCalculator::~TerminalCfvCalculator() = default;

std::vector<float> TerminalCfvCalculator::Calculate(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) {
  std::vector<float> cfv(static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
  CalculateInto(node, player, mapping, opponent_reach.data(),
                static_cast<int>(opponent_reach.size()), cfv.data());
  return cfv;
}

void TerminalCfvCalculator::CalculateInto(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, int opponent_reach_size, float* out_cfv) {
  if (out_cfv == nullptr) {
    throw std::invalid_argument("Terminal CFV output cannot be null");
  }
  ValidateInput(node, player, mapping, opponent_reach, opponent_reach_size);

  if (node.Status() == TerminalStatus::kFoldTerminal) {
    CalculateFold(node, player, mapping, opponent_reach, out_cfv);
    return;
  }
  if (node.Street() == PokerRound::kRiver) {
    CalculateRiverShowdown(node, player, mapping, opponent_reach, out_cfv);
    return;
  }
  CalculateRunoutShowdown(node, player, mapping, opponent_reach, out_cfv);
}

void TerminalCfvCalculator::CalculateRunoutShowdownBatch(
    const std::vector<BatchItem>& items) {
  if (items.empty()) {
    return;
  }
  const IsomorphicMapping* mapping = items.front().mapping;
  if (mapping == nullptr) {
    throw std::invalid_argument("Terminal CFV batch mapping cannot be null");
  }
  const int num_iso_hands = mapping->NumIsoHands();
  std::vector<const float*> reaches;
  reaches.reserve(items.size());
  for (const BatchItem& item : items) {
    if (item.node == nullptr) {
      throw std::invalid_argument("Terminal CFV batch node cannot be null");
    }
    if (item.mapping == nullptr) {
      throw std::invalid_argument("Terminal CFV batch mapping cannot be null");
    }
    if (item.out_cfv == nullptr) {
      throw std::invalid_argument("Terminal CFV batch output cannot be null");
    }
    if (item.mapping->RawBoard().ToString() !=
            mapping->RawBoard().ToString() ||
        item.mapping->NumIsoHands() != num_iso_hands) {
      throw std::invalid_argument(
          "Terminal CFV batch items must share one mapping");
    }
    ValidateInput(*item.node, item.player, *item.mapping, item.opponent_reach,
                  item.opponent_reach_size);
    if (item.node->Status() != TerminalStatus::kShowdownTerminal ||
        item.node->Street() == PokerRound::kRiver) {
      throw std::invalid_argument(
          "Terminal CFV batch only supports flop/turn showdown nodes");
    }
    reaches.push_back(item.opponent_reach);
  }

  const RunoutShowdownCache& cache = RunoutCacheFor(*mapping);
  const TerminalWinProbMatrix& matrix = MatrixFor(*mapping);
  std::vector<float> delta_mass;
  MultiplyEquityDeltaByReachBatchInto(matrix, reaches, &delta_mass);

  std::vector<double> valid_mass;
  for (std::size_t column = 0; column < items.size(); ++column) {
    const BatchItem& item = items[column];
    const ReachStats opponent_stats =
        BuildOpponentReachStats(cache.raw_hands->hands, item.opponent_reach);
    ComputeValidMassInto(cache.raw_hands->hands, num_iso_hands,
                         opponent_stats, &valid_mass);
    const PlayerTerminalPayoff& payoff =
        item.node->GetTerminalPayoff().players[item.player];
    for (int hero_iso = 0; hero_iso < num_iso_hands; ++hero_iso) {
      item.out_cfv[hero_iso] = static_cast<float>(
          static_cast<double>(payoff.chop) *
              valid_mass[static_cast<std::size_t>(hero_iso)] +
          static_cast<double>(payoff.win - payoff.chop) *
              static_cast<double>(
                  delta_mass[static_cast<std::size_t>(
                      hero_iso * static_cast<int>(items.size()) +
                      static_cast<int>(column))]));
    }
  }
}

void TerminalCfvCalculator::ValidateInput(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, int opponent_reach_size) const {
  if (!node.IsTerminal()) {
    throw std::invalid_argument("Terminal CFV requires a terminal node");
  }
  ValidatePlayer(player);
  if (node.Board().ToString() != mapping.RawBoard().ToString()) {
    throw std::invalid_argument(
        "Terminal CFV mapping board must match node board");
  }
  if (opponent_reach == nullptr) {
    throw std::invalid_argument("Terminal CFV opponent reach cannot be null");
  }
  if (opponent_reach_size != mapping.NumIsoHands()) {
    throw std::invalid_argument("Terminal CFV opponent reach size mismatch");
  }
  for (int index = 0; index < opponent_reach_size; ++index) {
    if (opponent_reach[index] < 0.0f) {
      throw std::invalid_argument("Terminal CFV reach cannot be negative");
    }
  }
  if (node.Status() == TerminalStatus::kShowdownTerminal &&
      node.Street() == PokerRound::kPreflop) {
    throw std::invalid_argument("Terminal CFV only supports postflop nodes");
  }
}

void TerminalCfvCalculator::CalculateFold(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
  const std::vector<RawHandEntry>& hands = RawHandsFor(mapping).hands;
  const ReachStats opponent_stats =
      BuildOpponentReachStats(hands, opponent_reach);
  thread_local std::vector<double> valid_mass;
  ComputeValidMassInto(hands, mapping.NumIsoHands(), opponent_stats,
                       &valid_mass);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  const double fold_payoff = node.IsFold()[static_cast<std::size_t>(player)]
                                 ? payoff.lose
                                 : payoff.win;
  for (int iso = 0; iso < mapping.NumIsoHands(); ++iso) {
    out_cfv[iso] =
        static_cast<float>(fold_payoff * valid_mass[static_cast<std::size_t>(iso)]);
  }
}

void TerminalCfvCalculator::CalculateRiverShowdown(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
  const RiverShowdownCache& cache = RiverCacheFor(mapping);
  const ReachStats total_stats =
      BuildOpponentReachStats(cache.raw_hands->hands, opponent_reach);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  std::fill(out_cfv, out_cfv + mapping.NumIsoHands(), 0.0f);
  ReachStats weaker_stats;

  for (const auto& group : cache.strength_groups) {
    const std::size_t group_begin = group.first;
    const std::size_t group_end = group.second;
    const ReachStats tie_stats =
        BuildStatsForRange(cache.sorted_items, group_begin, group_end,
                           opponent_reach);
    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = cache.sorted_items[index].hand;
      const double valid_mass = total_stats.Excluding(hand);
      const double win_mass = weaker_stats.Excluding(hand);
      const double tie_mass = tie_stats.Excluding(hand);
      const double lose_mass = valid_mass - win_mass - tie_mass;
      const double raw_cfv = static_cast<double>(payoff.win) * win_mass +
                             static_cast<double>(payoff.lose) * lose_mass +
                             static_cast<double>(payoff.chop) * tie_mass;
      out_cfv[hand.iso_index] +=
          static_cast<float>(raw_cfv * hand.iso_normalization);
    }

    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = cache.sorted_items[index].hand;
      const double iso_reach = opponent_reach[hand.iso_index];
      if (iso_reach != 0.0) {
        weaker_stats.Add(hand, iso_reach * hand.iso_normalization);
      }
    }
  }
}

void TerminalCfvCalculator::CalculateRunoutShowdown(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
  const RunoutShowdownCache& cache = RunoutCacheFor(mapping);
  const ReachStats opponent_stats =
      BuildOpponentReachStats(cache.raw_hands->hands, opponent_reach);
  thread_local std::vector<double> valid_mass;
  ComputeValidMassInto(cache.raw_hands->hands, mapping.NumIsoHands(),
                       opponent_stats, &valid_mass);
  const TerminalWinProbMatrix& matrix = MatrixFor(mapping);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  thread_local std::vector<float> delta_mass;
  MultiplyEquityDeltaByReachInto(matrix, opponent_reach, &delta_mass);
  const int num_iso_hands = mapping.NumIsoHands();
  for (int hero_iso = 0; hero_iso < num_iso_hands; ++hero_iso) {
    out_cfv[hero_iso] = static_cast<float>(
        static_cast<double>(payoff.chop) *
            valid_mass[static_cast<std::size_t>(hero_iso)] +
        static_cast<double>(payoff.win - payoff.chop) *
            static_cast<double>(delta_mass[static_cast<std::size_t>(hero_iso)]));
  }
}

const TerminalCfvCalculator::RawHandsCache& TerminalCfvCalculator::RawHandsFor(
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

const TerminalWinProbMatrix& TerminalCfvCalculator::MatrixFor(
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

const TerminalCfvCalculator::RunoutShowdownCache&
TerminalCfvCalculator::RunoutCacheFor(const IsomorphicMapping& mapping) {
  const RawHandsCache& raw_hands = RawHandsFor(mapping);
  const std::string key = MatrixKey(mapping);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  auto iterator = runout_cache_.find(key);
  if (iterator != runout_cache_.end()) {
    return *iterator->second;
  }

  auto cache = std::make_unique<RunoutShowdownCache>();
  cache->raw_hands = &raw_hands;

  const RunoutShowdownCache* pointer = cache.get();
  runout_cache_.emplace(key, std::move(cache));
  return *pointer;
}

const TerminalCfvCalculator::RiverShowdownCache&
TerminalCfvCalculator::RiverCacheFor(const IsomorphicMapping& mapping) {
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
