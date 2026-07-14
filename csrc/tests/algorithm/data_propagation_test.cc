#include <cmath>
#include <stdexcept>
#include <vector>

#include "algorithm/data_propagation.h"
#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_cards.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, const char* message) {
  if (std::fabs(actual - expected) > 1e-4f) {
    throw std::runtime_error(message);
  }
}

template <typename Fn>
void ExpectInvalidArgument(Fn fn, const char* message) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::vector<std::vector<float>> FullBelief() {
  return std::vector<std::vector<float>>(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, 1.0f));
}

bool IsPairWithRank(const fisher::game::poker::PokerHand& hand,
                    fisher::game::poker::PokerRank rank) {
  const auto cards = hand.Cards();
  return cards[0].Rank() == rank && cards[1].Rank() == rank;
}

std::vector<std::vector<float>> PocketAcesAndKingsBelief(
    const fisher::game::poker::GameBasic& game) {
  std::vector<std::vector<float>> belief(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, 0.0f));

  for (int hand_index = 0;
       hand_index < fisher::game::poker::GameBasic::kNumHands; ++hand_index) {
    const auto& hand = game.HandFromIndex(hand_index);
    if (IsPairWithRank(hand, fisher::game::poker::PokerRank::kAce) ||
        IsPairWithRank(hand, fisher::game::poker::PokerRank::kKing)) {
      belief[0][static_cast<std::size_t>(hand_index)] = 1.0f;
      belief[1][static_cast<std::size_t>(hand_index)] = 1.0f;
    }
  }

  return belief;
}

float Sum(const std::vector<float>& values) {
  float sum = 0.0f;
  for (float value : values) {
    sum += value;
  }
  return sum;
}

}  // namespace

