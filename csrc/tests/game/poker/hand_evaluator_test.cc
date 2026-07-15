#include <filesystem>
#include <stdexcept>
#include <string>

#include "game/poker/hand_evaluator.h"
#include "game/poker/poker_cards.h"

namespace {

std::string TestCachePath() {
#ifdef FISHER_LOOKUP_CACHE_DIR
  return std::string(FISHER_LOOKUP_CACHE_DIR) +
         "/hand_evaluator_test_lookup.bin";
#else
  return "hand_evaluator_test_lookup.bin";
#endif
}

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

uint32_t EvaluateBoardHandRaw(const char* board, const char* hand) {
  return fisher::game::poker::PokerHandEvaluator::Evaluate7Raw(
      fisher::game::poker::PokerCards(std::string(board) + hand));
}

uint16_t EvaluateBoardHandLookup(
    const fisher::game::poker::SevenCardLookupTable& lookup,
    const char* board, const char* hand) {
  return lookup.Evaluate7(fisher::game::poker::PokerCards(
      std::string(board) + hand));
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

}  // namespace

int main() {
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerHandEvaluator;
  using fisher::game::poker::SevenCardLookupTable;

  const uint32_t high_card =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcKdQh9s7c4d2h"));
  const uint32_t pair =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcAdQh9s7c4d2h"));
  const uint32_t two_pair =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcAdQhQs7c4d2h"));
  const uint32_t trips =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcAdAhQs7c4d2h"));
  const uint32_t straight =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcKdQhJsTc4d2h"));
  const uint32_t wheel =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("Ac2d3h4s5c9dTh"));
  const uint32_t six_high_straight =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("2c3d4h5s6c9dTh"));
  const uint32_t flush =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcQc9c7c4cKd2h"));
  const uint32_t full_house =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcAdAhQsQd4d2h"));
  const uint32_t quads =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcAdAhAsQd4d2h"));
  const uint32_t straight_flush =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcKcQcJcTc4d2h"));

  Expect(high_card < pair, "pair should beat high card");
  Expect(pair < two_pair, "two pair should beat one pair");
  Expect(two_pair < trips, "trips should beat two pair");
  Expect(trips < straight, "straight should beat trips");
  Expect(straight < flush, "flush should beat straight");
  Expect(flush < full_house, "full house should beat flush");
  Expect(full_house < quads, "quads should beat full house");
  Expect(quads < straight_flush, "straight flush should beat quads");
  Expect(wheel < six_high_straight, "6-high straight should beat wheel");

  Expect(EvaluateBoardHandRaw("AsKdQh2c3d", "AcAh") >
             EvaluateBoardHandRaw("AsKdQh2c3d", "KsKh"),
         "set of aces should beat set of kings");
  Expect(EvaluateBoardHandRaw("AsKsQsJsTs", "AcAh") ==
             EvaluateBoardHandRaw("AsKsQsJsTs", "2c2h"),
         "royal flush board should chop all hands");
  Expect(EvaluateBoardHandRaw("AcKcQc2d3h", "JcTc") >
             EvaluateBoardHandRaw("AcKcQc2d3h", "AdAh"),
         "straight flush should beat top set");
  Expect(EvaluateBoardHandRaw("AhKhQh2c3d", "9h8h") >
             EvaluateBoardHandRaw("AhKhQh2c3d", "AsAd"),
         "flush should beat one pair");
  Expect(EvaluateBoardHandRaw("AcKdQh2c3d", "AhJd") >
             EvaluateBoardHandRaw("AcKdQh2c3d", "As9s"),
         "same pair should compare kickers");

  const uint32_t clubs_straight_flush =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AcKcQcJcTc4d2h"));
  const uint32_t diamonds_straight_flush =
      PokerHandEvaluator::Evaluate7Raw(PokerCards("AdKdQdJdTd4c2h"));
  Expect(clubs_straight_flush == diamonds_straight_flush,
         "suit-isomorphic hands should have same raw value");

  ExpectInvalidArgument(
      [] { PokerHandEvaluator::Evaluate7Raw(PokerCards("AcKdQh9s7c4d")); },
      "six-card hand should be invalid");
  ExpectInvalidArgument(
      [] { PokerHandEvaluator::Evaluate7Raw(PokerCards("AcAcQh9s7c4d2h")); },
      "duplicate cards should be invalid");

  SevenCardLookupTable lookup;
  Expect(lookup.Size() == 6009159, "7-card iso lookup size mismatch");
  Expect(lookup.NumStrengths() == 4824, "7-card strength count mismatch");
  Expect(lookup.Evaluate7(PokerCards("AcKdQh9s7c4d2h")) <
             lookup.Evaluate7(PokerCards("AcAdQh9s7c4d2h")),
         "lookup pair should beat high card");
  Expect(lookup.Evaluate7(PokerCards("AcKcQcJcTc4d2h")) ==
             lookup.Evaluate7(PokerCards("AdKdQdJdTd4c2h")),
         "lookup should preserve suit-isomorphic equal strength");
  Expect(EvaluateBoardHandLookup(lookup, "AsKdQh2c3d", "AcAh") >
             EvaluateBoardHandLookup(lookup, "AsKdQh2c3d", "KsKh"),
         "lookup set of aces should beat set of kings");
  Expect(EvaluateBoardHandLookup(lookup, "AsKsQsJsTs", "AcAh") ==
             EvaluateBoardHandLookup(lookup, "AsKsQsJsTs", "2c2h"),
         "lookup royal flush board should chop all hands");
  Expect(EvaluateBoardHandLookup(lookup, "AhKhQh2c3d", "9h8h") >
             EvaluateBoardHandLookup(lookup, "AhKhQh2c3d", "AsAd"),
         "lookup flush should beat one pair");

  const hand_index_t index = lookup.IsomorphicIndex(PokerCards("AcKcQcJcTc4d2h"));
  Expect(lookup.StrengthByIndex(index) ==
             lookup.Evaluate7(PokerCards("AcKcQcJcTc4d2h")),
         "lookup index and evaluate mismatch");
  ExpectInvalidArgument([&] { lookup.StrengthByIndex(lookup.Size()); },
                        "out-of-range lookup index should be invalid");

  const std::string cache_path = TestCachePath();
  std::filesystem::remove(cache_path);
  SevenCardLookupTable cached_builder(cache_path);
  Expect(std::filesystem::exists(cache_path), "lookup cache should be written");
  const uint16_t cached_strength =
      cached_builder.Evaluate7(PokerCards("AcKcQcJcTc4d2h"));
  SevenCardLookupTable cached_loader(cache_path);
  Expect(cached_loader.Evaluate7(PokerCards("AcKcQcJcTc4d2h")) ==
             cached_strength,
         "lookup cache load should preserve strength");
  Expect(cached_loader.Size() == lookup.Size(),
         "cached lookup size should match");
  Expect(cached_loader.NumStrengths() == lookup.NumStrengths(),
         "cached strength count should match");

  return 0;
}
