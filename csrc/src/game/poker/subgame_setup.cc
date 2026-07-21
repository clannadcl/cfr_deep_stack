#include "game/poker/subgame_setup.h"

#include <stdexcept>
#include <utility>

#include "game/poker/node_state.h"

namespace fisher::game::poker {
namespace {

void ValidatePlayerIndex(int player, const char* name) {
  if (player < 0 || player >= GameBasic::kNumPlayers) {
    throw std::invalid_argument(name);
  }
}

void ValidateRootActor(int player) {
  if (player != NodeState::kChancePlayer &&
      (player < 0 || player >= GameBasic::kNumPlayers)) {
    throw std::invalid_argument("Current player must be -1, 0, or 1");
  }
}

void ValidateRootBeliefShape(
    const std::vector<std::vector<float>>& root_belief) {
  if (root_belief.size() != GameBasic::kNumPlayers) {
    throw std::invalid_argument("Root belief must contain 2 players");
  }
  for (const std::vector<float>& player_belief : root_belief) {
    if (player_belief.size() != GameBasic::kNumHands) {
      throw std::invalid_argument("Root belief must contain 1326 hands");
    }
  }
}

}  // namespace

SubgameSetup::Args::Args(PokerCards board, float pot,
                         std::array<float, 2> stacks,
                         std::array<float, 2> bet_total,
                         std::array<float, 2> bet_current_round,
                         int current_player, int last_aggressor,
                         int raise_count,
                         std::vector<Action> root_action_history,
                         RootBeliefInput root_belief,
                         TreeAbstractedBets abstracted_bets,
                         GameBasic game_basic, float bet_rounding,
                         float min_raise_size)
    : board(std::move(board)),
      pot(pot),
      stacks(stacks),
      bet_total(bet_total),
      bet_current_round(bet_current_round),
      current_player(current_player),
      last_aggressor(last_aggressor),
      raise_count(raise_count),
      root_action_history(std::move(root_action_history)),
      root_belief(std::move(root_belief)),
      abstracted_bets(std::move(abstracted_bets)),
      game_basic(std::move(game_basic)),
      bet_rounding(bet_rounding),
      min_raise_size(min_raise_size) {}

SubgameSetup::SubgameSetup(const Args& args)
    : board_(args.board),
      game_basic_(args.game_basic),
      street_(game_basic_.BoardRound(board_)),
      pot_(MoneyToMilliBb(args.pot)),
      stacks_(MoneyArrayToMilliBb(args.stacks)),
      bet_total_(MoneyArrayToMilliBb(args.bet_total)),
      bet_current_round_(MoneyArrayToMilliBb(args.bet_current_round)),
      current_player_(args.current_player),
      last_aggressor_(args.last_aggressor),
      raise_count_(args.raise_count),
      root_action_history_(args.root_action_history),
      abstracted_bets_(args.abstracted_bets),
      bet_rounding_(args.bet_rounding),
      min_raise_size_(args.min_raise_size),
      root_belief_(BuildRootBelief(args.root_belief, board_, game_basic_)) {
  if (pot_ < 0) {
    throw std::invalid_argument("Pot cannot be negative");
  }
  for (MoneyMilliBb stack : stacks_) {
    if (stack < 0) {
      throw std::invalid_argument("Stack cannot be negative");
    }
  }
  for (MoneyMilliBb bet : bet_current_round_) {
    if (bet < 0) {
      throw std::invalid_argument("Current-round bet cannot be negative");
    }
  }
  for (int player = 0; player < GameBasic::kNumPlayers; ++player) {
    const std::size_t index = static_cast<std::size_t>(player);
    if (bet_total_[index] < 0) {
      throw std::invalid_argument("Total bet cannot be negative");
    }
    if (bet_total_[index] < bet_current_round_[index]) {
      throw std::invalid_argument(
          "Total bet cannot be smaller than current-round bet");
    }
  }
  ValidateRootActor(current_player_);
  if (last_aggressor_ != -1) {
    ValidatePlayerIndex(last_aggressor_, "Last aggressor must be -1, 0, or 1");
  }
  if (raise_count_ < 0) {
    throw std::invalid_argument("Raise count cannot be negative");
  }
  if (bet_rounding_ <= 0.0f) {
    throw std::invalid_argument("Bet rounding must be positive");
  }
  if (min_raise_size_ <= 0.0f) {
    throw std::invalid_argument("Min raise size must be positive");
  }
  if (current_player_ == NodeState::kChancePlayer) {
    if (street_ != PokerRound::kFlop && street_ != PokerRound::kTurn) {
      throw std::invalid_argument(
          "Chance root must be on flop or turn before the next board card");
    }
    if (bet_current_round_[0] != 0 || bet_current_round_[1] != 0) {
      throw std::invalid_argument(
          "Chance root cannot have live current-round bets");
    }
    if (raise_count_ != 0) {
      throw std::invalid_argument("Chance root raise count must be zero");
    }
  }
}

const PokerCards& SubgameSetup::Board() const { return board_; }

PokerRound SubgameSetup::Street() const { return street_; }

float SubgameSetup::Pot() const { return MilliBbToMoney(pot_); }

std::array<float, 2> SubgameSetup::Stacks() const {
  return MilliBbArrayToMoney(stacks_);
}

std::array<float, 2> SubgameSetup::BetTotal() const {
  return MilliBbArrayToMoney(bet_total_);
}

std::array<float, 2> SubgameSetup::BetCurrentRound() const {
  return MilliBbArrayToMoney(bet_current_round_);
}

int SubgameSetup::CurrentPlayer() const { return current_player_; }

int SubgameSetup::LastAggressor() const { return last_aggressor_; }

int SubgameSetup::RaiseCount() const { return raise_count_; }

const std::vector<Action>& SubgameSetup::RootActionHistory() const {
  return root_action_history_;
}

const PokerBelief& SubgameSetup::RootBelief() const { return root_belief_; }

const TreeAbstractedBets& SubgameSetup::AbstractedBets() const {
  return abstracted_bets_;
}

const GameBasic& SubgameSetup::BasicGame() const { return game_basic_; }

float SubgameSetup::BetRounding() const { return bet_rounding_; }

float SubgameSetup::MinRaiseSize() const { return min_raise_size_; }

NodeState SubgameSetup::GetRootNodeState() const {
  return NodeState(NodeState::Args(
      shared_from_this(), board_, Pot(), Stacks(), BetTotal(),
      BetCurrentRound(), current_player_, last_aggressor_, raise_count_,
      std::array<bool, 2>{false, false}, TerminalStatus::kNotTerminal,
      root_action_history_));
}

PokerBelief SubgameSetup::BuildRootBelief(const RootBeliefInput& input,
                                          const PokerCards& board,
                                          const GameBasic& game_basic) {
  PokerBelief belief = std::visit(
      [](const auto& value) { return PokerBelief(value); }, input);
  std::vector<std::vector<float>> blocked_belief = belief.Belief();
  ValidateRootBeliefShape(blocked_belief);

  for (int hand_index = 0; hand_index < GameBasic::kNumHands; ++hand_index) {
    if (!game_basic.HandFromIndex(hand_index).HasCollision(board)) {
      continue;
    }
    for (std::vector<float>& player_belief : blocked_belief) {
      player_belief[static_cast<std::size_t>(hand_index)] = 0.0f;
    }
  }
  return PokerBelief(blocked_belief);
}

}  // namespace fisher::game::poker
