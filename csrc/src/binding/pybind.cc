#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "algorithm/cfr.h"
#include "algorithm/poker_cfr_solver.h"
#include "game/kuhn.h"
#include "game/poker/action.h"
#include "game/poker/game_basic.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_cards_isomorphic_index.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/tree_abstracted_bets.h"

namespace py = pybind11;

namespace {

fisher::game::KuhnGameConfig ParseGameConfig(py::dict config) {
  fisher::game::KuhnGameConfig game_config;
  if (config.contains("game"))
    game_config.name = config["game"].cast<std::string>();
  if (config.contains("name"))
    game_config.name = config["name"].cast<std::string>();
  return game_config;
}

py::dict BuildGame(py::dict config) {
  fisher::game::KuhnPokerGame game(ParseGameConfig(config));
  py::dict result;
  result["game"] = game.Name();
  result["num_players"] = game.NumPlayers();
  return result;
}

py::dict Solve(py::dict game_config, py::dict solver_config) {
  fisher::game::KuhnPokerGame game(ParseGameConfig(game_config));
  const int iterations = solver_config.contains("iterations")
                             ? solver_config["iterations"].cast<int>()
                             : 1000;
  const bool cfr_plus = solver_config.contains("cfr_plus")
                            ? solver_config["cfr_plus"].cast<bool>()
                            : false;

  fisher::algorithm::KuhnCfrSolver solver(cfr_plus);
  solver.Run(game, iterations);

  py::dict result;
  result["game"] = game.Name();
  result["iterations"] = solver.Iterations();
  result["strategy"] = solver.AveragePolicy();
  return result;
}

std::array<double, 2> PolicyValue(py::dict game_config,
                                  const fisher::algorithm::Strategy &strategy) {
  fisher::game::KuhnPokerGame game(ParseGameConfig(game_config));
  return fisher::algorithm::PolicyValue(game, strategy);
}

double NashExploitability(py::dict game_config,
                          const fisher::algorithm::Strategy &strategy) {
  fisher::game::KuhnPokerGame game(ParseGameConfig(game_config));
  return fisher::algorithm::NashExploitability(game, strategy);
}

using fisher::algorithm::PokerCfrSolver;
using fisher::game::poker::Action;
using fisher::game::poker::GameBasic;
using fisher::game::poker::IsomorphicMapping;
using fisher::game::poker::NodeState;
using fisher::game::poker::PokerCards;
using fisher::game::poker::PokerHand;
using fisher::game::poker::PokerRound;
using fisher::game::poker::RakeConfig;
using fisher::game::poker::SubgameSetup;
using fisher::game::poker::TreeAbstractedBets;

py::dict NestedSpotConfig(py::dict config) {
  if (config.contains("config")) {
    py::dict wrapper = config["config"].cast<py::dict>();
    if (wrapper.contains("spot_config")) {
      return wrapper["spot_config"].cast<py::dict>();
    }
  }
  if (config.contains("spot_config")) {
    return config["spot_config"].cast<py::dict>();
  }
  return config;
}

float FloatField(py::dict config, const char *name) {
  if (!config.contains(name) || config[name].is_none()) {
    throw std::invalid_argument(std::string("Missing required field: ") + name);
  }
  return config[name].cast<float>();
}

float FloatField(py::dict config, const char *name, float default_value) {
  if (!config.contains(name) || config[name].is_none()) {
    return default_value;
  }
  return config[name].cast<float>();
}

int IntField(py::dict config, const char *name, int default_value) {
  if (!config.contains(name) || config[name].is_none()) {
    return default_value;
  }
  return config[name].cast<int>();
}

std::array<float, 2> FloatArray2Field(py::dict config, const char *name) {
  if (!config.contains(name) || config[name].is_none()) {
    throw std::invalid_argument(std::string("Missing required field: ") + name);
  }
  const std::vector<float> values = config[name].cast<std::vector<float>>();
  if (values.size() != 2) {
    throw std::invalid_argument(std::string(name) + " must contain 2 values");
  }
  return {values[0], values[1]};
}

std::array<float, 2> FloatArray2Field(py::dict config, const char *name,
                                      std::array<float, 2> default_value) {
  if (!config.contains(name) || config[name].is_none()) {
    return default_value;
  }
  return FloatArray2Field(config, name);
}

RakeConfig ParseRakeConfig(py::dict config) {
  if (!config.contains("rake") || config["rake"].is_none()) {
    return RakeConfig{};
  }
  py::dict rake = config["rake"].cast<py::dict>();
  RakeConfig parsed;
  parsed.enabled = rake.contains("enabled") && !rake["enabled"].is_none()
                       ? rake["enabled"].cast<bool>()
                       : false;
  parsed.percentage =
      rake.contains("percentage") && !rake["percentage"].is_none()
          ? rake["percentage"].cast<double>()
          : 0.0;
  parsed.cap = rake.contains("cap") && !rake["cap"].is_none()
                   ? rake["cap"].cast<double>()
                   : 0.0;
  return parsed;
}

Action ParsePokerActionToken(const std::string &token) {
  if (token == "x")
    return Action::Check();
  if (token == "f")
    return Action::Fold();
  if (token == "c")
    return Action::Call();
  if (token == "C")
    return Action::Chance();
  if (token.size() > 1 && token[0] == 'b') {
    return Action::Bet(std::stof(token.substr(1)));
  }
  throw std::invalid_argument("Unsupported poker action token: " + token);
}

std::vector<std::string>
TokenizeActionHistoryString(const std::string &history) {
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < history.size()) {
    const char current = history[index];
    if (std::isspace(static_cast<unsigned char>(current)) || current == ',' ||
        current == '-') {
      ++index;
      continue;
    }
    if (current == 'f' || current == 'x' || current == 'c' || current == 'C') {
      tokens.emplace_back(1, current);
      ++index;
      continue;
    }
    if (current == 'b') {
      const std::size_t begin = index;
      ++index;
      while (index < history.size()) {
        const char amount_char = history[index];
        if (std::isdigit(static_cast<unsigned char>(amount_char)) ||
            amount_char == '.') {
          ++index;
          continue;
        }
        break;
      }
      tokens.push_back(history.substr(begin, index - begin));
      continue;
    }
    throw std::invalid_argument("Unsupported action history character");
  }
  return tokens;
}

