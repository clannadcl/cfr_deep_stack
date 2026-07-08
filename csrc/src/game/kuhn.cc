#include "game/kuhn.h"

#include <algorithm>
#include <stdexcept>

namespace fisher::game {

int KuhnState::CurrentPlayer() const {
  if (game_over) return kTerminalPlayer;
  if (cards.size() < 2) return kChancePlayer;
  return next_player;
}

bool KuhnState::IsTerminal() const { return game_over; }

bool KuhnState::IsChanceNode() const { return CurrentPlayer() == kChancePlayer; }

std::vector<std::pair<int, double>> KuhnState::ChanceOutcomes() const {
  std::vector<std::pair<int, double>> outcomes;
  for (int card = 0; card < 3; ++card) {
    if (std::find(cards.begin(), cards.end(), card) == cards.end()) {
      outcomes.push_back({card, 0.0});
    }
  }
  const double probability = 1.0 / static_cast<double>(outcomes.size());
  for (auto& outcome : outcomes) outcome.second = probability;
  return outcomes;
}

std::vector<int> KuhnState::LegalActions() const { return {0, 1}; }

KuhnState KuhnState::Child(int action) const {
  KuhnState child = *this;
  child.ApplyAction(action);
  return child;
}

void KuhnState::ApplyAction(int action) {
  if (IsChanceNode()) {
    cards.push_back(action);
    return;
  }

  bets.push_back(action);
  if (action == 1) pot[next_player] += 1.0;
  next_player = 1 - next_player;

  if (std::min(pot[0], pot[1]) == 2.0 ||
      (bets.size() == 2 && action == 0) || bets.size() == 3) {
    game_over = true;
  }
}

std::string KuhnState::InformationStateString(int player) const {
  if (cards.size() <= static_cast<size_t>(player)) {
    return "p" + std::to_string(player) + ":?";
  }
  std::string info_state = std::to_string(cards[player]);
  for (int action : bets) info_state.push_back(action == 0 ? 'p' : 'b');
  return info_state;
}

std::array<double, 2> KuhnState::Returns() const {
  if (!game_over) return {0.0, 0.0};
  const double winnings = std::min(pot[0], pot[1]);
  if (pot[0] > pot[1]) return {winnings, -winnings};
  if (pot[0] < pot[1]) return {-winnings, winnings};
  if (cards[0] > cards[1]) return {winnings, -winnings};
  return {-winnings, winnings};
}

KuhnPokerGame::KuhnPokerGame(KuhnGameConfig config)
    : config_(std::move(config)) {
  if (config_.name != "kuhn_poker") {
    throw std::invalid_argument("Only kuhn_poker is supported");
  }
}

const std::string& KuhnPokerGame::Name() const { return config_.name; }

int KuhnPokerGame::NumPlayers() const { return kKuhnNumPlayers; }

KuhnState KuhnPokerGame::NewInitialState() const { return KuhnState(); }

}  // namespace fisher::game
