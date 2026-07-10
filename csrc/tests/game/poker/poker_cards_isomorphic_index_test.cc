#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/poker_cards_isomorphic_index.h"
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

std::string PrefixString(const fisher::game::poker::PokerCards& cards,
                         std::size_t size) {
  std::string output;
  for (std::size_t index = 0; index < size; ++index) {
    output += cards.Cards()[index].ToString();
  }
  return output;
}

fisher::game::poker::PokerCard FirstCardNotIn(
    const fisher::game::poker::PokerCards& cards) {
  for (uint8_t card = 0; card < 52; ++card) {
    fisher::game::poker::PokerCard candidate(card);
    if (!cards.Contains(candidate)) {
      return candidate;
    }
  }
  throw std::runtime_error("expected a non-colliding card");
}

}  // namespace

int main() {
  using fisher::game::poker::HoleBoardCards;
  using fisher::game::poker::PokerCardsIsomorphicBoardIndex;
  using fisher::game::poker::PokerCardsIsomorphicHoleBoardIndex;
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerRound;

  PokerCardsIsomorphicBoardIndex board_index;
  const hand_index_t flop_index = board_index.GetIndex(PokerCards("2c3c4c"));
  Expect(flop_index == board_index.GetIndex(PokerCards("2c3c4c")),
         "same board should have stable index");
  Expect(flop_index == board_index.GetIndex(PokerCards("2d3d4d")),
         "isomorphic monotone flops should have same index");

  const hand_index_t turn_index = board_index.GetIndex(PokerCards("2c3c4c5d"));
  Expect(turn_index == board_index.GetIndex(PokerCards("2d3d4d5h")),
         "isomorphic turns should have same index");

  const hand_index_t river_index =
      board_index.GetIndex(PokerCards("2c3c4c5d6h"));
  Expect(river_index == board_index.GetIndex(PokerCards("2d3d4d5h6s")),
         "isomorphic rivers should have same index");

  PokerCards flop_representative =
      board_index.GetBoard(PokerRound::kFlop, flop_index);
  Expect(board_index.GetIndex(flop_representative) == flop_index,
         "flop index roundtrip mismatch");
  PokerCards turn_representative =
      board_index.GetBoard(PokerRound::kTurn, turn_index);
  Expect(board_index.GetIndex(turn_representative) == turn_index,
         "turn index roundtrip mismatch");
  PokerCards river_representative =
      board_index.GetBoard(PokerRound::kRiver, river_index);
  Expect(board_index.GetIndex(river_representative) == river_index,
         "river index roundtrip mismatch");

  const hand_index_t prefix_flop_index =
      board_index.GetIndex(PokerCards("KcTd4s"));
  const PokerCards stable_flop =
      board_index.GetBoard(PokerRound::kFlop, prefix_flop_index);
  const PokerCard turn_card = FirstCardNotIn(stable_flop);
  PokerCards stable_turn_input = stable_flop.Merge(PokerCards({turn_card}));
  const hand_index_t stable_turn_index =
      board_index.GetIndex(stable_turn_input);
  const PokerCards stable_turn =
      board_index.GetBoard(PokerRound::kTurn, stable_turn_index);
  Expect(PrefixString(stable_turn, 3) == stable_flop.ToString(),
         "turn representative should preserve flop prefix");

  const PokerCard river_card = FirstCardNotIn(stable_turn);
  PokerCards stable_river_input = stable_turn.Merge(PokerCards({river_card}));
  const hand_index_t stable_river_index =
      board_index.GetIndex(stable_river_input);
  const PokerCards stable_river =
      board_index.GetBoard(PokerRound::kRiver, stable_river_index);
  Expect(PrefixString(stable_river, 4) == stable_turn.ToString(),
         "river representative should preserve turn prefix");

  const PokerCards new_turn = board_index.GetNewBoardCards(
      stable_flop, board_index.GetIndex(stable_turn_input));
  Expect(PrefixString(new_turn, 3) == stable_flop.ToString(),
         "GetNewBoardCards should preserve previous flop prefix");

  PokerCardsIsomorphicHoleBoardIndex hole_board_index;
  const hand_index_t preflop_hand_index =
      hole_board_index.GetIndex(PokerCards("AcKd"), PokerCards());
  Expect(preflop_hand_index ==
             hole_board_index.GetIndex(PokerCards("KdAc"), PokerCards()),
         "preflop hole-card order should not change index");
  HoleBoardCards preflop_cards =
      hole_board_index.GetCards(PokerRound::kPreflop, preflop_hand_index);
  Expect(hole_board_index.GetIndex(preflop_cards.hole_cards,
                                   preflop_cards.board) ==
             preflop_hand_index,
         "preflop hole index roundtrip mismatch");

  const hand_index_t flop_hand_index =
      hole_board_index.GetIndex(PokerCards("AcKd"), PokerCards("2c3d4h"));
  Expect(flop_hand_index ==
             hole_board_index.GetIndex(PokerCards("KdAc"),
                                       PokerCards("2c3d4h")),
         "postflop hole-card order should not change index");
  HoleBoardCards flop_cards =
      hole_board_index.GetCards(PokerRound::kFlop, flop_hand_index);
  Expect(hole_board_index.GetIndex(flop_cards.hole_cards, flop_cards.board) ==
             flop_hand_index,
         "flop hole-board index roundtrip mismatch");

  Expect(hole_board_index.GetIndex(PokerCards("AcKc"), PokerCards("2s3s4s")) ==
             hole_board_index.GetIndex(PokerCards("AdKd"),
                                       PokerCards("2c3c4c")),
         "isomorphic hole-board cards should have same index");

  ExpectInvalidArgument(
      [&] { board_index.GetIndex(PokerCards("AcKd")); },
      "two-card board should be invalid");
  ExpectInvalidArgument(
      [&] { board_index.GetBoard(PokerRound::kFlop,
                                 board_index.RoundSize(PokerRound::kFlop)); },
      "out-of-range board index should be invalid");
  ExpectInvalidArgument(
      [&] { hole_board_index.GetIndex(PokerCards("Ac"), PokerCards()); },
      "one hole card should be invalid");
  ExpectInvalidArgument(
      [&] {
        hole_board_index.GetIndex(PokerCards("AcKd"), PokerCards("Ac2d3h"));
      },
      "hole-board collision should be invalid");
  ExpectInvalidArgument(
      [&] { PokerCards duplicate("AcAc"); },
      "duplicate PokerCards construction should be invalid");

  return 0;
}
