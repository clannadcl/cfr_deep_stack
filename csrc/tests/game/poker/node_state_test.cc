#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/abstracted_action.h"
#include "game/poker/action.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_card.h"
#include "game/poker/poker_cards.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/tree_abstracted_bets.h"

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

std::vector<std::vector<float>> MatrixBelief(float value) {
  return std::vector<std::vector<float>>(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, value));
}

bool HasAction(const std::vector<fisher::game::poker::Action>& actions,
               const fisher::game::poker::Action& action) {
  for (const fisher::game::poker::Action& current : actions) {
    if (current == action) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    fisher::game::poker::PokerCards board, float pot,
    std::array<float, 2> stacks, std::array<float, 2> bet_total,
    std::array<float, 2> bet_current_round, int current_player,
    int last_aggressor, int raise_count,
    fisher::game::poker::TreeAbstractedBets abstracted_bets,
    float min_raise_size = 1.0f,
    fisher::game::poker::GameBasic game_basic =
        fisher::game::poker::GameBasic()) {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::SubgameSetup;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, pot, stacks, bet_total, bet_current_round, current_player,
      last_aggressor, raise_count, std::vector<fisher::game::poker::Action>{},
      MatrixBelief(1.0f), abstracted_bets, std::move(game_basic),
      /*bet_rounding=*/0.1f, min_raise_size));
}

fisher::game::poker::TreeAbstractedBets MakeBets(
    const fisher::game::poker::AbstractedBetStringConfig& bets) {
  return fisher::game::poker::TreeAbstractedBets(
      bets, fisher::game::poker::AbstractedDonkBetStringConfig{"50%"});
}

}  // namespace

