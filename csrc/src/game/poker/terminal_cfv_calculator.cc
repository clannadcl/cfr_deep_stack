#include "game/poker/terminal_cfv_calculator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#if defined(FISHER_USE_OPENBLAS)
#include <cblas.h>
#endif

namespace fisher::game::poker {
namespace {

using Clock = std::chrono::steady_clock;

double MillisecondsBetween(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

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

void BuildStatsForRangeInto(const std::vector<StrengthItem>& items,
                            std::size_t begin, std::size_t end,
                            const float* opponent_reach,
                            ReachStats* stats) {
  stats->Reset();
  for (std::size_t index = begin; index < end; ++index) {
    const RawHandEntry& hand = items[index].hand;
    const double iso_reach = opponent_reach[hand.iso_index];
    if (iso_reach == 0.0) {
      continue;
    }
    stats->Add(hand, iso_reach * hand.iso_normalization);
  }
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
  const std::size_t matrix_size =
      static_cast<std::size_t>(num_iso_hands * batch_size);
  delta_mass->resize(matrix_size);
  if (batch_size == 0) {
    return;
  }

  thread_local std::vector<float> reach_matrix;
  reach_matrix.resize(matrix_size);
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
  std::fill(delta_mass->begin(), delta_mass->end(), 0.0f);
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

void TerminalCfvCalculator::SetProfilingEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(profile_mutex_);
  profiling_enabled_ = enabled;
}

bool TerminalCfvCalculator::ProfilingEnabled() const {
  std::lock_guard<std::mutex> lock(profile_mutex_);
  return profiling_enabled_;
}

void TerminalCfvCalculator::ResetProfile() {
  std::lock_guard<std::mutex> lock(profile_mutex_);
  profile_ = Profile{};
}

TerminalCfvCalculator::Profile TerminalCfvCalculator::ProfileSnapshot()
    const {
  std::lock_guard<std::mutex> lock(profile_mutex_);
  return profile_;
}

void TerminalCfvCalculator::AddProfile(const Profile& profile) {
  std::lock_guard<std::mutex> lock(profile_mutex_);
  if (!profiling_enabled_) {
    return;
  }
  profile_.fold_calls += profile.fold_calls;
  profile_.runout_batch_calls += profile.runout_batch_calls;
  profile_.river_matrix_batch_calls += profile.river_matrix_batch_calls;
  profile_.river_scan_batch_calls += profile.river_scan_batch_calls;
  profile_.river_scan_items += profile.river_scan_items;
  profile_.runout_batch_items += profile.runout_batch_items;

  profile_.fold_ms += profile.fold_ms;
  profile_.runout_cache_ms += profile.runout_cache_ms;
  profile_.runout_matrix_ms += profile.runout_matrix_ms;
  profile_.runout_multiply_ms += profile.runout_multiply_ms;
  profile_.runout_valid_mass_ms += profile.runout_valid_mass_ms;
  profile_.runout_combine_ms += profile.runout_combine_ms;
  profile_.river_scan_cache_ms += profile.river_scan_cache_ms;
  profile_.river_scan_stats_ms += profile.river_scan_stats_ms;
  profile_.river_scan_group_stats_ms += profile.river_scan_group_stats_ms;
  profile_.river_scan_combine_ms += profile.river_scan_combine_ms;
  profile_.river_scan_accumulate_ms += profile.river_scan_accumulate_ms;
}

std::vector<float> TerminalCfvCalculator::Calculate(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) {
  std::vector<float> cfv(static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
  CalculateInto(node, player, mapping, opponent_reach.data(), cfv.data());
  return cfv;
}

void TerminalCfvCalculator::CalculateInto(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
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
  const bool profiling = ProfilingEnabled();
  Profile profile;
  profile.runout_batch_calls = 1;
  profile.runout_batch_items = static_cast<int64_t>(items.size());
  const IsomorphicMapping& mapping = *items.front().mapping;
  const int num_iso_hands = mapping.NumIsoHands();
  std::vector<const float*> reaches;
  reaches.reserve(items.size());
  for (const BatchItem& item : items) {
    reaches.push_back(item.opponent_reach);
  }

  Clock::time_point begin = Clock::now();
  const RunoutShowdownCache& cache = RunoutCacheFor(mapping);
  Clock::time_point end = Clock::now();
  if (profiling) {
    profile.runout_cache_ms += MillisecondsBetween(begin, end);
  }
  begin = Clock::now();
  const TerminalWinProbMatrix& matrix = MatrixFor(mapping);
  end = Clock::now();
  if (profiling) {
    profile.runout_matrix_ms += MillisecondsBetween(begin, end);
  }
  thread_local std::vector<float> delta_mass;
  begin = Clock::now();
  MultiplyEquityDeltaByReachBatchInto(matrix, reaches, &delta_mass);
  end = Clock::now();
  if (profiling) {
    profile.runout_multiply_ms += MillisecondsBetween(begin, end);
  }

  ReachStats opponent_stats;
  std::vector<double> valid_mass;
  for (std::size_t column = 0; column < items.size(); ++column) {
    const BatchItem& item = items[column];
    begin = Clock::now();
    BuildOpponentReachStatsInto(cache.raw_hands->hands, item.opponent_reach,
                                &opponent_stats);
    ComputeValidMassInto(cache.raw_hands->hands, num_iso_hands,
                         opponent_stats, &valid_mass);
    end = Clock::now();
    if (profiling) {
      profile.runout_valid_mass_ms += MillisecondsBetween(begin, end);
    }
    const PlayerTerminalPayoff& payoff =
        item.node->GetTerminalPayoff().players[item.player];
    begin = Clock::now();
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
    end = Clock::now();
    if (profiling) {
      profile.runout_combine_ms += MillisecondsBetween(begin, end);
    }
  }
  if (profiling) {
    AddProfile(profile);
  }
}

void TerminalCfvCalculator::CalculateRiverShowdownBatch(
    const std::vector<BatchItem>& items) {
  if (ProfilingEnabled() && !items.empty()) {
    Profile profile;
    profile.river_matrix_batch_calls = 1;
    AddProfile(profile);
  }
  CalculateRunoutShowdownBatch(items);
}

void TerminalCfvCalculator::CalculateRiverShowdownScanBatch(
    const std::vector<BatchItem>& items) {
  if (items.empty()) {
    return;
  }
  const bool profiling = ProfilingEnabled();
  Profile profile;
  profile.river_scan_batch_calls = 1;
  profile.river_scan_items = static_cast<int64_t>(items.size());
  const IsomorphicMapping& mapping = *items.front().mapping;
  const Clock::time_point cache_begin = Clock::now();
  const RiverShowdownCache& cache = RiverCacheFor(mapping);
  const Clock::time_point cache_end = Clock::now();
  if (profiling) {
    profile.river_scan_cache_ms =
        MillisecondsBetween(cache_begin, cache_end);
  }
  const int num_iso_hands = mapping.NumIsoHands();
  for (const BatchItem& item : items) {
    CalculateRiverShowdownWithCache(*item.node, item.player, num_iso_hands,
                                    cache, item.opponent_reach,
                                    item.out_cfv);
  }
  if (profiling) {
    AddProfile(profile);
  }
}

void TerminalCfvCalculator::CalculateFold(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
  const bool profiling = ProfilingEnabled();
  const Clock::time_point begin = Clock::now();
  const std::vector<RawHandEntry>& hands = RawHandsFor(mapping).hands;
  ReachStats opponent_stats;
  BuildOpponentReachStatsInto(hands, opponent_reach, &opponent_stats);
  thread_local std::vector<double> valid_mass;
  ComputeValidMassInto(hands, mapping.NumIsoHands(), opponent_stats,
                       &valid_mass);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  const double fold_payoff = node.IsFold()[static_cast<std::size_t>(player)]
                                 ? payoff.lose
                                 : payoff.win;
  for (int iso = 0; iso < mapping.NumIsoHands(); ++iso) {
    out_cfv[iso] = static_cast<float>(
        fold_payoff * valid_mass[static_cast<std::size_t>(iso)]);
  }
  if (profiling) {
    Profile profile;
    profile.fold_calls = 1;
    profile.fold_ms = MillisecondsBetween(begin, Clock::now());
    AddProfile(profile);
  }
}

void TerminalCfvCalculator::CalculateRiverShowdown(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
  const RiverShowdownCache& cache = RiverCacheFor(mapping);
  CalculateRiverShowdownWithCache(node, player, mapping.NumIsoHands(), cache,
                                  opponent_reach, out_cfv);
}

void TerminalCfvCalculator::CalculateRiverShowdownWithCache(
    const NodeState& node, int player, int num_iso_hands,
    const RiverShowdownCache& cache, const float* opponent_reach,
    float* out_cfv) {
  const bool profiling = ProfilingEnabled();
  Profile profile;
  ReachStats total_stats;
  Clock::time_point begin = Clock::now();
  BuildOpponentReachStatsInto(cache.raw_hands->hands, opponent_reach,
                              &total_stats);
  Clock::time_point end = Clock::now();
  if (profiling) {
    profile.river_scan_stats_ms += MillisecondsBetween(begin, end);
  }

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  begin = Clock::now();
  std::fill(out_cfv, out_cfv + num_iso_hands, 0.0f);
  end = Clock::now();
  if (profiling) {
    profile.river_scan_combine_ms += MillisecondsBetween(begin, end);
  }
  ReachStats weaker_stats;
  ReachStats tie_stats;

  for (const auto& group : cache.strength_groups) {
    const std::size_t group_begin = group.first;
    const std::size_t group_end = group.second;
    begin = Clock::now();
    BuildStatsForRangeInto(cache.sorted_items, group_begin, group_end,
                           opponent_reach, &tie_stats);
    end = Clock::now();
    if (profiling) {
      profile.river_scan_group_stats_ms += MillisecondsBetween(begin, end);
    }
    begin = Clock::now();
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
    end = Clock::now();
    if (profiling) {
      profile.river_scan_combine_ms += MillisecondsBetween(begin, end);
    }

    begin = Clock::now();
    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = cache.sorted_items[index].hand;
      const double iso_reach = opponent_reach[hand.iso_index];
      if (iso_reach != 0.0) {
        weaker_stats.Add(hand, iso_reach * hand.iso_normalization);
      }
    }
    end = Clock::now();
    if (profiling) {
      profile.river_scan_accumulate_ms += MillisecondsBetween(begin, end);
    }
  }
  if (profiling) {
    AddProfile(profile);
  }
}

void TerminalCfvCalculator::CalculateRunoutShowdown(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const float* opponent_reach, float* out_cfv) {
  const RunoutShowdownCache& cache = RunoutCacheFor(mapping);
  ReachStats opponent_stats;
  BuildOpponentReachStatsInto(cache.raw_hands->hands, opponent_reach,
                              &opponent_stats);
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
