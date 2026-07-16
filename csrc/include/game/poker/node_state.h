#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/money.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_cards_isomorphic_index.h"

namespace fisher::game::poker {

class SubgameSetup;

enum class TerminalStatus {
  kNotTerminal = 0,
  kFoldTerminal = 1,
  kShowdownTerminal = 2,
};

struct PlayerTerminalPayoff {
  float win = 0.0f;
  float lose = 0.0f;
  float chop = 0.0f;
};

struct TerminalPayoff {
  float contested_pot = 0.0f;
  float rake = 0.0f;
  float uncalled_bet = 0.0f;
  std::array<PlayerTerminalPayoff, 2> players;
};

class NodeState {
 public:
  static constexpr int kChancePlayer = -1;
  static constexpr int kTerminalPlayer = -2;

  struct Args {
    Args() = delete;
    Args(std::shared_ptr<const SubgameSetup> setup, PokerCards board,
         float pot, std::array<float, 2> stacks,
         std::array<float, 2> bet_total,
         std::array<float, 2> bet_current_round, int actor_player,
         int last_aggressor, int num_raises_current_round,
         std::array<bool, 2> is_fold, TerminalStatus terminal_status,
         std::vector<Action> action_history);

    std::shared_ptr<const SubgameSetup> setup;
    PokerCards board;
    float pot;
    std::array<float, 2> stacks;
    std::array<float, 2> bet_total;
    std::array<float, 2> bet_current_round;
    int actor_player;
    int last_aggressor;
    int num_raises_current_round;
    std::array<bool, 2> is_fold;
    TerminalStatus terminal_status;
    std::vector<Action> action_history;
  };

  explicit NodeState(const Args& args);

  const std::shared_ptr<const SubgameSetup>& Setup() const;
  const PokerCards& Board() const;
  PokerRound Street() const;
  float Pot() const;
  std::array<float, 2> Stacks() const;
  std::array<float, 2> BetTotal() const;
  std::array<float, 2> BetCurrentRound() const;
  int ActorPlayer() const;
  int LastAggressor() const;
  int NumRaisesCurrentRound() const;
  int NumHands() const;
  const std::array<bool, 2>& IsFold() const;
  TerminalStatus Status() const;
  bool IsTerminal() const;
  bool HasTerminalPayoff() const;
  const TerminalPayoff& GetTerminalPayoff() const;
  const std::vector<Action>& ActionHistory() const;
  const std::vector<Action>& ValidActions() const;

  NodeState CommitAction(const Action& action) const;
  NodeState CommitChanceAction(PokerCard card) const;

 private:
  void Validate() const;
  TerminalPayoff BuildTerminalPayoff() const;
  int OpponentPlayer() const;

  std::shared_ptr<const SubgameSetup> setup_;
  PokerCards board_;
  PokerRound street_;
  MoneyMilliBb pot_;
  std::array<MoneyMilliBb, 2> stacks_;
  std::array<MoneyMilliBb, 2> bet_total_;
  std::array<MoneyMilliBb, 2> bet_current_round_;
  int actor_player_;
  int last_aggressor_;
  int num_raises_current_round_;
  std::array<bool, 2> is_fold_;
  TerminalStatus terminal_status_;
  std::optional<TerminalPayoff> terminal_payoff_;
  std::vector<Action> action_history_;
  std::vector<Action> valid_actions_;
};

}  // namespace fisher::game::poker
