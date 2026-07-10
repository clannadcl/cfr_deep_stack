#include <stdexcept>

#include "game/poker/poker_card.h"
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

}  // namespace

int main() {
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerHand;

  PokerHand hand("AcKh");
  Expect(hand.HighCard().ToString() == "Ac", "high card mismatch");
  Expect(hand.LowCard().ToString() == "Kh", "low card mismatch");
  Expect(hand.ToString() == "AcKh", "hand string mismatch");
  Expect(hand.Contains(PokerCard("Ac")), "hand should contain Ac");
  Expect(!hand.Contains(PokerCard("2c")), "hand should not contain 2c");
  Expect(hand.HasCollision(PokerCards("2cAc3d")),
         "hand-board collision should be detected");

  PokerHand reversed("KhAc");
  Expect(reversed.HighCard().ToString() == "Ac",
         "reversed high card mismatch");
  Expect(reversed.LowCard().ToString() == "Kh",
         "reversed low card mismatch");
  Expect(reversed.ToString() == hand.ToString(),
         "reversed hand should canonicalize");

  ExpectInvalidArgument([] { PokerHand invalid(PokerCards("Ac")); },
                        "one-card hand should be invalid");
  ExpectInvalidArgument([] { PokerHand invalid(PokerCards("AcKdQs")); },
                        "three-card hand should be invalid");
  ExpectInvalidArgument([] { PokerHand invalid(PokerCards("AcAc")); },
                        "duplicate hand should be invalid");

  return 0;
}
