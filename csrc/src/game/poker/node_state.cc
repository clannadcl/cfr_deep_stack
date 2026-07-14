#include "game/poker/node_state.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "game/poker/action_resolver.h"
#include "game/poker/subgame_setup.h"

namespace fisher::game::poker {
namespace {

void ValidatePlayerIndex(int player, const char* message) {
  if (player < 0 || player >= GameBasic::kNumPlayers) {
    throw std::invalid_argument(message);
  }
}

bool IsPostflopRound(PokerRound round) {
  return round == PokerRound::kFlop || round == PokerRound::kTurn ||
         round == PokerRound::kRiver;
}

}  // namespace

NodeState::Args::Args(std::shared_ptr<const SubgameSetup> setup,
                      PokerCards board, float pot,
                      std::array<float, 2> stacks,
                      std::array<float, 2> bet_total,
                      std::array<float, 2> bet_current_round,
                      int actor_player, int last_aggressor,
                      int num_raises_current_round,
                      std::array<bool, 2> is_fold,
                      TerminalStatus terminal_status,
                      std::vector<Action> action_history)
    : setup(std::move(setup)),
      board(std::move(board)),
      pot(pot),
      stacks(stacks),
      bet_total(bet_total),
      bet_current_round(bet_current_round),
      actor_player(actor_player),
      last_aggressor(last_aggressor),
      num_raises_current_round(num_raises_current_round),
      is_fold(is_fold),
      terminal_status(terminal_status),
      action_history(std::move(action_history)) {}

NodeState::NodeState(const Args& args)
    : setup_(args.setup),
      board_(args.board),
      street_(RoundFromBoardSize(board_.Size())),
      pot_(args.pot),
      stacks_(args.stacks),
      bet_total_(args.bet_total),
      bet_current_round_(args.bet_current_round),
      actor_player_(args.actor_player),
      last_aggressor_(args.last_aggressor),
      num_raises_current_round_(args.num_raises_current_round),
      is_fold_(args.is_fold),
      terminal_status_(args.terminal_status),
      action_history_(args.action_history) {
  Validate();
  valid_actions_ = ActionResolver::Resolve(*this);
}

const std::shared_ptr<const SubgameSetup>& NodeState::Setup() const {
  return setup_;
}

const PokerCards& NodeState::Board() const { return board_; }

PokerRound NodeState::Street() const { return street_; }

float NodeState::Pot() const { return pot_; }

const std::array<float, 2>& NodeState::Stacks() const { return stacks_; }

const std::array<float, 2>& NodeState::BetTotal() const {
  return bet_total_;
}

const std::array<float, 2>& NodeState::BetCurrentRound() const {
  return bet_current_round_;
}

int NodeState::ActorPlayer() const { return actor_player_; }

int NodeState::LastAggressor() const { return last_aggressor_; }

int NodeState::NumRaisesCurrentRound() const {
  return num_raises_current_round_;
}

int NodeState::NumHands() const { return GameBasic::kNumHands; }

const std::array<bool, 2>& NodeState::IsFold() const { return is_fold_; }

TerminalStatus NodeState::Status() const { return terminal_status_; }

bool NodeState::IsTerminal() const {
  return terminal_status_ != TerminalStatus::kNotTerminal;
}

const std::vector<Action>& NodeState::ActionHistory() const {
  return action_history_;
}

const std::vector<Action>& NodeState::ValidActions() const {
  return valid_actions_;
}

NodeState NodeState::CommitAction(const Action& action) const {
  if (actor_player_ < 0 || IsTerminal()) {
    throw std::invalid_argument("Cannot commit player action on this node");
  }
  if (action.IsChance()) {
    throw std::invalid_argument("Use CommitChanceAction for chance actions");
  }
  if (std::find(valid_actions_.begin(), valid_actions_.end(), action) ==
      valid_actions_.end()) {
    throw std::invalid_argument("Action is not valid for this node");
  }

  std::array<float, 2> stacks = stacks_;
  std::array<float, 2> bet_total = bet_total_;
  std::array<float, 2> bet_current_round = bet_current_round_;
  std::array<bool, 2> is_fold = is_fold_;
  std::vector<Action> action_history = action_history_;
  action_history.push_back(action);

  const int actor = actor_player_;
  const int opponent = OpponentPlayer();
  int next_actor = opponent;
  int next_last_aggressor = last_aggressor_;
  int next_num_raises = num_raises_current_round_;
  TerminalStatus next_status = TerminalStatus::kNotTerminal;

  if (action.IsFold()) {
    is_fold[static_cast<std::size_t>(actor)] = true;
    next_actor = kTerminalPlayer;
    next_status = TerminalStatus::kFoldTerminal;
  } else if (action.IsCheck()) {
    if (actor == 0) {
      next_actor = 1;
    } else if (street_ == PokerRound::kRiver) {
      next_actor = kTerminalPlayer;
      next_status = TerminalStatus::kShowdownTerminal;
    } else {
      next_actor = kChancePlayer;
    }
  } else if (action.IsCall()) {
    const float to_call =
        bet_current_round[static_cast<std::size_t>(opponent)] -
        bet_current_round[static_cast<std::size_t>(actor)];
    const float call_amount =
        std::min(to_call, stacks[static_cast<std::size_t>(actor)]);
    stacks[static_cast<std::size_t>(actor)] -= call_amount;
    bet_current_round[static_cast<std::size_t>(actor)] += call_amount;
    bet_total[static_cast<std::size_t>(actor)] += call_amount;
    if (stacks[0] == 0.0f || stacks[1] == 0.0f ||
        street_ == PokerRound::kRiver) {
      next_actor = kTerminalPlayer;
      next_status = TerminalStatus::kShowdownTerminal;
    } else {
      next_actor = kChancePlayer;
    }
  } else if (action.IsBet()) {
    const float target = action.Amount();
    const float current_bet = bet_current_round[static_cast<std::size_t>(actor)];
    const float delta = target - current_bet;
    if (delta <= 0.0f || delta > stacks[static_cast<std::size_t>(actor)]) {
      throw std::invalid_argument("Invalid bet action amount");
    }
    stacks[static_cast<std::size_t>(actor)] -= delta;
    bet_current_round[static_cast<std::size_t>(actor)] = target;
    bet_total[static_cast<std::size_t>(actor)] += delta;
    next_last_aggressor = actor;
    next_num_raises += 1;
    next_actor = opponent;
  } else {
    throw std::invalid_argument("Unsupported action type");
  }

  return NodeState(Args(setup_, board_, pot_, stacks, bet_total,
                        bet_current_round, next_actor, next_last_aggressor,
                        next_num_raises, is_fold, next_status,
                        action_history));
}

NodeState NodeState::CommitChanceAction(PokerCard card) const {
  if (actor_player_ != kChancePlayer || IsTerminal()) {
    throw std::invalid_argument("Cannot commit chance action on this node");
  }
  if (street_ == PokerRound::kRiver) {
    throw std::invalid_argument("Cannot deal chance card after river");
  }
  if (board_.Contains(card)) {
    throw std::invalid_argument("Chance card collides with board");
  }

  PokerCards next_board = board_;
  next_board.Add(card);
  std::vector<Action> action_history = action_history_;
  action_history.push_back(Action::Chance());
  const float next_pot = pot_ + bet_current_round_[0] + bet_current_round_[1];

  return NodeState(Args(
      setup_, next_board, next_pot, stacks_, bet_total_,
      std::array<float, 2>{0.0f, 0.0f}, /*actor_player=*/0, last_aggressor_,
      /*num_raises_current_round=*/0, is_fold_,
      TerminalStatus::kNotTerminal, action_history));
}

void NodeState::Validate() const {
  if (setup_ == nullptr) {
    throw std::invalid_argument("NodeState setup cannot be null");
  }
  if (!IsPostflopRound(street_)) {
    throw std::invalid_argument("NodeState only supports postflop boards");
  }
  if (pot_ < 0.0f) {
    throw std::invalid_argument("NodeState pot cannot be negative");
  }
  for (int player = 0; player < GameBasic::kNumPlayers; ++player) {
    const std::size_t index = static_cast<std::size_t>(player);
    if (stacks_[index] < 0.0f) {
      throw std::invalid_argument("NodeState stack cannot be negative");
    }
    if (bet_total_[index] < 0.0f) {
      throw std::invalid_argument("NodeState total bet cannot be negative");
    }
    if (bet_current_round_[index] < 0.0f) {
      throw std::invalid_argument(
          "NodeState current-round bet cannot be negative");
    }
    if (bet_total_[index] < bet_current_round_[index]) {
      throw std::invalid_argument(
          "NodeState total bet cannot be smaller than current-round bet");
    }
  }
  if (last_aggressor_ != -1) {
    ValidatePlayerIndex(last_aggressor_, "NodeState last aggressor invalid");
  }
  if (num_raises_current_round_ < 0) {
    throw std::invalid_argument("NodeState raise count cannot be negative");
  }
  if (actor_player_ != kChancePlayer && actor_player_ != kTerminalPlayer) {
    ValidatePlayerIndex(actor_player_, "NodeState actor player invalid");
  }
  if (terminal_status_ == TerminalStatus::kNotTerminal &&
      actor_player_ == kTerminalPlayer) {
    throw std::invalid_argument("Non-terminal node cannot use terminal actor");
  }
  if (terminal_status_ != TerminalStatus::kNotTerminal &&
      actor_player_ != kTerminalPlayer) {
    throw std::invalid_argument("Terminal node must use terminal actor");
  }
  if (terminal_status_ == TerminalStatus::kFoldTerminal &&
      is_fold_[0] == is_fold_[1]) {
    throw std::invalid_argument("Fold terminal requires exactly one fold");
  }
}

int NodeState::OpponentPlayer() const {
  ValidatePlayerIndex(actor_player_, "NodeState actor player invalid");
  return 1 - actor_player_;
}

}  // namespace fisher::game::poker
