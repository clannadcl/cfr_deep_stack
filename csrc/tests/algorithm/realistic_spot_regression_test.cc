#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

#include "algorithm/best_response_calculator.h"
#include "algorithm/poker_cfr_solver.h"
#include "game/poker/action.h"
#include "game/poker/game_basic.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"
#include "game/poker/poker_tree.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/tree_abstracted_bets.h"

namespace {

constexpr int kProfileIterations = 3;

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

float SumWeights(const std::vector<float>& values) {
  float sum = 0.0f;
  for (float value : values) {
    sum += value;
  }
  return sum;
}

bool IsFiniteVector(const std::vector<float>& values) {
  for (float value : values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

struct SpotConfig {
  std::string name;
  std::string board_cards;
  std::string round;
  std::array<float, 2> stacks;
  std::array<float, 2> bet_total;
  std::array<float, 2> bet_current_round;
  float common_pot = 0.0f;
  float min_bet_increment = 0.0f;
  int previous_street_aggressor = -1;
  int current_player = 0;
  int raise_count = 0;
  bool rake_enabled = false;
  double rake_percentage = 0.0;
  double rake_cap = 0.0;
  std::vector<fisher::game::poker::Action> root_action_history;
  std::vector<std::string> ranges;
  fisher::game::poker::AbstractedBetStringConfig bets;
  fisher::game::poker::AbstractedDonkBetStringConfig donk_bets;
};

void SkipWhitespace(const std::string& text, std::size_t* index) {
  while (*index < text.size() &&
         std::isspace(static_cast<unsigned char>(text[*index]))) {
    ++(*index);
  }
}

std::string ParseJsonString(const std::string& text, std::size_t* index) {
  if (*index >= text.size() || text[*index] != '"') {
    throw std::runtime_error("expected JSON string");
  }
  ++(*index);

  std::string value;
  while (*index < text.size()) {
    const char current = text[*index];
    ++(*index);
    if (current == '"') {
      return value;
    }
    if (current == '\\') {
      if (*index >= text.size()) {
        throw std::runtime_error("unterminated JSON escape");
      }
      const char escaped = text[*index];
      ++(*index);
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          value.push_back(escaped);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          throw std::runtime_error("unsupported JSON escape");
      }
    } else {
      value.push_back(current);
    }
  }
  throw std::runtime_error("unterminated JSON string");
}

std::size_t FindJsonValue(const std::string& text, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const std::size_t key_index = text.find(needle);
  if (key_index == std::string::npos) {
    throw std::runtime_error("missing JSON key: " + key);
  }
  const std::size_t colon = text.find(':', key_index + needle.size());
  if (colon == std::string::npos) {
    throw std::runtime_error("missing JSON ':' for key: " + key);
  }
  std::size_t value_index = colon + 1;
  SkipWhitespace(text, &value_index);
  return value_index;
}

std::string ExtractBracketedValue(const std::string& text,
                                  const std::string& key, char open,
                                  char close) {
  const std::size_t begin = FindJsonValue(text, key);
  if (begin >= text.size() || text[begin] != open) {
    throw std::runtime_error("unexpected JSON value for key: " + key);
  }

  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = begin; index < text.size(); ++index) {
    const char current = text[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        in_string = false;
      }
      continue;
    }
    if (current == '"') {
      in_string = true;
      continue;
    }
    if (current == open) {
      ++depth;
    } else if (current == close) {
      --depth;
      if (depth == 0) {
        return text.substr(begin, index - begin + 1);
      }
    }
  }
  throw std::runtime_error("unterminated JSON bracketed value: " + key);
}

std::vector<std::string> ParseJsonStringArray(const std::string& text) {
  std::size_t index = 0;
  SkipWhitespace(text, &index);
  if (index >= text.size() || text[index] != '[') {
    throw std::runtime_error("expected JSON array");
  }
  ++index;

  std::vector<std::string> values;
  SkipWhitespace(text, &index);
  if (index < text.size() && text[index] == ']') {
    ++index;
  } else {
    while (index < text.size()) {
      SkipWhitespace(text, &index);
      values.push_back(ParseJsonString(text, &index));
      SkipWhitespace(text, &index);
      if (index < text.size() && text[index] == ',') {
        ++index;
        continue;
      }
      if (index < text.size() && text[index] == ']') {
        ++index;
        break;
      }
      throw std::runtime_error("expected JSON array delimiter");
    }
  }

  SkipWhitespace(text, &index);
  if (index != text.size()) {
    throw std::runtime_error("unexpected JSON trailing data");
  }
  return values;
}