std::vector<Action> ParsePokerActionHistory(py::object input) {
  std::vector<std::string> tokens;
  if (input.is_none()) {
    return {};
  }
  if (py::isinstance<py::str>(input)) {
    tokens = TokenizeActionHistoryString(input.cast<std::string>());
  } else {
    tokens = input.cast<std::vector<std::string>>();
  }
  std::vector<Action> parsed;
  parsed.reserve(tokens.size());
  for (const std::string &token : tokens) {
    parsed.push_back(ParsePokerActionToken(token));
  }
  return parsed;
}

TreeAbstractedBets ParseAbstractedBets(py::dict config) {
  if (!config.contains("abstracted_bets") ||
      config["abstracted_bets"].is_none()) {
    return TreeAbstractedBets(TreeAbstractedBets::Args{});
  }
  py::dict abstracted = config["abstracted_bets"].cast<py::dict>();
  const auto bets =
      abstracted.contains("bets") && !abstracted["bets"].is_none()
          ? abstracted["bets"].cast<std::vector<std::vector<std::string>>>()
          : std::vector<std::vector<std::string>>{
                {"33%", "66%", "125%", "allin"},
                {"50%", "100%", "allin"},
            };
  const auto donk_bets =
      abstracted.contains("donk_bets") && !abstracted["donk_bets"].is_none()
          ? abstracted["donk_bets"].cast<std::vector<std::string>>()
          : std::vector<std::string>{"33%"};
  TreeAbstractedBets parsed(bets, donk_bets);
  const auto set_street_bets = [&](const char *key, PokerRound round) {
    if (abstracted.contains(key) && !abstracted[key].is_none()) {
      parsed.SetStreetBets(
          round, abstracted[key].cast<std::vector<std::vector<std::string>>>());
    }
  };
  const auto set_donk_bets = [&](const char *key, PokerRound round) {
    if (abstracted.contains(key) && !abstracted[key].is_none()) {
      parsed.SetDonkBets(round,
                         abstracted[key].cast<std::vector<std::string>>());
    }
  };
  set_street_bets("preflop_bets", PokerRound::kPreflop);
  set_street_bets("flop_bets", PokerRound::kFlop);
  set_street_bets("turn_bets", PokerRound::kTurn);
  set_street_bets("river_bets", PokerRound::kRiver);
  set_donk_bets("preflop_donk_bets", PokerRound::kPreflop);
  set_donk_bets("flop_donk_bets", PokerRound::kFlop);
  set_donk_bets("turn_donk_bets", PokerRound::kTurn);
  set_donk_bets("river_donk_bets", PokerRound::kRiver);
  if (abstracted.contains("bet_to_allin_threshold") &&
      !abstracted["bet_to_allin_threshold"].is_none()) {
    parsed.SetBetToAllInThreshold(
        abstracted["bet_to_allin_threshold"].cast<float>());
  }
  if (abstracted.contains("add_allin_threshold") &&
      !abstracted["add_allin_threshold"].is_none()) {
    parsed.SetAddAllInThreshold(
        abstracted["add_allin_threshold"].cast<float>());
  }
  if (abstracted.contains("merging_threshold") &&
      !abstracted["merging_threshold"].is_none()) {
    parsed.SetMergingThreshold(abstracted["merging_threshold"].cast<float>());
  }
  return parsed;
}

