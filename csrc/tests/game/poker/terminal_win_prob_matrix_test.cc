#include <array>
#include <cstddef>
#include <cstdint>
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

struct TestRawHandEntry {
  int raw_index = -1;
  int iso_index = -1;
  uint8_t high_card = 0;
  uint8_t low_card = 0;
  float normalization = 0.0f;

  bool Contains(uint8_t card) const {
    return high_card == card || low_card == card;
  }

  bool CollidesWith(const TestRawHandEntry& other) const {
    return Contains(other.high_card) || Contains(other.low_card);
  }
};

std::vector<TestRawHandEntry> BuildTestRawHandEntries(
    const fisher::game::poker::GameBasic& game_basic,
    const fisher::game::poker::IsomorphicMapping& mapping) {
  std::vector<TestRawHandEntry> entries;
  for (int raw_index = 0; raw_index < fisher::game::poker::GameBasic::kNumHands;
       ++raw_index) {
    const int iso_index = mapping.RawToIso(raw_index);
    if (iso_index == fisher::game::poker::IsomorphicMapping::kInvalidIsoIndex) {
      continue;
    }
    const fisher::game::poker::PokerHand& hand =
        game_basic.HandFromIndex(raw_index);
    entries.push_back(TestRawHandEntry{
        raw_index,
        iso_index,
        hand.HighCard().Value(),
        hand.LowCard().Value(),
        1.0f / static_cast<float>(mapping.RawHandCount(iso_index)),
    });
  }
  return entries;
}

std::vector<uint8_t> TestRunoutCards(
    const fisher::game::poker::PokerCards& board) {
  std::array<bool, fisher::game::poker::GameBasic::kDeckSize> dead_cards{};
  for (const fisher::game::poker::PokerCard card : board.Cards()) {
    dead_cards[static_cast<std::size_t>(card.Value())] = true;
  }
  std::vector<uint8_t> runout_cards;
  for (uint8_t card = 0; card < fisher::game::poker::GameBasic::kDeckSize;
       ++card) {
    if (!dead_cards[static_cast<std::size_t>(card)]) {
      runout_cards.push_back(card);
    }
  }
  return runout_cards;
}

std::array<uint8_t, fisher::game::poker::PokerHandEvaluator::kSevenCards>
TestBaseSevenCards(const fisher::game::poker::PokerCards& board,
                   const TestRawHandEntry& hand) {
  std::array<uint8_t, fisher::game::poker::PokerHandEvaluator::kSevenCards>
      cards{};
  for (std::size_t index = 0; index < board.Size(); ++index) {
    cards[index] = board.Cards()[index].Value();
  }
  cards[5] = hand.high_card;
  cards[6] = hand.low_card;
  return cards;
}

float ReferenceRawBlockWinProb(
    const fisher::game::poker::PokerCards& board,
    const std::vector<uint8_t>& runout_cards,
    const fisher::game::poker::SevenCardLookupTable& evaluator,
    const TestRawHandEntry& hero, const TestRawHandEntry& opponent) {
  if (hero.CollidesWith(opponent)) {
    return 0.0f;
  }

  int wins = 0;
  int runouts = 0;
  auto hero_cards = TestBaseSevenCards(board, hero);
  auto opponent_cards = TestBaseSevenCards(board, opponent);
  if (board.Size() == 4) {
    for (uint8_t river : runout_cards) {
      if (hero.Contains(river) || opponent.Contains(river)) {
        continue;
      }
      hero_cards[4] = river;
      opponent_cards[4] = river;
      wins += evaluator.Evaluate7(hero_cards.data()) >
              evaluator.Evaluate7(opponent_cards.data());
      ++runouts;
    }
  } else if (board.Size() == 3) {
    for (std::size_t turn_index = 0; turn_index < runout_cards.size();
         ++turn_index) {
      const uint8_t turn = runout_cards[turn_index];
      if (hero.Contains(turn) || opponent.Contains(turn)) {
        continue;
      }
      hero_cards[3] = turn;
      opponent_cards[3] = turn;
      for (std::size_t river_index = turn_index + 1;
           river_index < runout_cards.size(); ++river_index) {
        const uint8_t river = runout_cards[river_index];
        if (hero.Contains(river) || opponent.Contains(river)) {
          continue;
        }
        hero_cards[4] = river;
        opponent_cards[4] = river;
        wins += evaluator.Evaluate7(hero_cards.data()) >
                evaluator.Evaluate7(opponent_cards.data());
        ++runouts;
      }
    }
  } else {
    throw std::runtime_error("reference matrix only supports flop or turn");
  }
  if (runouts <= 0) {
    throw std::runtime_error("reference matrix pair has no runouts");
  }
  return static_cast<float>(wins) / static_cast<float>(runouts);
}

std::vector<float> ReferenceFullBlockWinMatrix(
    const fisher::game::poker::GameBasic& game_basic,
    const fisher::game::poker::PokerCards& board,
    const fisher::game::poker::IsomorphicMapping& mapping,
    const fisher::game::poker::SevenCardLookupTable& evaluator) {
  const std::vector<TestRawHandEntry> hands =
      BuildTestRawHandEntries(game_basic, mapping);
  const std::vector<uint8_t> runout_cards = TestRunoutCards(board);
  std::vector<float> reference(static_cast<std::size_t>(
                                   mapping.NumIsoHands() *
                                   mapping.NumIsoHands()),
                               0.0f);
  for (const TestRawHandEntry& hero : hands) {
    for (const TestRawHandEntry& opponent : hands) {
      reference[static_cast<std::size_t>(hero.iso_index *
                                             mapping.NumIsoHands() +
                                         opponent.iso_index)] +=
          ReferenceRawBlockWinProb(board, runout_cards, evaluator, hero,
                                   opponent) *
          hero.normalization * opponent.normalization;
    }
  }
  return reference;
}

void ExpectMatchesReferenceFullBlock(
    const fisher::game::poker::GameBasic& game_basic,
    const fisher::game::poker::PokerCards& board,
    const std::vector<std::string>& possible_hands,
    const fisher::game::poker::SevenCardLookupTable& evaluator,
    const char* message) {
  const fisher::game::poker::IsomorphicMapping mapping(
      game_basic, board, PossibleHands(game_basic, possible_hands));
  const fisher::game::poker::TerminalWinProbMatrix matrix(game_basic, board,
                                                          mapping, evaluator);
  const std::vector<float> reference =
      ReferenceFullBlockWinMatrix(game_basic, board, mapping, evaluator);
  for (int hero_iso = 0; hero_iso < mapping.NumIsoHands(); ++hero_iso) {
    for (int opponent_iso = 0; opponent_iso < mapping.NumIsoHands();
         ++opponent_iso) {
      const float actual = matrix.WinProb(hero_iso, opponent_iso);
      const float expected =
          reference[static_cast<std::size_t>(hero_iso *
                                                mapping.NumIsoHands() +
                                            opponent_iso)];
      if (std::fabs(actual - expected) > 1e-5f) {
        throw std::runtime_error(message);
      }
    }
  }
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
    ExpectMatchesReferenceFullBlock(
        game_basic, PokerCards("AcAdAhAs"),
        {"KcKd", "KhKs", "QcQd", "QhQs", "JcJd", "JhJs"}, evaluator,
        "turn optimized matrix should match full raw block averaging");
  }

  {
    ExpectMatchesReferenceFullBlock(
        game_basic, PokerCards("2c7dJh"),
        {"AcAd", "AhAs", "KcKd", "KhKs", "QcQd", "QhQs", "TcTd", "ThTs"},
        evaluator,
        "flop optimized matrix should match full raw block averaging");
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