std::string ExtractJsonStringField(const std::string& text,
                                   const std::string& key) {
  std::size_t index = FindJsonValue(text, key);
  return ParseJsonString(text, &index);
}

float ExtractJsonFloatField(const std::string& text, const std::string& key) {
  const std::size_t begin = FindJsonValue(text, key);
  std::size_t parsed = 0;
  try {
    return std::stof(text.substr(begin), &parsed);
  } catch (const std::exception&) {
    throw std::runtime_error("invalid JSON float for key: " + key);
  }
}

int ExtractJsonIntField(const std::string& text, const std::string& key) {
  const std::size_t begin = FindJsonValue(text, key);
  std::size_t parsed = 0;
  try {
    return std::stoi(text.substr(begin), &parsed);
  } catch (const std::exception&) {
    throw std::runtime_error("invalid JSON integer for key: " + key);
  }
}

bool ExtractJsonBoolField(const std::string& text, const std::string& key) {
  const std::size_t begin = FindJsonValue(text, key);
  if (text.compare(begin, 4, "true") == 0) {
    return true;
  }
  if (text.compare(begin, 5, "false") == 0) {
    return false;
  }
  throw std::runtime_error("invalid JSON bool for key: " + key);
}

std::array<float, 2> ExtractJsonFloatArray2(const std::string& text,
                                            const std::string& key) {
  const std::string array_text = ExtractBracketedValue(text, key, '[', ']');
  std::size_t index = 1;
  SkipWhitespace(array_text, &index);
  std::size_t first_parsed = 0;
  float first = std::stof(array_text.substr(index), &first_parsed);
  index += first_parsed;
  SkipWhitespace(array_text, &index);
  if (index >= array_text.size() || array_text[index] != ',') {
    throw std::runtime_error("expected 2-number array for key: " + key);
  }
  ++index;
  SkipWhitespace(array_text, &index);
  std::size_t second_parsed = 0;
  float second = std::stof(array_text.substr(index), &second_parsed);
  index += second_parsed;
  SkipWhitespace(array_text, &index);
  if (index >= array_text.size() || array_text[index] != ']') {
    throw std::runtime_error("expected 2-number array for key: " + key);
  }
  return {first, second};
}

std::vector<std::string> ExtractJsonStringArrayField(
    const std::string& text, const std::string& key) {
  return ParseJsonStringArray(ExtractBracketedValue(text, key, '[', ']'));
}

fisher::game::poker::AbstractedBetStringConfig ExtractJsonStringMatrixField(
    const std::string& text, const std::string& key) {
  const std::string matrix_text = ExtractBracketedValue(text, key, '[', ']');
  std::size_t index = 1;
  fisher::game::poker::AbstractedBetStringConfig matrix;
  while (true) {
    SkipWhitespace(matrix_text, &index);
    if (index < matrix_text.size() && matrix_text[index] == ']') {
      return matrix;
    }
    if (index >= matrix_text.size() || matrix_text[index] != '[') {
      throw std::runtime_error("expected nested string array for key: " + key);
    }
    const std::size_t row_begin = index;
    int depth = 0;
    bool in_string = false;
    for (; index < matrix_text.size(); ++index) {
      const char current = matrix_text[index];
      if (current == '"' && (index == 0 || matrix_text[index - 1] != '\\')) {
        in_string = !in_string;
      }
      if (in_string) {
        continue;
      }
      if (current == '[') {
        ++depth;
      } else if (current == ']') {
        --depth;
        if (depth == 0) {
          ++index;
          break;
        }
      }
    }
    matrix.push_back(ParseJsonStringArray(
        matrix_text.substr(row_begin, index - row_begin)));
    SkipWhitespace(matrix_text, &index);
    if (index < matrix_text.size() && matrix_text[index] == ',') {
      ++index;
    }
  }
}