int main() {
  using fisher::game::poker::AbstractedBetStringConfig;
  using fisher::game::poker::AbstractedDonkBetStringConfig;
  using fisher::game::poker::Action;
  using fisher::game::poker::NodeState;
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerRound;
  using fisher::game::poker::RakeConfig;
  using fisher::game::poker::TerminalStatus;
  using fisher::game::poker::TreeAbstractedBets;

  auto root_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, /*current_player=*/1, /*last_aggressor=*/-1,
      /*raise_count=*/0, MakeBets({{"33%", "66%", "allin"}}));
  NodeState root = root_setup->GetRootNodeState();
  Expect(root.Board().ToString() == "AsKdQh", "root board mismatch");
  Expect(root.Street() == PokerRound::kFlop, "root street mismatch");
  Expect(root.ActorPlayer() == 1, "root actor mismatch");
  Expect(!root.HasTerminalPayoff(), "non-terminal root should have no payoff");
  ExpectInvalidArgument(
      [&] { root.GetTerminalPayoff(); },
      "non-terminal payoff access should be invalid");
  Expect(root.ValidActions().front() == Action::Check(),
         "check should be first root action");
  Expect(HasAction(root.ValidActions(), Action::Bet(3.3f)),
         "33 percent pot bet missing");
  Expect(HasAction(root.ValidActions(), Action::Bet(100.0f)),
         "allin bet missing");

  auto check_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 0, -1, 0, MakeBets({{"50%"}}));
  NodeState after_first_check =
      check_setup->GetRootNodeState().CommitAction(Action::Check());
  Expect(after_first_check.ActorPlayer() == 1,
         "first check should pass action to player 1");
  NodeState chance_node = after_first_check.CommitAction(Action::Check());
  Expect(chance_node.ActorPlayer() == NodeState::kChancePlayer,
         "flop check-check should enter chance node");
  Expect(chance_node.ValidActions().size() == 1,
         "chance node should have one action");
  Expect(chance_node.ValidActions()[0] == Action::Chance(),
         "chance node action mismatch");
  NodeState turn = chance_node.CommitChanceAction(PokerCard("2c"));
  Expect(turn.Board().ToString() == "AsKdQh2c", "turn board mismatch");
  Expect(turn.Pot() == 10.0f, "check-check chance pot mismatch");
  Expect(turn.ActorPlayer() == 0, "turn first actor mismatch");

  auto bet_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 0, -1, 0, MakeBets({{"50%"}}));
  NodeState after_bet =
      bet_setup->GetRootNodeState().CommitAction(Action::Bet(5.0f));
  Expect(after_bet.Stacks()[0] == 95.0f, "bettor stack mismatch");
  Expect(after_bet.BetCurrentRound()[0] == 5.0f,
         "bettor current bet mismatch");
  Expect(after_bet.BetTotal()[0] == 5.0f, "bettor total bet mismatch");
  Expect(after_bet.LastAggressor() == 0, "last aggressor mismatch");
  Expect(after_bet.NumRaisesCurrentRound() == 1, "raise count mismatch");
  Expect(HasAction(after_bet.ValidActions(), Action::Fold()),
         "facing bet should allow fold");
  Expect(HasAction(after_bet.ValidActions(), Action::Call()),
         "facing bet should allow call");
  NodeState after_call = after_bet.CommitAction(Action::Call());
  Expect(after_call.ActorPlayer() == NodeState::kChancePlayer,
         "flop bet-call should enter chance node");
  Expect(after_call.Pot() == 10.0f, "pot should not settle before chance");
  NodeState bet_call_turn = after_call.CommitChanceAction(PokerCard("2c"));
  Expect(bet_call_turn.Pot() == 20.0f, "chance should settle street bets");
  Expect(bet_call_turn.BetCurrentRound()[0] == 0.0f,
         "turn current bet player 0 mismatch");
  Expect(bet_call_turn.BetCurrentRound()[1] == 0.0f,
         "turn current bet player 1 mismatch");

  NodeState fold_terminal = after_bet.CommitAction(Action::Fold());
  Expect(fold_terminal.ActorPlayer() == NodeState::kTerminalPlayer,
         "fold should be terminal");
  Expect(fold_terminal.Status() == TerminalStatus::kFoldTerminal,
         "fold terminal status mismatch");
  Expect(fold_terminal.Pot() == 10.0f, "fold should keep pot unsettled");
  Expect(fold_terminal.HasTerminalPayoff(),
         "fold terminal should cache payoff");
  const auto& fold_payoff = fold_terminal.GetTerminalPayoff();
  ExpectNear(fold_payoff.contested_pot, 10.0f,
             "fold payoff should exclude uncalled bet");
  ExpectNear(fold_payoff.uncalled_bet, 5.0f,
             "fold payoff uncalled bet mismatch");
  ExpectNear(fold_payoff.rake, 0.0f, "fold payoff rake mismatch");
  ExpectNear(fold_payoff.players[0].win, 5.0f,
             "fold payoff win mismatch");
  ExpectNear(fold_payoff.players[0].lose, -5.0f,
             "fold payoff lose mismatch");
  ExpectNear(fold_payoff.players[0].chop, 0.0f,
             "fold payoff chop mismatch");

  auto raked_fold_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {95.0f, 100.0f}, {5.0f, 0.0f},
      {5.0f, 0.0f}, 1, 0, 1, MakeBets({{"50%"}}),
      /*min_raise_size=*/1.0f,
      fisher::game::poker::GameBasic(RakeConfig{/*enabled=*/true,
                                                /*percentage=*/0.05,
                                                /*cap=*/100.0}));
  NodeState raked_fold_terminal =
      raked_fold_setup->GetRootNodeState().CommitAction(Action::Fold());
  const auto& raked_fold_payoff = raked_fold_terminal.GetTerminalPayoff();
  ExpectNear(raked_fold_payoff.contested_pot, 10.0f,
             "raked fold contested pot mismatch");
  ExpectNear(raked_fold_payoff.rake, 0.5f, "raked fold rake mismatch");
  ExpectNear(raked_fold_payoff.players[0].win, 4.5f,
             "raked fold win mismatch");
  ExpectNear(raked_fold_payoff.players[0].lose, -5.0f,
             "raked fold lose mismatch");

  auto river_setup = MakeSetup(
      PokerCards("AsKdQh2c3d"), 20.0f, {80.0f, 80.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 0, -1, 0, MakeBets({{"50%"}}));
  NodeState river_showdown =
      river_setup->GetRootNodeState()
          .CommitAction(Action::Check())
          .CommitAction(Action::Check());
  Expect(river_showdown.Status() == TerminalStatus::kShowdownTerminal,
         "river check-check should showdown");
  Expect(river_showdown.HasTerminalPayoff(),
         "river showdown should cache payoff");
  const auto& river_payoff = river_showdown.GetTerminalPayoff();
  ExpectNear(river_payoff.contested_pot, 20.0f,
             "river showdown contested pot mismatch");
  ExpectNear(river_payoff.players[0].win, 10.0f,
             "river showdown win mismatch");
  ExpectNear(river_payoff.players[0].lose, -10.0f,
             "river showdown lose mismatch");
  ExpectNear(river_payoff.players[0].chop, 0.0f,
             "river showdown chop mismatch");

  auto raked_showdown_setup = MakeSetup(
      PokerCards("AsKdQh2c3d"), 20.0f, {75.0f, 75.0f}, {5.0f, 5.0f},
      {5.0f, 5.0f}, 0, 1, 1, MakeBets({{"50%"}}),
      /*min_raise_size=*/1.0f,
      fisher::game::poker::GameBasic(RakeConfig{/*enabled=*/true,
                                                /*percentage=*/0.05,
                                                /*cap=*/100.0}));
  NodeState raked_showdown =
      raked_showdown_setup->GetRootNodeState()
          .CommitAction(Action::Check())
          .CommitAction(Action::Check());
  const auto& raked_showdown_payoff = raked_showdown.GetTerminalPayoff();
  ExpectNear(raked_showdown_payoff.contested_pot, 30.0f,
             "raked showdown contested pot mismatch");
  ExpectNear(raked_showdown_payoff.rake, 1.5f,
             "raked showdown rake mismatch");
  ExpectNear(raked_showdown_payoff.players[0].win, 13.5f,
             "raked showdown win mismatch");
  ExpectNear(raked_showdown_payoff.players[0].lose, -15.0f,
             "raked showdown lose mismatch");
  ExpectNear(raked_showdown_payoff.players[0].chop, -0.75f,
             "raked showdown chop mismatch");

  auto short_call_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {90.0f, 3.0f}, {10.0f, 0.0f},
      {10.0f, 0.0f}, 1, 0, 1, MakeBets({{"50%"}, {"50%"}}));
  NodeState short_call =
      short_call_setup->GetRootNodeState().CommitAction(Action::Call());
  Expect(short_call.Status() == TerminalStatus::kShowdownTerminal,
         "short allin call should showdown immediately");
  Expect(short_call.BetCurrentRound()[1] == 3.0f,
         "short allin call amount mismatch");
  const auto& short_call_payoff = short_call.GetTerminalPayoff();
  ExpectNear(short_call_payoff.contested_pot, 16.0f,
             "short allin payoff should exclude uncalled amount");
  ExpectNear(short_call_payoff.uncalled_bet, 7.0f,
             "short allin uncalled amount mismatch");
  ExpectNear(short_call_payoff.players[0].win, 8.0f,
             "short allin win mismatch");
  ExpectNear(short_call_payoff.players[0].lose, -8.0f,
             "short allin lose mismatch");

  auto raise_setup = MakeSetup(
      PokerCards("AsKdQh"), 3.0f, {97.0f, 100.0f}, {3.0f, 0.0f},
      {3.0f, 0.0f}, 1, 0, 1, MakeBets({{"33%"}, {"50%"}}));
  NodeState raise_node = raise_setup->GetRootNodeState();
  Expect(HasAction(raise_node.ValidActions(), Action::Bet(7.5f)),
         "50 percent raise target mismatch");

  auto x_raise_setup = MakeSetup(
      PokerCards("AsKdQh"), 3.0f, {97.0f, 100.0f}, {3.0f, 0.0f},
      {3.0f, 0.0f}, 1, 0, 1, MakeBets({{"33%"}, {"2.5x"}}));
  Expect(HasAction(x_raise_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(7.5f)),
         "2.5x raise target mismatch");

  auto geometric_setup = MakeSetup(
      PokerCards("AsKdQh"), 100.0f, {400.0f, 400.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 1, -1, 0, MakeBets({{"2e"}}));
  Expect(HasAction(geometric_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(100.0f)),
         "2e open target mismatch");

  auto geometric_allin_setup = MakeSetup(
      PokerCards("AsKdQh"), 100.0f, {400.0f, 400.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 1, -1, 0, MakeBets({{"1e", "allin"}}));
  Expect(HasAction(geometric_allin_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(400.0f)),
         "1e should resolve to allin target");

  auto capped_geometric_setup = MakeSetup(
      PokerCards("AsKdQh"), 100.0f, {1000.0f, 1000.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 1, -1, 0, MakeBets({{"1e200%"}}));
  Expect(HasAction(capped_geometric_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(200.0f)),
         "capped geometric open target mismatch");

  TreeAbstractedBets::Args merging_args;
  merging_args.default_bets = {{fisher::game::poker::AbstractedAction::
                                    BetPercent(50.0f),
                                fisher::game::poker::AbstractedAction::
                                    BetPercent(52.0f)}};
  merging_args.default_donk_bets = {
      fisher::game::poker::AbstractedAction::BetPercent(50.0f)};
  merging_args.merging_threshold = 0.1f;
  auto merging_setup = MakeSetup(
      PokerCards("AsKdQh"), 100.0f, {1000.0f, 1000.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 1, -1, 0, TreeAbstractedBets(merging_args));
  Expect(!HasAction(merging_setup->GetRootNodeState().ValidActions(),
                    Action::Bet(50.0f)),
         "close smaller bet should be merged");
  Expect(HasAction(merging_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(52.0f)),
         "close larger bet should be kept");

  TreeAbstractedBets donk_bets(AbstractedBetStringConfig{{"10bb"}},
                               AbstractedDonkBetStringConfig{"2.5bb"});
  auto donk_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 0, 1, 0, donk_bets);
  NodeState donk_node = donk_setup->GetRootNodeState();
  Expect(HasAction(donk_node.ValidActions(), Action::Bet(2.5f)),
         "donk bet config should be used");
  Expect(!HasAction(donk_node.ValidActions(), Action::Bet(10.0f)),
         "street bet config should not be used for donk");

  auto min_raise_setup = MakeSetup(
      PokerCards("AsKdQh"), 10.0f, {90.0f, 100.0f}, {10.0f, 0.0f},
      {10.0f, 0.0f}, 1, 0, 1, MakeBets({{"33%"}, {"12bb", "15bb"}}),
      /*min_raise_size=*/5.0f);
  NodeState min_raise_node = min_raise_setup->GetRootNodeState();
  Expect(!HasAction(min_raise_node.ValidActions(), Action::Bet(12.0f)),
         "raise below min raise should be filtered");
  Expect(HasAction(min_raise_node.ValidActions(), Action::Bet(15.0f)),
         "raise at min raise should be allowed");

  auto threshold_setup = MakeSetup(
      PokerCards("AsKdQh"), 100.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 1, -1, 0, MakeBets({{"80%"}}));
  Expect(HasAction(threshold_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(100.0f)),
         "75 percent threshold should replace bet with allin");

  auto spr_setup = MakeSetup(
      PokerCards("AsKdQh"), 100.0f, {200.0f, 200.0f}, {0.0f, 0.0f},
      {0.0f, 0.0f}, 1, -1, 0, MakeBets({{"33%"}}));
  Expect(HasAction(spr_setup->GetRootNodeState().ValidActions(),
                   Action::Bet(200.0f)),
         "low SPR should add allin");

  ExpectInvalidArgument(
      [&] { chance_node.CommitChanceAction(PokerCard("As")); },
      "chance collision should be invalid");
  ExpectInvalidArgument(
      [&] { root.CommitAction(Action::Call()); },
      "invalid root action should be rejected");

  return 0;
}
