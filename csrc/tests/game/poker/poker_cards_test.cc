#include <cstdint>
#include <stdexcept>
#include <vector>

#include "game/poker/poker_card.h"
#include "game/poker/poker_cards.h"

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

}  // namespace

int main() {
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerCards;

  PokerCards empty;
  Expect(empty.Empty(), "default PokerCards should be empty");
  Expect(empty.Size() == 0, "empty PokerCards size mismatch");
  Expect(empty.ToString().empty(), "empty PokerCards string mismatch");

  PokerCards cards("AcKdTh");
  Expect(!cards.Empty(), "cards should not be empty");
  Expect(cards.Size() == 3, "cards size mismatch");
  Expect(cards.ToString() == "AcKdTh", "cards should preserve input order");
  Expect(cards.Contains(PokerCard("Ac")), "cards should contain Ac");
  Expect(!cards.Contains(PokerCard("2c")), "cards should not contain 2c");

  PokerCards from_ids(std::vector<uint8_t>{51, 0, 34});
  Expect(from_ids.ToString() == "As2cTh", "uint8 constructor should preserve order");

  PokerCards from_cards(std::vector<PokerCard>{PokerCard("As"), PokerCard("2c")});
  Expect(from_cards.ToString() == "As2c",
         "PokerCard constructor should preserve order");

  PokerCards board("2c3d4h");
  PokerCards hand("AcKd");
  PokerCards merged = board.Merge(hand);
  Expect(merged.ToString() == "2c3d4hAcKd", "merge output mismatch");
  Expect(board.HasCollision(PokerCards("2cAs")), "collision should be detected");
  Expect(!board.HasCollision(hand), "non-overlap should not collide");

  PokerCards difference = merged.Difference(PokerCards("3dAcQs"));
  Expect(difference.ToString() == "2c4hKd", "difference output mismatch");

  PokerCards deck = PokerCards::GenerateDeck();
  Expect(deck.Size() == 52, "deck should contain 52 cards");
  Expect(deck.ToString().substr(0, 8) == "2c2d2h2s", "deck prefix mismatch");
  Expect(deck.ToString().substr(deck.ToString().size() - 8) == "AcAdAhAs",
         "deck suffix mismatch");

  std::vector<PokerCards> combos = PokerCards("2c2d2h2s").Combinations(2);
  Expect(combos.size() == 6, "4 choose 2 should produce 6 combos");
  Expect(combos[0].ToString() == "2c2d", "first combo mismatch");
  Expect(combos[1].ToString() == "2c2h", "second combo mismatch");
  Expect(combos[5].ToString() == "2h2s", "last combo mismatch");

  std::vector<PokerCards> preflop_combos = deck.Combinations(2);
  Expect(preflop_combos.size() == 1326, "52 choose 2 should produce 1326 combos");
  Expect(preflop_combos.front().ToString() == "2c2d",
         "first deck combo should use the two smallest card indices");
  Expect(preflop_combos.back().ToString() == "AhAs",
         "last deck combo should use the two largest card indices");

  std::vector<PokerCards> zero_card_combos = PokerCards("AcKd").Combinations(0);
  Expect(zero_card_combos.size() == 1, "k=0 should produce one empty combo");
  Expect(zero_card_combos[0].Empty(), "k=0 combo should be empty");

  ExpectInvalidArgument([] { PokerCards invalid("AcA"); },
                        "odd-length string should be invalid");
  ExpectInvalidArgument([] { PokerCards invalid("1c"); },
                        "invalid rank should be invalid");
  ExpectInvalidArgument([] { PokerCards invalid("Ax"); },
                        "invalid suit should be invalid");
  ExpectInvalidArgument([] { PokerCards invalid("AcAc"); },
                        "duplicate string cards should be invalid");
  ExpectInvalidArgument([] { PokerCards invalid(std::vector<uint8_t>{52}); },
                        "invalid uint8 card id should be invalid");
  ExpectInvalidArgument([] { PokerCards("Ac").Merge(PokerCards("Ac")); },
                        "merge collision should be invalid");
  ExpectInvalidArgument([] { PokerCards("AcKd").Combinations(-1); },
                        "negative k should be invalid");
  ExpectInvalidArgument([] { PokerCards("AcKd").Combinations(3); },
                        "k larger than size should be invalid");

  return 0;
}