fisher::game::poker::Action ParseAction(const std::string& action) {
  using fisher::game::poker::Action;
  if (action == "x") return Action::Check();
  if (action == "f") return Action::Fold();
  if (action == "c") return Action::Call();
  if (action == "C") return Action::Chance();
  if (action.size() > 1 && action[0] == 'b') {
    return Action::Bet(std::stof(action.substr(1)));
  }
  throw std::runtime_error("unsupported action fixture value");
}

std::vector<fisher::game::poker::Action> ParseActionHistory(
    const std::vector<std::string>& actions) {
  std::vector<fisher::game::poker::Action> parsed;
  parsed.reserve(actions.size());
  for (const std::string& action : actions) {
    parsed.push_back(ParseAction(action));
  }
  return parsed;
}

SpotConfig LoadTestCaseConfig(const std::string& file_name) {
  const std::string path =
      std::string(FISHER_TEST_DATA_DIR) + "/" + file_name;
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open test case fixture: " + file_name);
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string text = buffer.str();

  SpotConfig config;
  config.name = ExtractJsonStringField(text, "name");
  config.board_cards = ExtractJsonStringField(text, "board_cards");
  config.round = ExtractJsonStringField(text, "round");
  config.stacks = ExtractJsonFloatArray2(text, "stacks");
  config.bet_total = ExtractJsonFloatArray2(text, "bet_total");
  config.bet_current_round = ExtractJsonFloatArray2(text, "bet_current_round");
  config.common_pot = ExtractJsonFloatField(text, "common_pot");
  config.min_bet_increment = ExtractJsonFloatField(text, "min_bet_increment");
  config.previous_street_aggressor =
      ExtractJsonIntField(text, "previous_street_aggressor");
  config.current_player = ExtractJsonIntField(text, "current_player");
  config.raise_count = ExtractJsonIntField(text, "raise_count");
  config.rake_enabled = ExtractJsonBoolField(text, "enabled");
  config.rake_percentage = ExtractJsonFloatField(text, "percentage");
  config.rake_cap = ExtractJsonFloatField(text, "cap");
  config.root_action_history =
      ParseActionHistory(ExtractJsonStringArrayField(text,
                                                     "root_action_history"));
  config.ranges = ExtractJsonStringArrayField(text, "ranges");
  config.bets = ExtractJsonStringMatrixField(text, "bets");
  config.donk_bets = ExtractJsonStringArrayField(text, "donk_bets");
  if (config.ranges.size() != 2) {
    throw std::runtime_error("turn test case fixture must contain 2 ranges");
  }
  return config;
}

double MillisecondsSince(std::chrono::steady_clock::time_point begin,
                         std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

void PrintHeroProfile(int iteration, int player,
                      const fisher::algorithm::PokerCfrSolver::
                          HeroPassProfile& profile) {
  std::cout << "iteration=" << iteration << " player=" << player
            << " total_ms=" << profile.total_ms
            << " init_reach_ms=" << profile.initialize_root_reach_ms
            << " forward_ms=" << profile.forward_reach_ms
            << " terminal_cfv_ms=" << profile.terminal_cfv_ms
            << " backward_ms=" << profile.backward_update_ms << '\n';
}

struct ProfileAccumulator {
  int passes = 0;
  double initialize_root_reach_ms = 0.0;
  double forward_reach_ms = 0.0;
  double terminal_cfv_ms = 0.0;
  double backward_update_ms = 0.0;
  double total_ms = 0.0;

  void Add(const fisher::algorithm::PokerCfrSolver::HeroPassProfile& profile) {
    ++passes;
    initialize_root_reach_ms += profile.initialize_root_reach_ms;
    forward_reach_ms += profile.forward_reach_ms;
    terminal_cfv_ms += profile.terminal_cfv_ms;
    backward_update_ms += profile.backward_update_ms;
    total_ms += profile.total_ms;
  }

  void Reset() {
    passes = 0;
    initialize_root_reach_ms = 0.0;
    forward_reach_ms = 0.0;
    terminal_cfv_ms = 0.0;
    backward_update_ms = 0.0;
    total_ms = 0.0;
  }
};

void PrintProfileWindow(const std::string& prefix, int iteration_begin,
                        int iteration_end, int player,
                        const ProfileAccumulator& profile) {
  if (profile.passes == 0) {
    return;
  }
  const double passes = static_cast<double>(profile.passes);
  std::cout << prefix
            << " iteration_begin=" << iteration_begin
            << " iteration_end=" << iteration_end
            << " player=" << player
            << " passes=" << profile.passes
            << " total_ms=" << profile.total_ms
            << " total_avg_ms=" << profile.total_ms / passes
            << " init_reach_ms=" << profile.initialize_root_reach_ms
            << " init_reach_avg_ms="
            << profile.initialize_root_reach_ms / passes
            << " forward_ms=" << profile.forward_reach_ms
            << " forward_avg_ms=" << profile.forward_reach_ms / passes
            << " terminal_cfv_ms=" << profile.terminal_cfv_ms
            << " terminal_cfv_avg_ms=" << profile.terminal_cfv_ms / passes
            << " backward_ms=" << profile.backward_update_ms
            << " backward_avg_ms=" << profile.backward_update_ms / passes
            << '\n';
}

}  // namespace