int main() {
  using fisher::algorithm::PropagateCfvBackward;
  using fisher::algorithm::PropagateReachForward;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsomorphicMapping;
  using fisher::game::poker::IsomorphicMappingTable;
  using fisher::game::poker::PokerBelief;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerRank;

  GameBasic game;
  PokerBelief belief(FullBelief());
  IsomorphicMappingTable table(game, belief);

  const auto& flop = table.Get(PokerCards("AsKdQh"));
  const auto& turn = table.Get(PokerCards("AsKdQh2c"));

  std::vector<float> parent_reach(
      static_cast<std::size_t>(flop.NumIsoHands()), 0.0f);
  for (int iso = 0; iso < flop.NumIsoHands(); ++iso) {
    parent_reach[static_cast<std::size_t>(iso)] =
        static_cast<float>(flop.RawHandCount(iso));
  }

  std::vector<float> child_reach;
  PropagateReachForward(flop, turn, parent_reach, &child_reach);
  Expect(child_reach.size() == static_cast<std::size_t>(turn.NumIsoHands()),
         "child reach size mismatch");

  int child_valid_raw_count = 0;
  for (int raw = 0; raw < GameBasic::kNumHands; ++raw) {
    if (flop.RawToIso(raw) >= 0 && turn.RawToIso(raw) >= 0) {
      ++child_valid_raw_count;
    }
  }
  ExpectNear(Sum(child_reach), static_cast<float>(child_valid_raw_count),
             "child reach mass mismatch");

  for (int iso = 0; iso < turn.NumIsoHands(); ++iso) {
    ExpectNear(child_reach[static_cast<std::size_t>(iso)],
               static_cast<float>(turn.RawHandCount(iso)),
               "child reach bucket count mismatch");
  }

  std::vector<float> child_cfv(
      static_cast<std::size_t>(turn.NumIsoHands()), 0.0f);
  for (int iso = 0; iso < turn.NumIsoHands(); ++iso) {
    child_cfv[static_cast<std::size_t>(iso)] =
        static_cast<float>(turn.RawHandCount(iso));
  }

  std::vector<float> parent_cfv;
  PropagateCfvBackward(flop, turn, child_cfv, &parent_cfv);
  Expect(parent_cfv.size() == static_cast<std::size_t>(flop.NumIsoHands()),
         "parent cfv size mismatch");
  ExpectNear(Sum(parent_cfv), static_cast<float>(child_valid_raw_count),
             "parent cfv mass mismatch");

  PokerBelief sparse_belief(PocketAcesAndKingsBelief(game));
  IsomorphicMappingTable sparse_table(game, sparse_belief);
  const auto& sparse_turn = sparse_table.Get(PokerCards("AcAd2h3s"));
  const auto& sparse_river = sparse_table.Get(PokerCards("AcAd2h3sAh"));

  int turn_aa_count = 0;
  int river_aa_count = 0;
  int turn_kk_count = 0;
  int river_kk_count = 0;
  std::vector<float> sparse_turn_reach(
      static_cast<std::size_t>(sparse_turn.NumIsoHands()), 0.0f);

  for (int raw = 0; raw < GameBasic::kNumHands; ++raw) {
    const auto& hand = game.HandFromIndex(raw);
    const bool is_aa = IsPairWithRank(hand, PokerRank::kAce);
    const bool is_kk = IsPairWithRank(hand, PokerRank::kKing);
    if (!is_aa && !is_kk) {
      Expect(sparse_turn.RawToIso(raw) == IsomorphicMapping::kInvalidIsoIndex,
             "non-AA/KK hand should be masked from sparse turn mapping");
      Expect(sparse_river.RawToIso(raw) == IsomorphicMapping::kInvalidIsoIndex,
             "non-AA/KK hand should be masked from sparse river mapping");
      continue;
    }

    const int turn_iso = sparse_turn.RawToIso(raw);
    const int river_iso = sparse_river.RawToIso(raw);
    if (is_aa && turn_iso >= 0) {
      ++turn_aa_count;
      sparse_turn_reach[static_cast<std::size_t>(turn_iso)] += 1.0f;
    }
    if (is_aa && river_iso >= 0) {
      ++river_aa_count;
    }
    if (is_kk && turn_iso >= 0) {
      ++turn_kk_count;
      sparse_turn_reach[static_cast<std::size_t>(turn_iso)] += 1.0f;
    }
    if (is_kk && river_iso >= 0) {
      ++river_kk_count;
    }
  }

  Expect(turn_aa_count == 1, "turn board should leave one AA combo");
  Expect(river_aa_count == 0, "river ace should block all AA combos");
  Expect(turn_kk_count == 6, "turn board should leave all KK combos");
  Expect(river_kk_count == 6, "river board should keep all KK combos");
  ExpectNear(Sum(sparse_turn_reach), 7.0f, "sparse turn reach mismatch");

  std::vector<float> sparse_river_reach;
  PropagateReachForward(sparse_turn, sparse_river, sparse_turn_reach,
                        &sparse_river_reach);
  ExpectNear(Sum(sparse_river_reach), 6.0f,
             "river reach should remove blocked AA mass");
  for (int raw = 0; raw < GameBasic::kNumHands; ++raw) {
    const auto& hand = game.HandFromIndex(raw);
    if (!IsPairWithRank(hand, PokerRank::kKing)) {
      continue;
    }
    const int river_iso = sparse_river.RawToIso(raw);
    Expect(river_iso >= 0, "KK hand should remain valid on river");
    Expect(sparse_river_reach[static_cast<std::size_t>(river_iso)] > 0.0f,
           "KK river bucket should receive reach");
  }

  std::vector<float> sparse_river_cfv(
      static_cast<std::size_t>(sparse_river.NumIsoHands()), 5.0f);
  std::vector<float> sparse_turn_cfv;
  PropagateCfvBackward(sparse_turn, sparse_river, sparse_river_cfv,
                       &sparse_turn_cfv);
  for (int raw = 0; raw < GameBasic::kNumHands; ++raw) {
    const auto& hand = game.HandFromIndex(raw);
    const int turn_iso = sparse_turn.RawToIso(raw);
    if (turn_iso < 0) {
      continue;
    }
    if (IsPairWithRank(hand, PokerRank::kAce)) {
      ExpectNear(sparse_turn_cfv[static_cast<std::size_t>(turn_iso)], 0.0f,
                 "blocked AA should receive zero backward CFV");
    }
    if (IsPairWithRank(hand, PokerRank::kKing)) {
      Expect(sparse_turn_cfv[static_cast<std::size_t>(turn_iso)] > 0.0f,
             "KK should receive backward CFV");
    }
  }

  ExpectInvalidArgument(
      [&] {
        std::vector<float> output;
        PropagateReachForward(flop, turn,
                              std::vector<float>(
                                  static_cast<std::size_t>(
                                      flop.NumIsoHands() - 1),
                                  0.0f),
                              &output);
      },
      "bad parent reach size should be invalid");
  ExpectInvalidArgument(
      [&] { PropagateReachForward(flop, turn, parent_reach, nullptr); },
      "null child reach should be invalid");
  ExpectInvalidArgument(
      [&] {
        std::vector<float> output;
        PropagateReachForward(flop, table.Get(PokerCards("AsKdQh2c3d")),
                              parent_reach, &output);
      },
      "non-adjacent child board should be invalid");
  ExpectInvalidArgument(
      [&] {
        std::vector<float> output;
        PropagateReachForward(flop, table.Get(PokerCards("AsKdJh2c")),
                              parent_reach, &output);
      },
      "non-prefix child board should be invalid");
  ExpectInvalidArgument(
      [&] {
        std::vector<float> output;
        PropagateCfvBackward(flop, turn,
                             std::vector<float>(
                                 static_cast<std::size_t>(
                                     turn.NumIsoHands() - 1),
                                 0.0f),
                             &output);
      },
      "bad child cfv size should be invalid");
  ExpectInvalidArgument(
      [&] { PropagateCfvBackward(flop, turn, child_cfv, nullptr); },
      "null parent cfv should be invalid");

  return 0;
}
