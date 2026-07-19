#include "game/poker/isomorphic_mapping.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace fisher::game::poker {
namespace {

constexpr int kNumSuits = 4;

bool TryBuildSuitMapFromCards(const std::vector<PokerCard>& raw_cards,
                              const std::vector<PokerCard>& iso_cards,
                              std::size_t raw_index,
                              std::vector<bool>* used_iso_cards,
                              std::array<int, kNumSuits>* suit_map,
                              std::array<bool, kNumSuits>* used_targets) {
  if (raw_index == raw_cards.size()) {
    return true;
  }

  const PokerCard raw_card = raw_cards[raw_index];
  const int raw_suit = static_cast<int>(raw_card.Suit());
  for (std::size_t iso_index = 0; iso_index < iso_cards.size(); ++iso_index) {
    if ((*used_iso_cards)[iso_index] ||
        raw_card.Rank() != iso_cards[iso_index].Rank()) {
      continue;
    }

    const int iso_suit = static_cast<int>(iso_cards[iso_index].Suit());
    if ((*suit_map)[raw_suit] != -1 && (*suit_map)[raw_suit] != iso_suit) {
      continue;
    }
    if ((*suit_map)[raw_suit] == -1 && (*used_targets)[iso_suit]) {
      continue;
    }

    const int previous_target = (*suit_map)[raw_suit];
    const bool assigned_new_target = previous_target == -1;
    (*suit_map)[raw_suit] = iso_suit;
    (*used_targets)[iso_suit] = true;
    (*used_iso_cards)[iso_index] = true;

    if (TryBuildSuitMapFromCards(raw_cards, iso_cards, raw_index + 1,
                                 used_iso_cards, suit_map, used_targets)) {
      return true;
    }

    (*used_iso_cards)[iso_index] = false;
    if (assigned_new_target) {
      (*suit_map)[raw_suit] = -1;
      (*used_targets)[iso_suit] = false;
    }
  }
  return false;
}

std::array<int, kNumSuits> BuildSuitMap(const PokerCards& raw_board,
                                        const PokerCards& iso_board) {
  if (raw_board.Size() != iso_board.Size()) {
    throw std::runtime_error("Board suit map size mismatch");
  }

  std::array<int, kNumSuits> suit_map;
  suit_map.fill(-1);
  std::array<bool, kNumSuits> used_targets{};
  std::vector<bool> used_iso_cards(iso_board.Size(), false);
  if (!TryBuildSuitMapFromCards(raw_board.Cards(), iso_board.Cards(), 0,
                                &used_iso_cards, &suit_map,
                                &used_targets)) {
    throw std::runtime_error("Cannot build board suit map");
  }

  for (int suit = 0; suit < kNumSuits; ++suit) {
    if (suit_map[suit] == -1 && !used_targets[suit]) {
      suit_map[suit] = suit;
      used_targets[suit] = true;
    }
  }

  for (int suit = 0; suit < kNumSuits; ++suit) {
    if (suit_map[suit] != -1) {
      continue;
    }
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

std::string BoardKey(const PokerCards& board) { return board.ToString(); }

}  // namespace

IsomorphicMapping::IsomorphicMapping(
    const GameBasic& game_basic, const PokerCards& board,
    const std::vector<bool>& root_possible_hands)
    : raw_board_(board),
      round_(game_basic.BoardRound(board)),
      raw_index_to_iso_index_(GameBasic::kNumHands, kInvalidIsoIndex) {
  if (round_ == PokerRound::kPreflop) {
    throw std::invalid_argument("IsomorphicMapping only supports postflop");
  }
  if (root_possible_hands.size() !=
      static_cast<std::size_t>(GameBasic::kNumHands)) {
    throw std::invalid_argument(
        "Root possible hand mask must contain 1326 entries");
  }

  const hand_index_t board_index = game_basic.BoardIsomorphicIndex(board);
  iso_board_ = game_basic.BoardIndexer().GetBoard(round_, board_index);
  suit_mapping_ = BuildSuitMap(raw_board_, iso_board_);

  std::vector<hand_index_t> raw_to_sparse_iso(GameBasic::kNumHands, 0);
  std::vector<bool> raw_is_valid(GameBasic::kNumHands, false);
  std::vector<hand_index_t> sparse_indices;
  sparse_indices.reserve(static_cast<std::size_t>(GameBasic::kNumHands));
  std::unordered_map<hand_index_t, int> total_count_by_sparse_iso;
  std::unordered_map<hand_index_t, int> active_count_by_sparse_iso;

  for (int raw_index = 0; raw_index < GameBasic::kNumHands; ++raw_index) {
    const PokerHand& hand = game_basic.HandFromIndex(raw_index);
    if (hand.HasCollision(board)) {
      continue;
    }

    const hand_index_t sparse_iso =
        game_basic.HoleBoardIsomorphicIndex(hand, board);
    ++total_count_by_sparse_iso[sparse_iso];
    if (!root_possible_hands[static_cast<std::size_t>(raw_index)]) {
      continue;
    }
    ++active_count_by_sparse_iso[sparse_iso];
    raw_to_sparse_iso[static_cast<std::size_t>(raw_index)] = sparse_iso;
    raw_is_valid[static_cast<std::size_t>(raw_index)] = true;
    sparse_indices.push_back(sparse_iso);
  }

  for (const auto& [sparse_iso, total_count] : total_count_by_sparse_iso) {
    const auto active_it = active_count_by_sparse_iso.find(sparse_iso);
    const int active_count =
        active_it == active_count_by_sparse_iso.end() ? 0 : active_it->second;
    if (active_count > 0 && active_count != total_count) {
      throw std::invalid_argument(
          "Root possible hands must be closed over isomorphic hand buckets");
    }
  }

  std::sort(sparse_indices.begin(), sparse_indices.end());
  sparse_indices.erase(std::unique(sparse_indices.begin(), sparse_indices.end()),
                       sparse_indices.end());
  if (sparse_indices.empty()) {
    throw std::invalid_argument("IsomorphicMapping has no valid iso hands");
  }

  std::unordered_map<hand_index_t, int> sparse_to_compact;
  sparse_to_compact.reserve(sparse_indices.size());
  for (std::size_t index = 0; index < sparse_indices.size(); ++index) {
    sparse_to_compact.emplace(sparse_indices[index], static_cast<int>(index));
  }

  iso_index_to_raw_index_.assign(sparse_indices.size(), kInvalidIsoIndex);
  raw_hand_count_for_each_iso_hand_.assign(sparse_indices.size(), 0);

  for (int raw_index = 0; raw_index < GameBasic::kNumHands; ++raw_index) {
    if (!raw_is_valid[static_cast<std::size_t>(raw_index)]) {
      continue;
    }
    const hand_index_t sparse_iso =
        raw_to_sparse_iso[static_cast<std::size_t>(raw_index)];
    const int compact_iso = sparse_to_compact.at(sparse_iso);
    raw_index_to_iso_index_[static_cast<std::size_t>(raw_index)] =
        compact_iso;
    ++raw_hand_count_for_each_iso_hand_[static_cast<std::size_t>(compact_iso)];
    if (iso_index_to_raw_index_[static_cast<std::size_t>(compact_iso)] ==
        kInvalidIsoIndex) {
      iso_index_to_raw_index_[static_cast<std::size_t>(compact_iso)] =
          raw_index;
    }
  }

  for (std::size_t sparse_index = 0; sparse_index < sparse_indices.size();
       ++sparse_index) {
    const int compact_iso = static_cast<int>(sparse_index);
    const HoleBoardCards representative =
        game_basic.HoleBoardIndexer().GetCards(round_,
                                               sparse_indices[sparse_index]);
    int representative_raw = kInvalidIsoIndex;
    try {
      representative_raw = game_basic.HandIndex(representative.hole_cards);
    } catch (const std::exception&) {
      representative_raw = kInvalidIsoIndex;
    }
    if (representative_raw >= 0 &&
        representative_raw < GameBasic::kNumHands &&
        raw_index_to_iso_index_[static_cast<std::size_t>(representative_raw)] ==
            compact_iso) {
      iso_index_to_raw_index_[static_cast<std::size_t>(compact_iso)] =
          representative_raw;
    }
  }

  for (int iso_index = 0; iso_index < NumIsoHands(); ++iso_index) {
    if (iso_index_to_raw_index_[static_cast<std::size_t>(iso_index)] < 0 ||
        raw_hand_count_for_each_iso_hand_[static_cast<std::size_t>(iso_index)] <=
            0) {
      throw std::runtime_error("Invalid compact iso bucket");
    }
  }
}

const PokerCards& IsomorphicMapping::RawBoard() const { return raw_board_; }

const PokerCards& IsomorphicMapping::IsoBoard() const { return iso_board_; }

PokerRound IsomorphicMapping::Round() const { return round_; }

const std::array<int, 4>& IsomorphicMapping::SuitMapping() const {
  return suit_mapping_;
}

int IsomorphicMapping::NumIsoHands() const {
  return static_cast<int>(iso_index_to_raw_index_.size());
}

int IsomorphicMapping::RawToIso(int raw_hand_index) const {
  ValidateRawHandIndex(raw_hand_index);
  return raw_index_to_iso_index_[static_cast<std::size_t>(raw_hand_index)];
}

int IsomorphicMapping::IsoToRepresentativeRaw(int iso_index) const {
  ValidateIsoIndex(iso_index);
  return iso_index_to_raw_index_[static_cast<std::size_t>(iso_index)];
}

int IsomorphicMapping::RawHandCount(int iso_index) const {
  ValidateIsoIndex(iso_index);
  return raw_hand_count_for_each_iso_hand_[static_cast<std::size_t>(iso_index)];
}

const std::vector<int>& IsomorphicMapping::RawIndexToIsoIndex() const {
  return raw_index_to_iso_index_;
}

const std::vector<int>& IsomorphicMapping::IsoIndexToRawIndex() const {
  return iso_index_to_raw_index_;
}

const std::vector<int>& IsomorphicMapping::RawHandCountForEachIsoHand() const {
  return raw_hand_count_for_each_iso_hand_;
}

void IsomorphicMapping::ValidateRawHandIndex(int raw_hand_index) const {
  if (raw_hand_index < 0 || raw_hand_index >= GameBasic::kNumHands) {
    throw std::invalid_argument("Raw hand index is out of range");
  }
}

void IsomorphicMapping::ValidateIsoIndex(int iso_index) const {
  if (iso_index < 0 || iso_index >= NumIsoHands()) {
    throw std::invalid_argument("Iso hand index is out of range");
  }
}

IsomorphicMappingTable::IsomorphicMappingTable(GameBasic game_basic,
                                               const PokerBelief& root_belief)
    : game_basic_(std::move(game_basic)),
      root_possible_hands_(BuildRootPossibleHands(root_belief)) {}

const IsomorphicMapping& IsomorphicMappingTable::Get(const PokerCards& board) {
  const std::string key = BoardKey(board);
  auto iterator = mappings_.find(key);
  if (iterator != mappings_.end()) {
    return *iterator->second;
  }

  auto mapping = std::make_unique<IsomorphicMapping>(
      game_basic_, board, root_possible_hands_);
  const IsomorphicMapping* mapping_pointer = mapping.get();
  mappings_.emplace(key, std::move(mapping));
  return *mapping_pointer;
}

bool IsomorphicMappingTable::Contains(const PokerCards& board) const {
  return mappings_.find(BoardKey(board)) != mappings_.end();
}

std::vector<bool> IsomorphicMappingTable::BuildRootPossibleHands(
    const PokerBelief& root_belief) {
  const std::vector<std::vector<float>>& belief = root_belief.Belief();
  if (belief.size() != GameBasic::kNumPlayers) {
    throw std::invalid_argument("Root belief must contain 2 players");
  }

  std::vector<bool> possible(GameBasic::kNumHands, true);
  for (int hand_index = 0; hand_index < GameBasic::kNumHands; ++hand_index) {
    bool any_player_possible = false;
    for (int player = 0; player < GameBasic::kNumPlayers; ++player) {
      const std::vector<float>& player_belief =
          belief[static_cast<std::size_t>(player)];
      if (player_belief.size() !=
          static_cast<std::size_t>(GameBasic::kNumHands)) {
        throw std::invalid_argument("Root belief must contain 1326 hands");
      }
      if (player_belief[static_cast<std::size_t>(hand_index)] > 0.0f) {
        any_player_possible = true;
      }
    }
    possible[static_cast<std::size_t>(hand_index)] = any_player_possible;
  }
  return possible;
}

}  // namespace fisher::game::poker