SubgameSetup::RootBeliefInput ParseRootBelief(py::dict config) {
  if (config.contains("beliefs") && !config["beliefs"].is_none()) {
    return config["beliefs"].cast<std::vector<std::vector<float>>>();
  }
  if (!config.contains("ranges") || config["ranges"].is_none()) {
    throw std::invalid_argument("Poker spot config must contain ranges");
  }
  return config["ranges"].cast<std::vector<std::string>>();
}

std::shared_ptr<SubgameSetup> BuildPokerSubgameSetup(py::dict input_config) {
  py::dict config = NestedSpotConfig(input_config);
  if (!config.contains("board_cards") || config["board_cards"].is_none()) {
    throw std::invalid_argument("Missing required field: board_cards");
  }
  const GameBasic game(ParseRakeConfig(config));
  const float bet_rounding = FloatField(config, "min_bet_increment", 0.1f);
  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      PokerCards(config["board_cards"].cast<std::string>()),
      FloatField(config, "common_pot"), FloatArray2Field(config, "stacks"),
      FloatArray2Field(config, "bet_total", {0.0f, 0.0f}),
      FloatArray2Field(config, "bet_current_round", {0.0f, 0.0f}),
      IntField(config, "current_player", 0),
      IntField(config, "previous_street_aggressor",
               IntField(config, "last_aggressor", -1)),
      IntField(config, "raise_count", 0),
      config.contains("root_action_history")
          ? ParsePokerActionHistory(config["root_action_history"])
          : std::vector<Action>{},
      ParseRootBelief(config), ParseAbstractedBets(config), game,
      /*bet_rounding=*/bet_rounding,
      /*min_raise_size=*/FloatField(config, "min_raise_size", bet_rounding)));
}

int RankGridIndex(fisher::game::poker::PokerRank rank) {
  return 12 - static_cast<int>(rank);
}

py::dict MakeEmptyCell(int row, int col, const std::string &label,
                       int num_actions) {
  py::dict cell;
  cell["row"] = row;
  cell["col"] = col;
  cell["label"] = label;
  cell["reach"] = 0.0f;
  cell["combos"] = 0;
  cell["strategy"] =
      std::vector<float>(static_cast<std::size_t>(num_actions), 0.0f);
  return cell;
}

std::string HandClassLabel(int row, int col) {
  constexpr char ranks[] = "AKQJT98765432";
  if (row == col) {
    return std::string{ranks[row], ranks[col]};
  }
  const int high = std::min(row, col);
  const int low = std::max(row, col);
  return std::string{ranks[high], ranks[low]} + (row < col ? "s" : "o");
}

struct PokerSolveSession {
  PokerSolveSession(std::shared_ptr<SubgameSetup> setup,
                    const PokerCfrSolver::Args &args)
      : setup(std::move(setup)), solver(args), result(solver.Solve()) {}

  std::shared_ptr<SubgameSetup> setup;
  PokerCfrSolver solver;
  PokerCfrSolver::SolveResult result;
};

