#pragma once

#include <array>
#include <variant>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_cards_isomorphic_index.h"
#include "game/poker/tree_abstracted_bets.h"

namespace fisher::game::poker {

class SubgameSetup {
 public:
  using RootBeliefInput =
      std::variant<std::vector<std::vector<float>>, std::vector<std::string>>;

  struct Args {
    Args() = delete;
    Args(PokerCards board, float pot, std::array<float, 2> stacks,
         std::array<float, 2> bet_current_round, int current_player,
         int last_aggressor, int raise_count,
         std::vector<Action> root_action_history,
         RootBeliefInput root_belief, TreeAbstractedBets abstracted_bets,
         GameBasic game_basic, float bet_rounding);

    PokerCards board;
    float pot;
    std::array<float, 2> stacks;
    std::array<float, 2> bet_current_round;
    int current_player;
    int last_aggressor;
    int raise_count;
    std::vector<Action> root_action_history;
    RootBeliefInput root_belief;
    TreeAbstractedBets abstracted_bets;
    GameBasic game_basic;
    float bet_rounding;
  };

  explicit SubgameSetup(const Args& args);

  const PokerCards& Board() const;
  PokerRound Street() const;
  float Pot() const;
  const std::array<float, 2>& Stacks() const;
  const std::array<float, 2>& BetCurrentRound() const;
  int CurrentPlayer() const;
  int LastAggressor() const;
  int RaiseCount() const;
  const std::vector<Action>& RootActionHistory() const;
  const PokerBelief& RootBelief() const;
  const TreeAbstractedBets& AbstractedBets() const;
  const GameBasic& BasicGame() const;
  float BetRounding() const;

 private:
  static PokerBelief BuildRootBelief(const RootBeliefInput& input,
                                     const PokerCards& board,
                                     const GameBasic& game_basic);

  PokerCards board_;
  GameBasic game_basic_;
  PokerRound street_;
  float pot_;
  std::array<float, 2> stacks_;
  std::array<float, 2> bet_current_round_;
  int current_player_;
  int last_aggressor_;
  int raise_count_;
  std::vector<Action> root_action_history_;
  TreeAbstractedBets abstracted_bets_;
  float bet_rounding_;
  PokerBelief root_belief_;
};

}  // namespace fisher::game::poker
