#include "game/poker/terminal_cfv_calculator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

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
                                   const std::vector<float>& opponent_reach) {
  ReachStats stats;
  for (const RawHandEntry& hand : hands) {
    const double iso_reach =
        opponent_reach[static_cast<std::size_t>(hand.iso_index)];
    if (iso_reach == 0.0) {
      continue;
    }
    stats.Add(hand, iso_reach * hand.iso_normalization);
  }
  return stats;
}

std::vector<double> ComputeValidMass(const std::vector<RawHandEntry>& hands,
                                     int num_iso_hands,
                                     const ReachStats& opponent_stats) {
  std::vector<double> valid_mass(static_cast<std::size_t>(num_iso_hands), 0.0);
  for (const RawHandEntry& hand : hands) {
    valid_mass[static_cast<std::size_t>(hand.iso_index)] +=
        opponent_stats.Excluding(hand) * hand.iso_normalization;
  }
  return valid_mass;
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

ReachStats BuildStatsForRange(const std::vector<StrengthItem>& items,
                              std::size_t begin, std::size_t end,
                              const std::vector<float>& opponent_reach) {
  ReachStats stats;
  for (std::size_t index = begin; index < end; ++index) {
    const RawHandEntry& hand = items[index].hand;
    const double iso_reach =
        opponent_reach[static_cast<std::size_t>(hand.iso_index)];
    if (iso_reach == 0.0) {
      continue;
    }
    stats.Add(hand, iso_reach * hand.iso_normalization);
  }
  return stats;
}

}  // namespace

TerminalCfvCalculator::TerminalCfvCalculator(
    const GameBasic& game_basic, const SevenCardLookupTable& evaluator)
    : game_basic_(game_basic), evaluator_(evaluator) {}

std::vector<float> TerminalCfvCalculator::Calculate(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) {
  ValidateInput(node, player, mapping, opponent_reach);

  if (node.Status() == TerminalStatus::kFoldTerminal) {
    return CalculateFold(node, player, mapping, opponent_reach);
  }
  if (node.Street() == PokerRound::kRiver) {
    return CalculateRiverShowdown(node, player, mapping, opponent_reach);
  }
  return CalculateRunoutShowdown(node, player, mapping, opponent_reach);
}

void TerminalCfvCalculator::ValidateInput(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) const {
  if (!node.IsTerminal()) {
    throw std::invalid_argument("Terminal CFV requires a terminal node");
  }
  ValidatePlayer(player);
  if (node.Board().ToString() != mapping.RawBoard().ToString()) {
    throw std::invalid_argument(
        "Terminal CFV mapping board must match node board");
  }
  if (opponent_reach.size() !=
      static_cast<std::size_t>(mapping.NumIsoHands())) {
    throw std::invalid_argument("Terminal CFV opponent reach size mismatch");
  }
  for (float reach : opponent_reach) {
    if (reach < 0.0f) {
      throw std::invalid_argument("Terminal CFV reach cannot be negative");
    }
  }
  if (node.Status() == TerminalStatus::kShowdownTerminal &&
      node.Street() == PokerRound::kPreflop) {
    throw std::invalid_argument("Terminal CFV only supports postflop nodes");
  }
}

std::vector<float> TerminalCfvCalculator::CalculateFold(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) const {
  const std::vector<RawHandEntry> hands =
      BuildRawHandEntries(game_basic_, mapping);
  const ReachStats opponent_stats = BuildOpponentReachStats(hands,
                                                            opponent_reach);
  const std::vector<double> valid_mass =
      ComputeValidMass(hands, mapping.NumIsoHands(), opponent_stats);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  const double fold_payoff = node.IsFold()[static_cast<std::size_t>(player)]
                                 ? payoff.lose
                                 : payoff.win;
  std::vector<float> cfv(static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
  for (int iso = 0; iso < mapping.NumIsoHands(); ++iso) {
    cfv[static_cast<std::size_t>(iso)] =
        static_cast<float>(fold_payoff * valid_mass[static_cast<std::size_t>(iso)]);
  }
  return cfv;
}

std::vector<float> TerminalCfvCalculator::CalculateRiverShowdown(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) const {
  const std::vector<RawHandEntry> hands =
      BuildRawHandEntries(game_basic_, mapping);
  const ReachStats total_stats = BuildOpponentReachStats(hands, opponent_reach);
  const std::vector<StrengthItem> items =
      BuildSortedStrengthItems(node.Board(), hands, evaluator_);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  std::vector<float> cfv(static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
  ReachStats weaker_stats;

  std::size_t group_begin = 0;
  while (group_begin < items.size()) {
    std::size_t group_end = group_begin + 1;
    while (group_end < items.size() &&
           items[group_end].strength == items[group_begin].strength) {
      ++group_end;
    }

    const ReachStats tie_stats =
        BuildStatsForRange(items, group_begin, group_end, opponent_reach);
    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = items[index].hand;
      const double valid_mass = total_stats.Excluding(hand);
      const double win_mass = weaker_stats.Excluding(hand);
      const double tie_mass = tie_stats.Excluding(hand);
      const double lose_mass = valid_mass - win_mass - tie_mass;
      const double raw_cfv = static_cast<double>(payoff.win) * win_mass +
                             static_cast<double>(payoff.lose) * lose_mass +
                             static_cast<double>(payoff.chop) * tie_mass;
      cfv[static_cast<std::size_t>(hand.iso_index)] +=
          static_cast<float>(raw_cfv * hand.iso_normalization);
    }

    for (std::size_t index = group_begin; index < group_end; ++index) {
      const RawHandEntry& hand = items[index].hand;
      const double iso_reach =
          opponent_reach[static_cast<std::size_t>(hand.iso_index)];
      if (iso_reach != 0.0) {
        weaker_stats.Add(hand, iso_reach * hand.iso_normalization);
      }
    }
    group_begin = group_end;
  }

  return cfv;
}

std::vector<float> TerminalCfvCalculator::CalculateRunoutShowdown(
    const NodeState& node, int player, const IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) {
  const std::vector<RawHandEntry> hands =
      BuildRawHandEntries(game_basic_, mapping);
  const ReachStats opponent_stats = BuildOpponentReachStats(hands,
                                                            opponent_reach);
  const std::vector<double> valid_mass =
      ComputeValidMass(hands, mapping.NumIsoHands(), opponent_stats);
  const TerminalWinProbMatrix& matrix = MatrixFor(mapping);

  const PlayerTerminalPayoff& payoff = node.GetTerminalPayoff().players[player];
  std::vector<float> cfv(static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
  for (int hero_iso = 0; hero_iso < mapping.NumIsoHands(); ++hero_iso) {
    double delta_mass = 0.0;
    for (int opponent_iso = 0; opponent_iso < mapping.NumIsoHands();
         ++opponent_iso) {
      delta_mass +=
          static_cast<double>(
              opponent_reach[static_cast<std::size_t>(opponent_iso)]) *
          static_cast<double>(matrix.EquityDelta(hero_iso, opponent_iso));
    }
    cfv[static_cast<std::size_t>(hero_iso)] = static_cast<float>(
        static_cast<double>(payoff.chop) *
            valid_mass[static_cast<std::size_t>(hero_iso)] +
        static_cast<double>(payoff.win - payoff.chop) * delta_mass);
  }
  return cfv;
}

const TerminalWinProbMatrix& TerminalCfvCalculator::MatrixFor(
    const IsomorphicMapping& mapping) {
  const std::string key = MatrixKey(mapping);
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

}  // namespace fisher::game::poker