int main() {
  using fisher::algorithm::PokerCfrSolver;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsomorphicMappingTable;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerHand;
  using fisher::game::poker::PokerRound;
  using fisher::game::poker::RakeConfig;
  using fisher::game::poker::SubgameSetup;
  using fisher::game::poker::TreeAbstractedBets;

  std::cout << std::fixed << std::setprecision(3);
  const auto load_begin = std::chrono::steady_clock::now();
  SpotConfig fixture = LoadTestCaseConfig("turn_test_case.json");
  const auto load_end = std::chrono::steady_clock::now();

  const auto setup_begin = std::chrono::steady_clock::now();
  GameBasic game(RakeConfig{/*enabled=*/fixture.rake_enabled,
                            /*percentage=*/fixture.rake_percentage,
                            /*cap=*/fixture.rake_cap});
  TreeAbstractedBets abstracted_bets(fixture.bets, fixture.donk_bets);
  auto setup = std::make_shared<SubgameSetup>(SubgameSetup::Args(
      PokerCards(fixture.board_cards), fixture.common_pot, fixture.stacks,
      fixture.bet_total, fixture.bet_current_round, fixture.current_player,
      fixture.previous_street_aggressor, fixture.raise_count,
      fixture.root_action_history, fixture.ranges, abstracted_bets, game,
      /*bet_rounding=*/fixture.min_bet_increment,
      /*min_raise_size=*/fixture.min_bet_increment));
  const auto setup_end = std::chrono::steady_clock::now();

  Expect(fixture.name == "turn_btn_vs_bb_qsth4dqd_srp_xx",
         "fixture name mismatch");
  Expect(fixture.round == "turn", "fixture round mismatch");
  Expect(setup->Board().ToString() == fixture.board_cards, "board mismatch");
  Expect(setup->Street() == PokerRound::kTurn, "street mismatch");
  Expect(setup->Pot() == fixture.common_pot, "pot mismatch");
  Expect(setup->Stacks()[0] == fixture.stacks[0],
         "player 0 stack mismatch");
  Expect(setup->Stacks()[1] == fixture.stacks[1],
         "player 1 stack mismatch");
  Expect(setup->CurrentPlayer() == fixture.current_player,
         "turn first actor should be BB");
  Expect(setup->LastAggressor() == fixture.previous_street_aggressor,
         "previous street aggressor mismatch");
  Expect(setup->BasicGame().Rake().enabled, "rake should be enabled");
  Expect(setup->BasicGame().Rake().percentage == fixture.rake_percentage,
         "rake percentage mismatch");
  Expect(setup->BasicGame().Rake().cap == fixture.rake_cap,
         "rake cap mismatch");

  const auto& belief = setup->RootBelief().Belief();
  Expect(belief.size() == 2, "root belief should contain two players");
  Expect(SumWeights(belief[0]) > 0.0f, "BB range should be non-empty");
  Expect(SumWeights(belief[1]) > 0.0f, "BTN range should be non-empty");

  for (int player = 0; player < GameBasic::kNumPlayers; ++player) {
    for (int hand_index = 0; hand_index < GameBasic::kNumHands; ++hand_index) {
      const float weight =
          belief[static_cast<std::size_t>(player)]
                [static_cast<std::size_t>(hand_index)];
      if (game.HandFromIndex(hand_index).HasCollision(setup->Board())) {
        Expect(weight == 0.0f, "board-blocked root hand should be zero");
      }
    }
  }

  const int bb_pair_index = game.HandIndex(PokerHand("2d2c"));
  const int btn_ace_index = game.HandIndex(PokerHand("AcKc"));
  Expect(belief[0][static_cast<std::size_t>(bb_pair_index)] > 0.0f,
         "known BB hand should be parsed");
  Expect(belief[1][static_cast<std::size_t>(btn_ace_index)] > 0.0f,
         "known BTN hand should be parsed");

  IsomorphicMappingTable table(setup->BasicGame(), setup->RootBelief());
  const auto& mapping = table.Get(setup->Board());
  Expect(mapping.NumIsoHands() > 0, "turn mapping should be non-empty");
  Expect(mapping.RawToIso(bb_pair_index) >= 0,
         "known unblocked hand should map to an iso hand");

  const auto solver_begin = std::chrono::steady_clock::now();
  PokerCfrSolver solver{PokerCfrSolver::Args(setup)};
  const auto solver_end = std::chrono::steady_clock::now();
  Expect(solver.Tree().NumNodes() > 1, "realistic spot should build a tree");
  Expect(!solver.TerminalNodeIds().empty(),
         "realistic spot should contain terminal nodes");

  std::cout << "fixture=" << fixture.name
            << " load_ms=" << MillisecondsSince(load_begin, load_end)
            << " setup_ms=" << MillisecondsSince(setup_begin, setup_end)
            << " solver_construct_ms="
            << MillisecondsSince(solver_begin, solver_end)
            << " threads=" << solver.NumThreads()
            << " nodes=" << solver.Tree().NumNodes()
            << " terminal_nodes=" << solver.TerminalNodeIds().size() << '\n';

  const auto iterations_begin = std::chrono::steady_clock::now();
  for (int iteration = 1; iteration <= kProfileIterations; ++iteration) {
    const auto player0_profile = solver.RunHeroPassProfiled(0);
    PrintHeroProfile(iteration, 0, player0_profile);
    const auto player1_profile = solver.RunHeroPassProfiled(1);
    PrintHeroProfile(iteration, 1, player1_profile);
  }
  const auto iterations_end = std::chrono::steady_clock::now();
  std::cout << "iterations=" << kProfileIterations
            << " total_iteration_ms="
            << MillisecondsSince(iterations_begin, iterations_end) << '\n';

  Expect(IsFiniteVector(solver.CurrentStrategyData()),
         "current strategy should remain finite");
  Expect(IsFiniteVector(solver.AverageStrategyData()),
         "average strategy should remain finite");

  const auto river_load_begin = std::chrono::steady_clock::now();
  SpotConfig river_fixture = LoadTestCaseConfig("river_test_case.json");
  const auto river_load_end = std::chrono::steady_clock::now();
  const auto river_setup_begin = std::chrono::steady_clock::now();
  GameBasic river_game(RakeConfig{/*enabled=*/river_fixture.rake_enabled,
                                  /*percentage=*/river_fixture.rake_percentage,
                                  /*cap=*/river_fixture.rake_cap});
  TreeAbstractedBets river_abstracted_bets(river_fixture.bets,
                                           river_fixture.donk_bets);
  auto river_setup = std::make_shared<SubgameSetup>(SubgameSetup::Args(
      PokerCards(river_fixture.board_cards), river_fixture.common_pot,
      river_fixture.stacks, river_fixture.bet_total,
      river_fixture.bet_current_round, river_fixture.current_player,
      river_fixture.previous_street_aggressor, river_fixture.raise_count,
      river_fixture.root_action_history, river_fixture.ranges,
      river_abstracted_bets, river_game,
      /*bet_rounding=*/river_fixture.min_bet_increment,
      /*min_raise_size=*/river_fixture.min_bet_increment));
  const auto river_setup_end = std::chrono::steady_clock::now();

  Expect(river_fixture.name == "river_co_vs_bb_kd8d4d4s4c_srp_xbc_xbc",
         "river fixture name mismatch");
  Expect(river_fixture.round == "river", "river fixture round mismatch");
  Expect(river_setup->Street() == PokerRound::kRiver,
         "river setup should be river");
  Expect(river_setup->Pot() == river_fixture.common_pot,
         "river pot mismatch");

  const auto river_solver_begin = std::chrono::steady_clock::now();
  PokerCfrSolver river_solver{PokerCfrSolver::Args(
      river_setup, /*num_threads=*/0, /*max_iterations=*/500,
      /*exploitability_check_interval=*/50,
      /*target_exploitability=*/-1.0f)};
  const auto river_solver_end = std::chrono::steady_clock::now();
  const float river_target = river_setup->Pot() * 0.001f;

  std::cout << "river_fixture=" << river_fixture.name
            << " load_ms="
            << MillisecondsSince(river_load_begin, river_load_end)
            << " setup_ms="
            << MillisecondsSince(river_setup_begin, river_setup_end)
            << " solver_construct_ms="
            << MillisecondsSince(river_solver_begin, river_solver_end)
            << " threads=" << river_solver.NumThreads()
            << " nodes=" << river_solver.Tree().NumNodes()
            << " terminal_nodes=" << river_solver.TerminalNodeIds().size()
            << " target_exploitability=" << river_target << '\n';

  int reached_iteration = -1;
  float last_exploitability = 0.0f;
  int river_profile_window_begin = 1;
  std::array<ProfileAccumulator, 2> river_profile_windows;
  const auto river_solve_begin = std::chrono::steady_clock::now();
  for (int iteration = 1; iteration <= 500; ++iteration) {
    river_profile_windows[0].Add(river_solver.RunHeroPassProfiled(0));
    river_profile_windows[1].Add(river_solver.RunHeroPassProfiled(1));
    if (iteration % 50 != 0) {
      continue;
    }
    PrintProfileWindow("river_profile_window", river_profile_window_begin,
                       iteration, 0, river_profile_windows[0]);
    PrintProfileWindow("river_profile_window", river_profile_window_begin,
                       iteration, 1, river_profile_windows[1]);
    river_profile_window_begin = iteration + 1;
    river_profile_windows[0].Reset();
    river_profile_windows[1].Reset();

    const auto check_begin = std::chrono::steady_clock::now();
    const fisher::algorithm::ExploitabilityResult result =
        fisher::algorithm::BestResponseCalculator(&river_solver).Compute();
    const auto check_end = std::chrono::steady_clock::now();
    last_exploitability = result.exploitability;
    std::cout << "river_iteration=" << iteration
              << " exploitability=" << result.exploitability
              << " target=" << river_target
              << " current_ev_p0=" << result.current_ev[0]
              << " current_ev_p1=" << result.current_ev[1]
              << " br_ev_p0=" << result.best_response_ev[0]
              << " br_ev_p1=" << result.best_response_ev[1]
              << " check_ms=" << MillisecondsSince(check_begin, check_end)
              << '\n';
    Expect(std::isfinite(result.exploitability),
           "river exploitability should be finite");
    if (result.exploitability <= river_target) {
      reached_iteration = iteration;
      break;
    }
  }
  const auto river_solve_end = std::chrono::steady_clock::now();
  std::cout << "river_solve_done"
            << " reached_iteration=" << reached_iteration
            << " last_exploitability=" << last_exploitability
            << " target=" << river_target
            << " total_ms="
            << MillisecondsSince(river_solve_begin, river_solve_end)
            << '\n';
  Expect(last_exploitability >= -1e-4f,
         "river exploitability should be non-negative");

  return 0;
}