std::shared_ptr<PokerSolveSession> SolvePoker(py::dict spot_config,
                                              py::dict solver_config) {
  auto setup = BuildPokerSubgameSetup(spot_config);
  const int num_threads = IntField(solver_config, "num_threads",
                                   IntField(solver_config, "threads", 0));
  const int max_iterations =
      IntField(solver_config, "max_iterations",
               IntField(solver_config, "iterations", 500));
  const int check_interval =
      IntField(solver_config, "exploitability_check_interval", 50);
  const float target_exploitability =
      FloatField(solver_config, "target_exploitability", -1.0f);
  PokerCfrSolver::Args args(setup, num_threads, max_iterations, check_interval,
                            target_exploitability);
  return std::make_shared<PokerSolveSession>(setup, args);
}

py::dict PokerNodeStrategyMatrix(PokerSolveSession &session,
                                 py::object action_history,
                                 std::optional<int> player) {
  const std::vector<Action> parsed_history =
      ParsePokerActionHistory(action_history);
  const std::optional<int> node_id =
      session.solver.Tree().FindNode(parsed_history);
  if (!node_id.has_value()) {
    throw std::invalid_argument("Cannot find node for action history");
  }
  const auto &node = session.solver.Tree().Node(*node_id);
  const NodeState &state = *node.node_state;
  const int actor = state.ActorPlayer();
  if (state.IsTerminal() || actor < 0) {
    throw std::invalid_argument(
        "Strategy matrix can only be exported for player nodes");
  }
  const int reach_player = player.value_or(actor);
  if (reach_player < 0 || reach_player >= GameBasic::kNumPlayers) {
    throw std::invalid_argument("Player must be 0 or 1");
  }

  py::list actions;
  for (const Action &action : state.ValidActions()) {
    actions.append(action.ToString());
  }

  const int num_actions = session.solver.Storage().NumActions(*node_id);
  const IsomorphicMapping &mapping = session.solver.MappingForNode(*node_id);
  const GameBasic &game = state.Setup()->BasicGame();
  const float *reach =
      session.solver.Storage().ReachBlock(*node_id, reach_player);

  std::array<std::array<double, 13>, 13> reach_mass_by_cell{};
  std::array<std::array<int, 13>, 13> combos_by_cell{};
  std::array<std::array<std::vector<double>, 13>, 13> strategy_weight_by_cell;
  for (auto &row : strategy_weight_by_cell) {
    for (std::vector<double> &cell : row) {
      cell.assign(static_cast<std::size_t>(num_actions), 0.0);
    }
  }

  for (int raw_index = 0; raw_index < GameBasic::kNumHands; ++raw_index) {
    const int iso_index = mapping.RawToIso(raw_index);
    if (iso_index == IsomorphicMapping::kInvalidIsoIndex) {
      continue;
    }
    const PokerHand &hand = game.HandFromIndex(raw_index);
    const int high = RankGridIndex(hand.HighCard().Rank());
    const int low = RankGridIndex(hand.LowCard().Rank());
    const bool pair = high == low;
    const bool suited = hand.HighCard().Suit() == hand.LowCard().Suit();
    const int row = pair ? high : (suited ? high : low);
    const int col = pair ? low : (suited ? low : high);
    const double raw_reach =
        static_cast<double>(reach[iso_index]) /
        static_cast<double>(mapping.RawHandCount(iso_index));
    reach_mass_by_cell[static_cast<std::size_t>(row)]
                      [static_cast<std::size_t>(col)] += raw_reach;
    ++combos_by_cell[static_cast<std::size_t>(row)]
                    [static_cast<std::size_t>(col)];
    for (int action = 0; action < num_actions; ++action) {
      strategy_weight_by_cell[static_cast<std::size_t>(row)]
                             [static_cast<std::size_t>(col)]
                             [static_cast<std::size_t>(action)] +=
          raw_reach * static_cast<double>(session.solver.AverageStrategyAt(
                          *node_id, action, iso_index));
    }
  }

  py::list cells;
  double max_reach = 0.0;
  for (int row = 0; row < 13; ++row) {
    for (int col = 0; col < 13; ++col) {
      const int combo_count = combos_by_cell[row][col];
      const double cell_reach =
          combo_count > 0
              ? reach_mass_by_cell[row][col] / static_cast<double>(combo_count)
              : 0.0;
      max_reach = std::max(max_reach, cell_reach);
    }
  }
  for (int row = 0; row < 13; ++row) {
    for (int col = 0; col < 13; ++col) {
      py::dict cell =
          MakeEmptyCell(row, col, HandClassLabel(row, col), num_actions);
      const double cell_reach_mass = reach_mass_by_cell[row][col];
      const int combo_count = combos_by_cell[row][col];
      const double cell_reach =
          combo_count > 0 ? cell_reach_mass / static_cast<double>(combo_count)
                          : 0.0;
      cell["reach"] = cell_reach;
      cell["reach_mass"] = cell_reach_mass;
      cell["normalized_reach"] = max_reach > 0.0 ? cell_reach / max_reach : 0.0;
      cell["combos"] = combo_count;
      std::vector<float> strategy(static_cast<std::size_t>(num_actions), 0.0f);
      if (cell_reach_mass > 0.0) {
        for (int action = 0; action < num_actions; ++action) {
          strategy[static_cast<std::size_t>(action)] = static_cast<float>(
              strategy_weight_by_cell[row][col]
                                     [static_cast<std::size_t>(action)] /
              cell_reach_mass);
        }
      }
      cell["strategy"] = strategy;
      cells.append(cell);
    }
  }

  std::vector<std::string> history;
  for (const Action &action : state.ActionHistory()) {
    history.push_back(action.ToString());
  }

  py::dict result;
  result["node_id"] = *node_id;
  result["actor_player"] = actor;
  result["reach_player"] = reach_player;
  result["board"] = state.Board().ToString();
  result["history"] = history;
  result["actions"] = actions;
  result["cells"] = cells;
  result["max_reach"] = max_reach;
  return result;
}

