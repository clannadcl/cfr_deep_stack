#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/game_basic.h"
#include "game/poker/terminal_win_prob_matrix.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, const char* message) {
  if (std::fabs(actual - expected) > 1e-5f) {
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

std::vector<bool> PossibleHands(
    const fisher::game::poker::GameBasic& game_basic,
    const std::vector<std::string>& hands) {
  std::vector<bool> possible(fisher::game::poker::GameBasic::kNumHands, false);
  for (const std::string& hand_string : hands) {
    possible[static_cast<std::size_t>(
        game_basic.HandIndex(fisher::game::poker::PokerHand(hand_string)))] =
        true;
  }
  return possible;
}

int IsoIndex(const fisher::game::poker::GameBasic& game_basic,
             const fisher::game::poker::IsomorphicMapping& mapping,
             const char* hand) {
  return mapping.RawToIso(
      game_basic.HandIndex(fisher::game::poker::PokerHand(hand)));
}

}  // namespace

int main() {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::TerminalWinProbMatrix;
  using fisher::game::poker::IsomorphicMapping;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::SevenCardLookupTable;

  GameBasic game_basic;
  SevenCardLookupTable evaluator;

  {
    const PokerCards board("AcAdAhAs");
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "QcQd", "KhQh"}));
    TerminalWinProbMatrix matrix(game_basic, board, mapping, evaluator);

    const int kings = IsoIndex(game_basic, mapping, "KcKd");
    const int queens = IsoIndex(game_basic, mapping, "QcQd");
    const int king_queen = IsoIndex(game_basic, mapping, "KhQh");

    Expect(matrix.NumIsoHands() == mapping.NumIsoHands(),
           "matrix iso size should match mapping");
    constexpr float kKingOverQueenWin = 42.0f / 44.0f;
    ExpectNear(matrix.WinProb(kings, queens), kKingOverQueenWin,
               "KK should beat QQ except when river pairs K kicker");
    ExpectNear(matrix.LoseProb(queens, kings), kKingOverQueenWin,
               "QQ lose prob should be transpose of KK win prob");
    ExpectNear(matrix.EquityDelta(kings, queens), kKingOverQueenWin,
               "KK vs QQ delta should ignore chop runouts");
    ExpectNear(matrix.EquityDelta(queens, kings), -kKingOverQueenWin,
               "equity delta should be antisymmetric");
    ExpectNear(matrix.EquityDeltaData()[static_cast<std::size_t>(
                   kings * matrix.NumIsoHands() + queens)],
               matrix.EquityDelta(kings, queens),
               "stored equity delta should match accessor");
    ExpectNear(matrix.WinProb(kings, king_queen), 0.0f,
               "same K kicker should chop, not win");
    ExpectNear(matrix.WinProb(king_queen, kings), 0.0f,
               "same K kicker chop should be symmetric");
  }

  {
    const PokerCards board("AcAdAhAs");
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "KhKs", "QcQd"}));
    TerminalWinProbMatrix matrix(game_basic, board, mapping, evaluator);

    const int kings_one = IsoIndex(game_basic, mapping, "KcKd");
    const int kings_two = IsoIndex(game_basic, mapping, "KhKs");
    const int queens = IsoIndex(game_basic, mapping, "QcQd");

    constexpr float kKingOverQueenWin = 42.0f / 44.0f;
    ExpectNear(matrix.WinProb(kings_one, queens), kKingOverQueenWin,
               "all KK combos in bucket should beat QQ except K rivers");
    ExpectNear(matrix.WinProb(kings_two, queens), kKingOverQueenWin,
               "isomorphic KK combo should read same bucket win prob");
  }

  {
    const PokerCards board("AcAdAh");
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "QcQd"}));
    TerminalWinProbMatrix matrix(game_basic, board, mapping, evaluator);

    const int kings = IsoIndex(game_basic, mapping, "KcKd");
    const int queens = IsoIndex(game_basic, mapping, "QcQd");
    Expect(matrix.WinProb(kings, queens) > 0.9f,
           "KK should be a large favorite over QQ on AAA flop");
    ExpectNear(matrix.EquityDelta(kings, queens),
               -matrix.EquityDelta(queens, kings),
               "flop equity delta should be antisymmetric");
  }

  {
    const PokerCards river("AcAdAhAs2c");
    IsomorphicMapping mapping(
        game_basic, river,
        PossibleHands(game_basic, {"KcKd", "QcQd"}));
    ExpectInvalidArgument(
        [&] { TerminalWinProbMatrix(game_basic, river, mapping, evaluator); },
        "river hand equity matrix should be invalid");
  }

  return 0;
}
