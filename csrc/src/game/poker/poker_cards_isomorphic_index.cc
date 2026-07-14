#include "game/poker/poker_cards_isomorphic_index.h"

#include <array>
#include <stdexcept>

namespace fisher::game::poker {
namespace {

constexpr int kNumRounds = 4;
constexpr int kNumSuits = 4;

int RoundIndex(PokerRound round) {
  const int index = static_cast<int>(round);
  if (index < 0 || index >= kNumRounds) {
    throw std::invalid_argument("Invalid poker round");
  }
  return index;
}

int BoardIndexerRound(PokerRound round) {
  switch (round) {
    case PokerRound::kFlop:
      return 0;
    case PokerRound::kTurn:
      return 1;
    case PokerRound::kRiver:
      return 2;
    case PokerRound::kPreflop:
      break;
  }
  throw std::invalid_argument("Board indexer does not support preflop");
}

int HoleBoardIndexerRound(PokerRound round) {
  return RoundIndex(round);
}

void InitIndexer(const std::vector<uint8_t>& cards_per_round,
                 hand_indexer_t* indexer) {
  if (!hand_indexer_init(cards_per_round.size(), cards_per_round.data(),
                         indexer)) {
    throw std::runtime_error("Failed to initialize hand indexer");
  }
}

std::vector<uint8_t> ToCardIds(const PokerCards& cards) {
  std::vector<uint8_t> ids;
  ids.reserve(cards.Size());
  for (PokerCard card : cards.Cards()) {
    ids.push_back(card.Value());
  }
  return ids;
}

PokerCard ApplySuitMap(PokerCard card, const std::array<int, kNumSuits>& map) {
  const int rank = static_cast<int>(card.Rank());
  const int suit = static_cast<int>(card.Suit());
  if (map[suit] < 0 || map[suit] >= kNumSuits) {
    throw std::runtime_error("Incomplete suit map");
  }
  return PokerCard(static_cast<uint8_t>(rank * kNumSuits + map[suit]));
}

bool TryBuildSuitMapFromCards(const std::vector<PokerCard>& from_cards,
                              const std::vector<PokerCard>& to_cards,
                              std::size_t from_index,
                              std::vector<bool>* used_to_cards,
                              std::array<int, kNumSuits>* suit_map,
                              std::array<bool, kNumSuits>* used_targets) {
  if (from_index == from_cards.size()) {
    return true;
  }

  const PokerCard from_card = from_cards[from_index];
  const int from_suit = static_cast<int>(from_card.Suit());
  for (std::size_t to_index = 0; to_index < to_cards.size(); ++to_index) {
    if ((*used_to_cards)[to_index] ||
        from_card.Rank() != to_cards[to_index].Rank()) {
      continue;
    }

    const int to_suit = static_cast<int>(to_cards[to_index].Suit());
    if ((*suit_map)[from_suit] != -1 && (*suit_map)[from_suit] != to_suit) {
      continue;
    }
    if ((*suit_map)[from_suit] == -1 && (*used_targets)[to_suit]) {
      continue;
    }

    const int previous_target = (*suit_map)[from_suit];
    const bool assigned_new_target = previous_target == -1;
    (*suit_map)[from_suit] = to_suit;
    (*used_targets)[to_suit] = true;
    (*used_to_cards)[to_index] = true;

    if (TryBuildSuitMapFromCards(from_cards, to_cards, from_index + 1,
                                 used_to_cards, suit_map, used_targets)) {
      return true;
    }

    (*used_to_cards)[to_index] = false;
    if (assigned_new_target) {
      (*suit_map)[from_suit] = -1;
      (*used_targets)[to_suit] = false;
    }
  }
  return false;
}

std::array<int, kNumSuits> BuildSuitMap(
    const std::vector<PokerCard>& from_prefix,
    const std::vector<PokerCard>& to_prefix) {
  if (from_prefix.size() != to_prefix.size()) {
    throw std::runtime_error("Suit map prefix size mismatch");
  }

  std::array<int, kNumSuits> suit_map;
  suit_map.fill(-1);
  std::array<bool, kNumSuits> used_targets{};
  std::vector<bool> used_to_cards(to_prefix.size(), false);
  if (!TryBuildSuitMapFromCards(from_prefix, to_prefix, 0, &used_to_cards,
                                &suit_map, &used_targets)) {
    throw std::runtime_error("Cannot build suit representative map");
  }

  for (int suit = 0; suit < kNumSuits; ++suit) {
    if (suit_map[suit] == -1 && !used_targets[suit]) {
      suit_map[suit] = suit;
      used_targets[suit] = true;
    }
  }

  for (int suit = 0; suit < kNumSuits; ++suit) {
    if (suit_map[suit] != -1) continue;
    for (int target = 0; target < kNumSuits; ++target) {
      if (!used_targets[target]) {
        suit_map[suit] = target;
        used_targets[target] = true;
        break;
      }
    }
  }

  return suit_map;
}

std::vector<PokerCard> AlignToPreviousPrefix(
    const std::vector<PokerCard>& previous_representative,
    const std::vector<PokerCard>& current_representative) {
  if (previous_representative.size() >= current_representative.size()) {
    throw std::runtime_error("Previous representative must be a strict prefix");
  }

  std::vector<PokerCard> current_prefix(
      current_representative.begin(),
      current_representative.begin() +
          static_cast<std::ptrdiff_t>(previous_representative.size()));
  const std::array<int, kNumSuits> suit_map =
      BuildSuitMap(current_prefix, previous_representative);

  std::vector<PokerCard> aligned;
  aligned.reserve(current_representative.size());
  for (PokerCard card : current_representative) {
    aligned.push_back(ApplySuitMap(card, suit_map));
  }
  return aligned;
}

void ValidateHoleBoard(const PokerCards& hole_cards, const PokerCards& board) {
  if (hole_cards.Size() != 2) {
    throw std::invalid_argument(
        "Hold'em hole cards must contain exactly 2 cards");
  }
  RoundFromBoardSize(board.Size());
  if (hole_cards.HasCollision(board)) {
    throw std::invalid_argument("Hole cards and board cards collide");
  }
}

void FreeInitialized(std::array<hand_indexer_t, 4>* indexers,
                     std::array<bool, 4>* initialized) {
  for (int index = 0; index < kNumRounds; ++index) {
    if ((*initialized)[index]) {
      hand_indexer_free(&(*indexers)[index]);
      (*initialized)[index] = false;
    }
  }
}

}  // namespace

int BoardCardsForRound(PokerRound round) {
  switch (round) {
    case PokerRound::kPreflop:
      return 0;
    case PokerRound::kFlop:
      return 3;
    case PokerRound::kTurn:
      return 4;
    case PokerRound::kRiver:
      return 5;
  }
  throw std::invalid_argument("Invalid poker round");
}

PokerRound RoundFromBoardSize(std::size_t board_cards) {
  switch (board_cards) {
    case 0:
      return PokerRound::kPreflop;
    case 3:
      return PokerRound::kFlop;
    case 4:
      return PokerRound::kTurn;
    case 5:
      return PokerRound::kRiver;
    default:
      throw std::invalid_argument("Unsupported board card count");
  }
}

PokerCardsIsomorphicBoardIndex::PokerCardsIsomorphicBoardIndex() {
  initialized_.fill(false);
  try {
    InitIndexer({3}, &indexers_[RoundIndex(PokerRound::kFlop)]);
    initialized_[RoundIndex(PokerRound::kFlop)] = true;
    InitIndexer({3, 1}, &indexers_[RoundIndex(PokerRound::kTurn)]);
    initialized_[RoundIndex(PokerRound::kTurn)] = true;
    InitIndexer({3, 1, 1}, &indexers_[RoundIndex(PokerRound::kRiver)]);
    initialized_[RoundIndex(PokerRound::kRiver)] = true;
  } catch (...) {
    FreeInitialized(&indexers_, &initialized_);
    throw;
  }
}

PokerCardsIsomorphicBoardIndex::~PokerCardsIsomorphicBoardIndex() {
  FreeInitialized(&indexers_, &initialized_);
}

hand_index_t PokerCardsIsomorphicBoardIndex::GetIndex(
    const PokerCards& board) const {
  const PokerRound round = RoundFromBoardSize(board.Size());
  if (round == PokerRound::kPreflop) {
    throw std::invalid_argument("Board index requires 3, 4, or 5 board cards");
  }

  const std::vector<uint8_t> cards = ToCardIds(board);
  return hand_index_last(&indexers_[RoundIndex(round)], cards.data());
}

PokerCards PokerCardsIsomorphicBoardIndex::GetBoard(
    PokerRound round, hand_index_t index) const {
  if (round == PokerRound::kPreflop) {
    throw std::invalid_argument("Board indexer does not support preflop");
  }

  std::vector<PokerCard> raw = UnindexRaw(round, index);
  if (round == PokerRound::kFlop) {
    return PokerCards(raw);
  }

  const PokerRound previous_round =
      round == PokerRound::kTurn ? PokerRound::kFlop : PokerRound::kTurn;
  const int previous_cards = BoardCardsForRound(previous_round);
  std::vector<PokerCard> raw_previous(
      raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(previous_cards));
  const hand_index_t previous_index = GetIndex(PokerCards(raw_previous));
  const PokerCards previous_representative =
      GetBoard(previous_round, previous_index);
  return PokerCards(AlignToPreviousPrefix(previous_representative.Cards(), raw));
}

PokerCards PokerCardsIsomorphicBoardIndex::GetNewBoardCards(
    const PokerCards& previous_board, hand_index_t next_round_index) const {
  const PokerRound previous_round = RoundFromBoardSize(previous_board.Size());
  PokerRound next_round;
  switch (previous_round) {
    case PokerRound::kFlop:
      next_round = PokerRound::kTurn;
      break;
    case PokerRound::kTurn:
      next_round = PokerRound::kRiver;
      break;
    case PokerRound::kPreflop:
    case PokerRound::kRiver:
      throw std::invalid_argument("No next board round for given board");
  }

  std::vector<PokerCard> raw = UnindexRaw(next_round, next_round_index);
  return PokerCards(AlignToPreviousPrefix(previous_board.Cards(), raw));
}

hand_index_t PokerCardsIsomorphicBoardIndex::RoundSize(PokerRound round) const {
  if (round == PokerRound::kPreflop) {
    throw std::invalid_argument("Board indexer does not support preflop");
  }
  return hand_indexer_size(&indexers_[RoundIndex(round)],
                           BoardIndexerRound(round));
}

std::vector<PokerCard> PokerCardsIsomorphicBoardIndex::UnindexRaw(
    PokerRound round, hand_index_t index) const {
  if (index >= RoundSize(round)) {
    throw std::invalid_argument("Board index is out of range");
  }

  std::vector<uint8_t> card_ids(static_cast<std::size_t>(
      BoardCardsForRound(round)));
  if (!hand_unindex(&indexers_[RoundIndex(round)], BoardIndexerRound(round),
                    index, card_ids.data())) {
    throw std::runtime_error("Failed to unindex board cards");
  }

  std::vector<PokerCard> cards;
  cards.reserve(card_ids.size());
  for (uint8_t card_id : card_ids) {
    cards.emplace_back(card_id);
  }
  return cards;
}

PokerCardsIsomorphicHoleBoardIndex::PokerCardsIsomorphicHoleBoardIndex() {
  initialized_.fill(false);
  try {
    InitIndexer({2}, &indexers_[RoundIndex(PokerRound::kPreflop)]);
    initialized_[RoundIndex(PokerRound::kPreflop)] = true;
    InitIndexer({3, 2}, &indexers_[RoundIndex(PokerRound::kFlop)]);
    initialized_[RoundIndex(PokerRound::kFlop)] = true;
    InitIndexer({3, 1, 2}, &indexers_[RoundIndex(PokerRound::kTurn)]);
    initialized_[RoundIndex(PokerRound::kTurn)] = true;
    InitIndexer({3, 1, 1, 2}, &indexers_[RoundIndex(PokerRound::kRiver)]);
    initialized_[RoundIndex(PokerRound::kRiver)] = true;
  } catch (...) {
    FreeInitialized(&indexers_, &initialized_);
    throw;
  }
}

PokerCardsIsomorphicHoleBoardIndex::~PokerCardsIsomorphicHoleBoardIndex() {
  FreeInitialized(&indexers_, &initialized_);
}

hand_index_t PokerCardsIsomorphicHoleBoardIndex::GetIndex(
    const PokerCards& hole_cards, const PokerCards& board) const {
  const PokerRound round = RoundFromBoardSize(board.Size());
  ValidateHoleBoard(hole_cards, board);

  std::vector<uint8_t> cards = ToCardIds(board);
  std::vector<uint8_t> hole_ids = ToCardIds(PokerHand(hole_cards).ToPokerCards());
  cards.insert(cards.end(), hole_ids.begin(), hole_ids.end());
  return hand_index_last(&indexers_[RoundIndex(round)], cards.data());
}

hand_index_t PokerCardsIsomorphicHoleBoardIndex::GetIndex(
    const PokerHand& hand, const PokerCards& board) const {
  return GetIndex(hand.ToPokerCards(), board);
}

HoleBoardCards PokerCardsIsomorphicHoleBoardIndex::GetCards(
    PokerRound round, hand_index_t index) const {
  std::vector<PokerCard> raw = UnindexRaw(round, index);
  const int board_cards = BoardCardsForRound(round);
  if (static_cast<int>(raw.size()) != board_cards + 2) {
    throw std::runtime_error("Unindexed cards do not match poker round");
  }

  std::vector<PokerCard> board(raw.begin(), raw.begin() + board_cards);
  std::vector<PokerCard> hole(raw.begin() + board_cards, raw.end());
  return HoleBoardCards{PokerCards(hole), PokerCards(board)};
}

hand_index_t PokerCardsIsomorphicHoleBoardIndex::RoundSize(
    PokerRound round) const {
  return hand_indexer_size(&indexers_[RoundIndex(round)],
                           HoleBoardIndexerRound(round));
}

std::vector<PokerCard> PokerCardsIsomorphicHoleBoardIndex::UnindexRaw(
    PokerRound round, hand_index_t index) const {
  if (index >= RoundSize(round)) {
    throw std::invalid_argument("Hole-board index is out of range");
  }

  std::vector<uint8_t> card_ids(
      static_cast<std::size_t>(BoardCardsForRound(round) + 2));
  if (!hand_unindex(&indexers_[RoundIndex(round)],
                    HoleBoardIndexerRound(round), index, card_ids.data())) {
    throw std::runtime_error("Failed to unindex hole-board cards");
  }

  std::vector<PokerCard> cards;
  cards.reserve(card_ids.size());
  for (uint8_t card_id : card_ids) {
    cards.emplace_back(card_id);
  }
  return cards;
}

}  // namespace fisher::game::poker
