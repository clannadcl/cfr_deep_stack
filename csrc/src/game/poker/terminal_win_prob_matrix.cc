#include "game/poker/terminal_win_prob_matrix.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace fisher::game::poker {
namespace {

constexpr int kSevenCards = PokerHandEvaluator::kSevenCards;
constexpr uint16_t kInvalidStrength = UINT16_MAX;

struct RawHandEntry {
  int raw_index = -1;
  int iso_index = -1;
  uint8_t high_card = 0;
  uint8_t low_card = 0;
  float normalization = 0.0f;

  bool Contains(uint8_t card) const {
    return high_card == card || low_card == card;
  }

  bool CollidesWith(const RawHandEntry& other) const {
    return Contains(other.high_card) || Contains(other.low_card);
  }
};

std::array<bool, GameBasic::kDeckSize> BoardDeadCards(const PokerCards& board) {
  std::array<bool, GameBasic::kDeckSize> dead_cards{};
  for (PokerCard card : board.Cards()) {
    dead_cards[static_cast<std::size_t>(card.Value())] = true;
  }
  return dead_cards;
}

std::vector<uint8_t> RemainingBoardCards(const PokerCards& board) {
  const std::array<bool, GameBasic::kDeckSize> board_dead =
      BoardDeadCards(board);
  std::vector<uint8_t> cards;
  cards.reserve(GameBasic::kDeckSize - board.Size());
  for (uint8_t card = 0; card < GameBasic::kDeckSize; ++card) {
    if (!board_dead[static_cast<std::size_t>(card)]) {
      cards.push_back(card);
    }
  }
  return cards;
}

std::vector<RawHandEntry> BuildRawHandEntries(
    const GameBasic& game_basic, const IsomorphicMapping& mapping) {
  std::vector<RawHandEntry> entries;
  entries.reserve(GameBasic::kNumHands);
  for (int raw_index = 0; raw_index < GameBasic::kNumHands; ++raw_index) {
    const int iso_index = mapping.RawToIso(raw_index);
    if (iso_index == IsomorphicMapping::kInvalidIsoIndex) {
      continue;
    }
    const PokerHand& hand = game_basic.HandFromIndex(raw_index);
    entries.push_back(RawHandEntry{
        raw_index,
        iso_index,
        hand.HighCard().Value(),
        hand.LowCard().Value(),
        1.0f / static_cast<float>(mapping.RawHandCount(iso_index)),
    });
  }
  return entries;
}

std::array<uint8_t, kSevenCards> BaseSevenCards(const PokerCards& board,
                                                const RawHandEntry& hand) {
  std::array<uint8_t, kSevenCards> cards{};
  int index = 0;
  for (PokerCard board_card : board.Cards()) {
    cards[static_cast<std::size_t>(index)] = board_card.Value();
    ++index;
  }
  cards[5] = hand.high_card;
  cards[6] = hand.low_card;
  return cards;
}

std::vector<std::array<uint16_t, GameBasic::kDeckSize>> BuildTurnStrengths(
    const PokerCards& board, const std::vector<RawHandEntry>& hands,
    const std::vector<uint8_t>& runout_cards,
    const SevenCardLookupTable& evaluator) {
  std::vector<std::array<uint16_t, GameBasic::kDeckSize>> strengths(
      GameBasic::kNumHands);
  for (auto& row : strengths) {
    row.fill(kInvalidStrength);
  }

  for (const RawHandEntry& hand : hands) {
    std::array<uint8_t, kSevenCards> cards = BaseSevenCards(board, hand);
    for (uint8_t river : runout_cards) {
      if (hand.Contains(river)) {
        continue;
      }
      cards[4] = river;
      strengths[static_cast<std::size_t>(hand.raw_index)]
               [static_cast<std::size_t>(river)] =
                   evaluator.Evaluate7(cards.data());
    }
  }
  return strengths;
}

std::vector<std::array<uint16_t, GameBasic::kDeckSize * GameBasic::kDeckSize>>
BuildFlopStrengths(const PokerCards& board,
                   const std::vector<RawHandEntry>& hands,
                   const std::vector<uint8_t>& runout_cards,
                   const SevenCardLookupTable& evaluator) {
  std::vector<std::array<uint16_t, GameBasic::kDeckSize * GameBasic::kDeckSize>>
      strengths(GameBasic::kNumHands);
  for (auto& row : strengths) {
    row.fill(kInvalidStrength);
  }

  for (const RawHandEntry& hand : hands) {
    std::array<uint8_t, kSevenCards> cards = BaseSevenCards(board, hand);
    for (std::size_t turn_index = 0; turn_index < runout_cards.size();
         ++turn_index) {
      const uint8_t turn = runout_cards[turn_index];
      if (hand.Contains(turn)) {
        continue;
      }
      cards[3] = turn;
      for (std::size_t river_index = turn_index + 1;
           river_index < runout_cards.size(); ++river_index) {
        const uint8_t river = runout_cards[river_index];
        if (hand.Contains(river)) {
          continue;
        }
        cards[4] = river;
        strengths[static_cast<std::size_t>(hand.raw_index)]
                 [static_cast<std::size_t>(turn) * GameBasic::kDeckSize +
                  river] = evaluator.Evaluate7(cards.data());
      }
    }
  }
  return strengths;
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
  BuildEquityDelta();
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
  ValidateIsoIndex(hero_iso);
  ValidateIsoIndex(opponent_iso);
  return equity_delta_[static_cast<std::size_t>(hero_iso * num_iso_hands_ +
                                               opponent_iso)];
}

const PokerCards& TerminalWinProbMatrix::Board() const { return board_; }

const std::vector<float>& TerminalWinProbMatrix::WinProbData() const {
  return win_prob_;
}

const std::vector<float>& TerminalWinProbMatrix::EquityDeltaData() const {
  return equity_delta_;
}

void TerminalWinProbMatrix::Build(
    const GameBasic& game_basic, const IsomorphicMapping& mapping,
    const SevenCardLookupTable& evaluator) {
  const std::vector<RawHandEntry> hands =
      BuildRawHandEntries(game_basic, mapping);
  const std::vector<uint8_t> runout_cards = RemainingBoardCards(board_);

  if (round_ == PokerRound::kTurn) {
    const auto strengths =
        BuildTurnStrengths(board_, hands, runout_cards, evaluator);
    for (const RawHandEntry& hero : hands) {
      for (const RawHandEntry& opponent : hands) {
        if (hero.CollidesWith(opponent)) {
          continue;
        }
        int wins = 0;
        int runouts = 0;
        const auto& hero_strengths =
            strengths[static_cast<std::size_t>(hero.raw_index)];
        const auto& opponent_strengths =
            strengths[static_cast<std::size_t>(opponent.raw_index)];
        for (uint8_t river : runout_cards) {
          if (hero.Contains(river) || opponent.Contains(river)) {
            continue;
          }
          const uint16_t hero_strength =
              hero_strengths[static_cast<std::size_t>(river)];
          const uint16_t opponent_strength =
              opponent_strengths[static_cast<std::size_t>(river)];
          if (hero_strength == kInvalidStrength ||
              opponent_strength == kInvalidStrength) {
            throw std::runtime_error("Missing turn hand strength");
          }
          wins += hero_strength > opponent_strength;
          ++runouts;
        }
        if (runouts <= 0) {
          throw std::runtime_error("Turn equity pair has no runouts");
        }
        win_prob_[static_cast<std::size_t>(hero.iso_index * num_iso_hands_ +
                                          opponent.iso_index)] +=
            (static_cast<float>(wins) / static_cast<float>(runouts)) *
            hero.normalization * opponent.normalization;
      }
    }
    return;
  }

  const auto strengths =
      BuildFlopStrengths(board_, hands, runout_cards, evaluator);
  for (const RawHandEntry& hero : hands) {
    for (const RawHandEntry& opponent : hands) {
      if (hero.CollidesWith(opponent)) {
        continue;
      }
      int wins = 0;
      int runouts = 0;
      const auto& hero_strengths =
          strengths[static_cast<std::size_t>(hero.raw_index)];
      const auto& opponent_strengths =
          strengths[static_cast<std::size_t>(opponent.raw_index)];
      for (std::size_t turn_index = 0; turn_index < runout_cards.size();
           ++turn_index) {
        const uint8_t turn = runout_cards[turn_index];
        if (hero.Contains(turn) || opponent.Contains(turn)) {
          continue;
        }
        for (std::size_t river_index = turn_index + 1;
             river_index < runout_cards.size(); ++river_index) {
          const uint8_t river = runout_cards[river_index];
          if (hero.Contains(river) || opponent.Contains(river)) {
            continue;
          }
          const std::size_t runout_index =
              static_cast<std::size_t>(turn) * GameBasic::kDeckSize + river;
          const uint16_t hero_strength = hero_strengths[runout_index];
          const uint16_t opponent_strength = opponent_strengths[runout_index];
          if (hero_strength == kInvalidStrength ||
              opponent_strength == kInvalidStrength) {
            throw std::runtime_error("Missing flop hand strength");
          }
          wins += hero_strength > opponent_strength;
          ++runouts;
        }
      }
      if (runouts <= 0) {
        throw std::runtime_error("Flop equity pair has no runouts");
      }
      win_prob_[static_cast<std::size_t>(hero.iso_index * num_iso_hands_ +
                                        opponent.iso_index)] +=
          (static_cast<float>(wins) / static_cast<float>(runouts)) *
          hero.normalization * opponent.normalization;
    }
  }
}

void TerminalWinProbMatrix::BuildEquityDelta() {
  equity_delta_.assign(
      static_cast<std::size_t>(num_iso_hands_ * num_iso_hands_), 0.0f);
  for (int hero_iso = 0; hero_iso < num_iso_hands_; ++hero_iso) {
    for (int opponent_iso = 0; opponent_iso < num_iso_hands_; ++opponent_iso) {
      const std::size_t index =
          static_cast<std::size_t>(hero_iso * num_iso_hands_ + opponent_iso);
      const std::size_t transpose_index =
          static_cast<std::size_t>(opponent_iso * num_iso_hands_ + hero_iso);
      equity_delta_[index] = win_prob_[index] - win_prob_[transpose_index];
    }
  }
}

void TerminalWinProbMatrix::ValidateIsoIndex(int iso_index) const {
  if (iso_index < 0 || iso_index >= num_iso_hands_) {
    throw std::invalid_argument(
        "TerminalWinProbMatrix iso index is out of range");
  }
}

}  // namespace fisher::game::poker