py::dict PokerSolveMetadata(PokerSolveSession &session) {
  py::dict metadata;
  metadata["iterations"] = session.result.iterations;
  metadata["converged"] = session.result.converged;
  metadata["target_exploitability"] = session.result.target_exploitability;
  metadata["exploitability"] = session.result.exploitability;
  metadata["current_ev"] = session.result.current_ev;
  metadata["best_response_ev"] = session.result.best_response_ev;
  metadata["num_threads"] = session.solver.NumThreads();
  metadata["num_nodes"] = session.solver.Tree().NumNodes();
  metadata["num_terminal_nodes"] = session.solver.TerminalNodeIds().size();
  metadata["root_board"] = session.setup->Board().ToString();
  metadata["root_street"] = static_cast<int>(session.setup->Street());
  metadata["root_pot"] = session.setup->Pot();
  metadata["root_current_player"] = session.setup->CurrentPlayer();
  metadata["root_last_aggressor"] = session.setup->LastAggressor();
  return metadata;
}

py::list PokerSolveLog(PokerSolveSession &session) {
  py::list checkpoints;
  for (const PokerCfrSolver::SolveResult::Checkpoint &checkpoint :
       session.result.checkpoints) {
    py::dict item;
    item["iteration"] = checkpoint.iteration;
    item["exploitability"] = checkpoint.exploitability;
    item["current_ev"] = checkpoint.current_ev;
    item["best_response_ev"] = checkpoint.best_response_ev;
    item["converged"] = checkpoint.converged;
    checkpoints.append(item);
  }
  return checkpoints;
}

} // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "C++ CFR game, algorithm, and pybind interface";
  m.def("build_game", &BuildGame, py::arg("config"));
  m.def("solve", &Solve, py::arg("game_config"), py::arg("solver_config"));
  m.def("policy_value", &PolicyValue, py::arg("game_config"),
        py::arg("strategy"));
  m.def("nash_exploitability", &NashExploitability, py::arg("game_config"),
        py::arg("strategy"));
  py::class_<PokerSolveSession, std::shared_ptr<PokerSolveSession>>(
      m, "PokerSolveSession")
      .def("node_strategy_matrix", &PokerNodeStrategyMatrix,
           py::arg("action_history") = py::str(""),
           py::arg("player") = py::none())
      .def("metadata", &PokerSolveMetadata)
      .def("solve_log", &PokerSolveLog)
      .def_property_readonly("iterations",
                             [](const PokerSolveSession &session) {
                               return session.result.iterations;
                             })
      .def_property_readonly("exploitability",
                             [](const PokerSolveSession &session) {
                               return session.result.exploitability;
                             })
      .def_property_readonly("num_nodes", [](const PokerSolveSession &session) {
        return session.solver.Tree().NumNodes();
      });
  m.def("solve_poker", &SolvePoker, py::arg("spot_config"),
        py::arg("solver_config") = py::dict());
}
