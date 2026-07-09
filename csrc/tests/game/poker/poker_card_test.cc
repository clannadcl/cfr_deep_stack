#include <cstdint>
#include <stdexcept>

#include "game/poker/poker_card.h"

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
  using fisher::game::poker::PokerRank;
  using fisher::game::poker::PokerSuit;

  PokerCard two_clubs(static_cast<uint8_t>(0));
  Expect(two_clubs.Value() == 0, "2c value mismatch");
  Expect(two_clubs.Rank() == PokerRank::kTwo, "2c rank mismatch");
  Expect(two_clubs.Suit() == PokerSuit::kClubs, "2c suit mismatch");
  Expect(two_clubs.ToString() == "2c", "2c string mismatch");

  PokerCard two_spades("2s");
  Expect(two_spades.Value() == 3, "2s value mismatch");
  Expect(two_spades.Rank() == PokerRank::kTwo, "2s rank mismatch");
  Expect(two_spades.Suit() == PokerSuit::kSpades, "2s suit mismatch");
  Expect(two_spades.ToString() == "2s", "2s string mismatch");

  PokerCard ace_spades(static_cast<uint8_t>(51));
  Expect(ace_spades.Value() == 51, "As value mismatch");
  Expect(ace_spades.Rank() == PokerRank::kAce, "As rank mismatch");
  Expect(ace_spades.Suit() == PokerSuit::kSpades, "As suit mismatch");
  Expect(ace_spades.ToString() == "As", "As string mismatch");

  PokerCard ten_hearts("Th");
  Expect(ten_hearts.Value() == 34, "Th value mismatch");
  Expect(ten_hearts.Rank() == PokerRank::kTen, "Th rank mismatch");
  Expect(ten_hearts.Suit() == PokerSuit::kHearts, "Th suit mismatch");
  Expect(ten_hearts.ToString() == "Th", "Th string mismatch");

  ExpectInvalidArgument(
      [] { PokerCard invalid(static_cast<uint8_t>(52)); },
      "value 52 should be invalid");
  ExpectInvalidArgument([] { PokerCard invalid("1c"); },
                        "rank 1 should be invalid");
  ExpectInvalidArgument([] { PokerCard invalid("2x"); },
                        "suit x should be invalid");
  ExpectInvalidArgument([] { PokerCard invalid("10c"); },
                        "three-character card should be invalid");

  return 0;
}
