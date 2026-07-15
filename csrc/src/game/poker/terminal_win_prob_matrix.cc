#include "game/poker/terminal_win_prob_matrix.h"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace fisher::game::poker {
namespace {

constexpr int kCardsPerHand = 2;
constexpr int kRiverBoardCards = 5;
constexpr int kSevenCards = PokerHandEvaluator::kSevenCards;

std::array<bool, GameBasic::kDeckSize> BoardDeadCards(const PokerCards& board) {
  std::array<bool, GameBasic::kDeckSize> dead_cards{};
  for (PokerCard card : board.Cards()) {
    dead_cards[static_cast<std::size_t>(card.Value())] = true;
  }
  return dead_cards;
}

}  // namespace

TerminalWinProbMatrix::TerminalWinProbMatrix(
    const GameBasic& game_basic, const PokerCards& board,
    const IsomorphicMapping& mapping, const SevenCardLookupTable& evaluator)
    : board_(board), round_(game_basic.BoardRound(board)) {
  if (round_ != PokerRound::kFlop && round_ != PokerRound::kTurn) {
    throw std::invalid_argument(
        "TerminalWinProbMatrix only supports flop or turn");
  }
  if (board_.ToString() != mapping.RawBoard().ToString()) {
    throw std::invalid_argument(
        "TerminalWinProbMatrix board must match isomorphic mapping board");
  }

  num_iso_hands_ = mapping.NumIsoHands();
  if (num_iso_hands_ <= 0) {
    throw std::invalid_argument("TerminalWinProbMatrix requires iso hands");
  }
  win_prob_.assign(static_cast<std::size_t>(num_iso_hands_ * num_iso_hands_),
                   0.0f);
  Build(game_basic, mapping, evaluator);
}

int TerminalWinProbMatrix::NumIsoHands() const { return num_iso_hands_; }

float TerminalWinProbMatrix::WinProb(int hero_iso, int opponent_iso) const {
  ValidateIsoIndex(hero_iso);
  ValidateIsoIndex(opponent_iso);
  return win_prob_[static_cast<std::size_t>(hero_iso * num_iso_hands_ +
                                            opponent_iso)];
}

float TerminalWinProbMatrix::LoseProb(int hero_iso, int opponent_iso) const {
  return WinProb(opponent_iso, hero_iso);
}

float TerminalWinProbMatrix::EquityDelta(int hero_iso, int opponent_iso) const {
  return WinProb(hero_iso, opponent_iso) - LoseProb(hero_iso, opponent_iso);
}

const PokerCards& TerminalWinProbMatrix::Board() const { return board_; }

const std::vector<float>& TerminalWinProbMatrix::WinProbData() const {
  return win_prob_;
}

void TerminalWinProbMatrix::Build(
    const GameBasic& game_basic, const IsomorphicMapping& mapping,
    const SevenCardLookupTable& evaluator) {
  for (int hero_raw = 0; hero_raw < GameBasic::kNumHands; ++hero_raw) {
    const int hero_iso = mapping.RawToIso(hero_raw);
    if (hero_iso == IsomorphicMapping::kInvalidIsoIndex) {
      continue;
    }
    const PokerHand& hero = game_basic.HandFromIndex(hero_raw);
    const float hero_bucket_count =
        static_cast<float>(mapping.RawHandCount(hero_iso));

    for (int opponent_raw = 0; opponent_raw < GameBasic::kNumHands;
         ++opponent_raw) {
      const int opponent_iso = mapping.RawToIso(opponent_raw);
      if (opponent_iso == IsomorphicMapping::kInvalidIsoIndex) {
        continue;
      }
      const PokerHand& opponent = game_basic.HandFromIndex(opponent_raw);
      if (hero.HasCollision(opponent.ToPokerCards())) {
        continue;
      }

      const float opponent_bucket_count =
          static_cast<float>(mapping.RawHandCount(opponent_iso));
      const float normalization =
          1.0f / (hero_bucket_count * opponent_bucket_count);
      AccumulateRawPair(game_basic, evaluator, hero_raw, opponent_raw,
                        hero_iso, opponent_iso, normalization);
    }
  }
}

