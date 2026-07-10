#include <stdexcept>
#include <vector>

#include "game/poker/game_basic.h"
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
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsPokerCardsCollided;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerHand;
  using fisher::game::poker::PokerRound;
  using fisher::game::poker::RakeConfig;

  GameBasic game;
  Expect(game.NumPlayers() == 2, "num players mismatch");
  Expect(game.NumHoleCards() == 2, "num hole cards mismatch");
  Expect(game.Deck().size() == 52, "deck size mismatch");
  Expect(game.Deck().front().ToString() == "2c", "deck first card mismatch");
  Expect(game.Deck().back().ToString() == "As", "deck last card mismatch");
  Expect(game.AllHands().size() == 1326, "all hands size mismatch");

  const PokerHand& first_hand = game.HandFromIndex(0);
  Expect(first_hand.LowCard().ToString() == "2c",
         "first hand low card mismatch");
  Expect(first_hand.HighCard().ToString() == "2d",
         "first hand high card mismatch");
  Expect(game.HandIndex(first_hand) == 0, "first hand index mismatch");
  Expect(game.HandIndex(PokerHand("2d2c")) == 0,
         "reversed first hand index mismatch");

  const PokerHand& last_hand = game.HandFromIndex(1325);
  Expect(last_hand.LowCard().ToString() == "Ah",
         "last hand low card mismatch");
  Expect(last_hand.HighCard().ToString() == "As",
         "last hand high card mismatch");
  Expect(game.HandIndex(last_hand) == 1325, "last hand index mismatch");

  for (int index = 0; index < GameBasic::kNumHands; ++index) {
    Expect(game.HandIndex(game.HandFromIndex(index)) == index,
           "hand index roundtrip mismatch");
  }

  Expect(game.HandIndex(PokerHand("AsKh")) == game.HandIndex(PokerHand("KhAs")),
         "hand index should be order-independent");

  Expect(game.BoardRound(PokerCards()) == PokerRound::kPreflop,
         "preflop round mismatch");
  Expect(game.BoardRound(PokerCards("2c3d4h")) == PokerRound::kFlop,
         "flop round mismatch");
  Expect(game.BoardRound(PokerCards("2c3d4h5s")) == PokerRound::kTurn,
         "turn round mismatch");
  Expect(game.BoardRound(PokerCards("2c3d4h5s6c")) == PokerRound::kRiver,
         "river round mismatch");

  ExpectInvalidArgument([&] { game.ValidateBoard(PokerCards("2c")); },
                        "one-card board should be invalid");
  ExpectInvalidArgument([&] { game.ValidateBoard(PokerCards("2c3d")); },
                        "two-card board should be invalid");
  ExpectInvalidArgument([&] { game.ValidateBoard(PokerCards("2c3d4h5s6c7d")); },
                        "six-card board should be invalid");
  ExpectInvalidArgument([&] { PokerCards duplicate("2c2c3d"); },
                        "duplicate board should be invalid");
  ExpectInvalidArgument(
      [&] { game.ValidateHandAndBoard(PokerHand("AsKh"), PokerCards("AsTd4c")); },
      "hand-board collision should be invalid");

  Expect(IsPokerCardsCollided(PokerCards("AsKh"), PokerCards("2cAs")),
         "collided poker cards should be detected");
  Expect(!IsPokerCardsCollided(PokerCards("AsKh"), PokerCards("2c3d")),
         "non-collided poker cards should not collide");

  Expect(game.ValidHands(PokerCards("2c3d4h")).size() == 1176,
         "flop valid hand count mismatch");
  Expect(game.ValidHands(PokerCards("2c3d4h5s")).size() == 1128,
         "turn valid hand count mismatch");
  Expect(game.ValidHands(PokerCards("2c3d4h5s6c")).size() == 1081,
         "river valid hand count mismatch");

  RakeConfig rake;
  rake.enabled = true;
  rake.percentage = 0.05;
  rake.cap = 0.6;
  GameBasic rake_game(rake);
  Expect(rake_game.Rake().enabled, "rake should be enabled");
  Expect(rake_game.Rake().percentage == 0.05, "rake percentage mismatch");
  Expect(rake_game.Rake().cap == 0.6, "rake cap mismatch");

  ExpectInvalidArgument([] { GameBasic invalid({true, -0.01, 0.0}); },
                        "negative rake percentage should be invalid");
  ExpectInvalidArgument([] { GameBasic invalid({true, 1.01, 0.0}); },
                        "rake percentage above one should be invalid");
  ExpectInvalidArgument([] { GameBasic invalid({true, 0.05, -0.1}); },
                        "negative rake cap should be invalid");

  const hand_index_t board_index =
      game.BoardIsomorphicIndex(PokerCards("2c3c4c"));
  Expect(board_index == game.BoardIndexer().GetIndex(PokerCards("2c3c4c")),
         "board indexer passthrough mismatch");
  const hand_index_t hole_board_index =
      game.HoleBoardIsomorphicIndex(PokerHand("AsKh"), PokerCards("2c3d4h"));
  Expect(hole_board_index ==
             game.HoleBoardIndexer().GetIndex(PokerHand("AsKh"),
                                              PokerCards("2c3d4h")),
         "hole-board indexer passthrough mismatch");

  return 0;
}
