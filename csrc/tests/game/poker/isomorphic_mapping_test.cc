#include <stdexcept>
#include <vector>

#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
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

}  // namespace

int main() {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsomorphicMapping;
  using fisher::game::poker::IsomorphicMappingTable;
  using fisher::game::poker::PokerBelief;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerHand;
  using fisher::game::poker::PokerRound;

  GameBasic game;
  PokerBelief full_belief(FullBelief());
  PokerCards board("2s3s4s");
  IsomorphicMapping mapping(game, board, std::vector<bool>(
                                             GameBasic::kNumHands, true));

  Expect(mapping.RawBoard().ToString() == "2s3s4s", "raw board mismatch");
  Expect(mapping.Round() == PokerRound::kFlop, "round mismatch");
  Expect(mapping.NumIsoHands() > 0, "mapping should have iso hands");
  Expect(mapping.RawIndexToIsoIndex().size() == GameBasic::kNumHands,
         "raw mapping size mismatch");
  Expect(mapping.IsoIndexToRawIndex().size() ==
             static_cast<std::size_t>(mapping.NumIsoHands()),
         "representative mapping size mismatch");
  Expect(mapping.RawHandCountForEachIsoHand().size() ==
             static_cast<std::size_t>(mapping.NumIsoHands()),
         "hand count mapping size mismatch");

  std::vector<bool> seen(static_cast<std::size_t>(mapping.NumIsoHands()),
                         false);
  int valid_raw_count = 0;
  for (int raw = 0; raw < GameBasic::kNumHands; ++raw) {
    const int iso = mapping.RawToIso(raw);
    if (iso < 0) {
      continue;
    }
    Expect(iso < mapping.NumIsoHands(), "compact iso index out of range");
    seen[static_cast<std::size_t>(iso)] = true;
    ++valid_raw_count;
  }
  for (bool bucket_seen : seen) {
    Expect(bucket_seen, "compact iso index should be contiguous");
  }
  int counted_raw = 0;
  for (int iso = 0; iso < mapping.NumIsoHands(); ++iso) {
    counted_raw += mapping.RawHandCount(iso);
    const int representative = mapping.IsoToRepresentativeRaw(iso);
    Expect(representative >= 0 && representative < GameBasic::kNumHands,
           "representative raw index mismatch");
    Expect(mapping.RawToIso(representative) == iso,
           "representative should map back to iso bucket");
  }
  Expect(counted_raw == valid_raw_count, "raw hand count sum mismatch");

  const int board_collision = game.HandIndex(PokerHand("As2s"));
  Expect(mapping.RawToIso(board_collision) ==
             IsomorphicMapping::kInvalidIsoIndex,
         "board collision hand should be invalid");

  const int clubs_hand = game.HandIndex(PokerHand("AcKc"));
  const int diamonds_hand = game.HandIndex(PokerHand("AdKd"));
  Expect(mapping.RawToIso(clubs_hand) >= 0,
         "clubs hand should be valid on monotone board");
  Expect(mapping.RawToIso(clubs_hand) == mapping.RawToIso(diamonds_hand),
         "isomorphic hands should share iso index");

  const int both_zero = game.HandIndex(PokerHand("AcKd"));
  std::vector<bool> partial_root_possible(
      static_cast<std::size_t>(GameBasic::kNumHands), true);
  partial_root_possible[static_cast<std::size_t>(both_zero)] = false;
  ExpectInvalidArgument(
      [&] { IsomorphicMapping(game, board, partial_root_possible); },
      "partial iso bucket root mask should be invalid");

  const int disabled_iso = mapping.RawToIso(both_zero);
  Expect(disabled_iso >= 0, "disabled test hand should be valid first");
  std::vector<std::vector<float>> masked = FullBelief();
  std::vector<int> disabled_raw_hands;
  for (int raw = 0; raw < GameBasic::kNumHands; ++raw) {
    if (mapping.RawToIso(raw) != disabled_iso) {
      continue;
    }
    disabled_raw_hands.push_back(raw);
    masked[0][static_cast<std::size_t>(raw)] = 0.0f;
    masked[1][static_cast<std::size_t>(raw)] = 0.0f;
  }
  Expect(!disabled_raw_hands.empty(), "should disable one full iso bucket");
  PokerBelief masked_belief(masked);
  IsomorphicMappingTable table(game, masked_belief);
  Expect(!table.Contains(board), "table should start empty");
  const IsomorphicMapping& masked_mapping = table.Get(board);
  Expect(table.Contains(board), "table should contain built board");
  Expect(&masked_mapping == &table.Get(board), "table should cache mapping");
  Expect(masked_mapping.NumIsoHands() == mapping.NumIsoHands() - 1,
         "closed bucket removal should reduce iso hand count by one");
  for (int raw : disabled_raw_hands) {
    Expect(masked_mapping.RawToIso(raw) ==
               IsomorphicMapping::kInvalidIsoIndex,
           "closed bucket raw hand should be invalid");
  }

  const IsomorphicMapping& turn_mapping = table.Get(PokerCards("2s3s4s5c"));
  Expect(&turn_mapping != &masked_mapping,
         "different board should have different mapping");

  ExpectInvalidArgument(
      [&] { table.Get(PokerCards()); },
      "preflop mapping should be invalid");
  ExpectInvalidArgument(
      [&] { mapping.RawToIso(-1); },
      "negative raw hand should be invalid");
  ExpectInvalidArgument(
      [&] { mapping.RawToIso(GameBasic::kNumHands); },
      "large raw hand should be invalid");
  ExpectInvalidArgument(
      [&] { mapping.IsoToRepresentativeRaw(-1); },
      "negative iso hand should be invalid");
  ExpectInvalidArgument(
      [&] { mapping.RawHandCount(mapping.NumIsoHands()); },
      "large iso hand should be invalid");

  return 0;
}
