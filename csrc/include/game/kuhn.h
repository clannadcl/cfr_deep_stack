#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace fisher::game {

constexpr int kChancePlayer = -1;
constexpr int kTerminalPlayer = -2;
constexpr int kKuhnNumPlayers = 2;

struct KuhnGameConfig {
  std::string name = "kuhn_poker";
};

struct KuhnState {
  std::vector<int> cards;
  std::vector<int> bets;
  std::array<double, 2> pot{1.0, 1.0};
  bool game_over = false;
  int next_player = 0;

  int CurrentPlayer() const;
  bool IsTerminal() const;
  bool IsChanceNode() const;
  std::vector<std::pair<int, double>> ChanceOutcomes() const;
  std::vector<int> LegalActions() const;
  KuhnState Child(int action) const;
  void ApplyAction(int action);
  std::string InformationStateString(int player) const;
  std::array<double, 2> Returns() const;
};

class KuhnPokerGame {
 public:
  explicit KuhnPokerGame(KuhnGameConfig config = {});

  const std::string& Name() const;
  int NumPlayers() const;
  KuhnState NewInitialState() const;

 private:
  KuhnGameConfig config_;
};

}  // namespace fisher::game
