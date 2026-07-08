#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>

#include "algorithm/cfr.h"
#include "game/kuhn.h"

namespace py = pybind11;

namespace {

fisher::game::KuhnGameConfig ParseGameConfig(py::dict config) {
  fisher::game::KuhnGameConfig game_config;
  if (config.contains("game")) game_config.name = config["game"].cast<std::string>();
  if (config.contains("name")) game_config.name = config["name"].cast<std::string>();
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

std::array<double, 2> PolicyValue(
    py::dict game_config, const fisher::algorithm::Strategy& strategy) {
  fisher::game::KuhnPokerGame game(ParseGameConfig(game_config));
  return fisher::algorithm::PolicyValue(game, strategy);
}

double NashExploitability(
    py::dict game_config, const fisher::algorithm::Strategy& strategy) {
  fisher::game::KuhnPokerGame game(ParseGameConfig(game_config));
  return fisher::algorithm::NashExploitability(game, strategy);
}

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "C++ CFR game, algorithm, and pybind interface";
  m.def("build_game", &BuildGame, py::arg("config"));
  m.def("solve", &Solve, py::arg("game_config"), py::arg("solver_config"));
  m.def("policy_value", &PolicyValue, py::arg("game_config"), py::arg("strategy"));
  m.def("nash_exploitability", &NashExploitability, py::arg("game_config"),
        py::arg("strategy"));
}