void TerminalWinProbMatrix::AccumulateRawPair(
    const GameBasic& game_basic, const SevenCardLookupTable& evaluator,
    int hero_raw, int opponent_raw, int hero_iso, int opponent_iso,
    float normalization) {
  const PokerHand& hero = game_basic.HandFromIndex(hero_raw);
  const PokerHand& opponent = game_basic.HandFromIndex(opponent_raw);
  const std::vector<uint8_t> remaining_deck =
      RemainingDeckForPair(hero, opponent);
  const float raw_win_probability =
      RawWinProbability(evaluator, hero, opponent, remaining_deck);
  win_prob_[static_cast<std::size_t>(hero_iso * num_iso_hands_ +
                                    opponent_iso)] +=
      raw_win_probability * normalization;
}

void TerminalWinProbMatrix::ValidateIsoIndex(int iso_index) const {
  if (iso_index < 0 || iso_index >= num_iso_hands_) {
    throw std::invalid_argument(
        "TerminalWinProbMatrix iso index is out of range");
  }
}

std::vector<uint8_t> TerminalWinProbMatrix::RemainingDeckForPair(
    const PokerHand& hero, const PokerHand& opponent) const {
  std::array<bool, GameBasic::kDeckSize> dead_cards = BoardDeadCards(board_);
  dead_cards[static_cast<std::size_t>(hero.HighCard().Value())] = true;
  dead_cards[static_cast<std::size_t>(hero.LowCard().Value())] = true;
  dead_cards[static_cast<std::size_t>(opponent.HighCard().Value())] = true;
  dead_cards[static_cast<std::size_t>(opponent.LowCard().Value())] = true;

  std::vector<uint8_t> remaining_deck;
  remaining_deck.reserve(GameBasic::kDeckSize - board_.Size() -
                         2 * kCardsPerHand);
  for (uint8_t card = 0; card < GameBasic::kDeckSize; ++card) {
    if (!dead_cards[static_cast<std::size_t>(card)]) {
      remaining_deck.push_back(card);
    }
  }
  return remaining_deck;
}

float TerminalWinProbMatrix::RawWinProbability(
    const SevenCardLookupTable& evaluator, const PokerHand& hero,
    const PokerHand& opponent, const std::vector<uint8_t>& remaining_deck) const {
  std::array<uint8_t, kSevenCards> hero_cards{};
  std::array<uint8_t, kSevenCards> opponent_cards{};
  int index = 0;
  for (PokerCard card : board_.Cards()) {
    hero_cards[static_cast<std::size_t>(index)] = card.Value();
    opponent_cards[static_cast<std::size_t>(index)] = card.Value();
    ++index;
  }

  hero_cards[5] = hero.HighCard().Value();
  hero_cards[6] = hero.LowCard().Value();
  opponent_cards[5] = opponent.HighCard().Value();
  opponent_cards[6] = opponent.LowCard().Value();

  int wins = 0;
  int runouts = 0;
  const int cards_to_river =
      kRiverBoardCards - static_cast<int>(board_.Size());
  if (cards_to_river == 1) {
    for (uint8_t river : remaining_deck) {
      hero_cards[4] = river;
      opponent_cards[4] = river;
      if (evaluator.Evaluate7(hero_cards.data()) >
          evaluator.Evaluate7(opponent_cards.data())) {
        ++wins;
      }
      ++runouts;
    }
  } else if (cards_to_river == 2) {
    for (std::size_t turn_index = 0; turn_index < remaining_deck.size();
         ++turn_index) {
      for (std::size_t river_index = turn_index + 1;
           river_index < remaining_deck.size(); ++river_index) {
        hero_cards[3] = remaining_deck[turn_index];
        hero_cards[4] = remaining_deck[river_index];
        opponent_cards[3] = remaining_deck[turn_index];
        opponent_cards[4] = remaining_deck[river_index];
        if (evaluator.Evaluate7(hero_cards.data()) >
            evaluator.Evaluate7(opponent_cards.data())) {
          ++wins;
        }
        ++runouts;
      }
    }
  } else {
    throw std::runtime_error("Unexpected TerminalWinProbMatrix runout count");
  }

  if (runouts <= 0) {
    throw std::runtime_error("TerminalWinProbMatrix raw pair has no runouts");
  }
  return static_cast<float>(wins) / static_cast<float>(runouts);
}

}  // namespace fisher::game::poker
