#include "game/poker/tree_abstracted_bets.h"

#include <stdexcept>
#include <string>

namespace fisher::game::poker {
namespace {

bool EndsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

float ParseFloat(const std::string& text) {
  if (text.empty()) {
    throw std::invalid_argument("Abstracted bet amount is empty");
  }

  std::size_t parsed = 0;
  float value = 0.0f;
  try {
    value = std::stof(text, &parsed);
  } catch (const std::exception&) {
    throw std::invalid_argument("Invalid abstracted bet amount");
  }
  if (parsed != text.size()) {
    throw std::invalid_argument("Invalid abstracted bet amount");
  }
  return value;
}

AbstractedAction ParseAbstractedBet(const std::string& text) {
  if (text == "allin") {
    return AbstractedAction::AllIn();
  }
  if (EndsWith(text, "%")) {
    return AbstractedAction::BetPercent(
        ParseFloat(text.substr(0, text.size() - 1)));
  }
  if (EndsWith(text, "bb")) {
    return AbstractedAction::BetBigBlind(
        ParseFloat(text.substr(0, text.size() - 2)));
  }
  throw std::invalid_argument("Unsupported abstracted bet string");
}

bool IsSupportedBetAction(const AbstractedAction& action) {
  const std::string& value = action.Value();
  return value == "allin" || value.rfind("percent:", 0) == 0 ||
         value.rfind("bb:", 0) == 0;
}

}  // namespace

TreeAbstractedBets::TreeAbstractedBets(const Args& args)
    : TreeAbstractedBets(args.default_bets, args.default_donk_bets) {
  if (args.preflop_bets.has_value()) {
    SetStreetBets(PokerRound::kPreflop, args.preflop_bets.value());
  }
  if (args.flop_bets.has_value()) {
    SetStreetBets(PokerRound::kFlop, args.flop_bets.value());
  }
  if (args.turn_bets.has_value()) {
    SetStreetBets(PokerRound::kTurn, args.turn_bets.value());
  }
  if (args.river_bets.has_value()) {
    SetStreetBets(PokerRound::kRiver, args.river_bets.value());
  }
  if (args.preflop_donk_bets.has_value()) {
    SetDonkBets(PokerRound::kPreflop, args.preflop_donk_bets.value());
  }
  if (args.flop_donk_bets.has_value()) {
    SetDonkBets(PokerRound::kFlop, args.flop_donk_bets.value());
  }
  if (args.turn_donk_bets.has_value()) {
    SetDonkBets(PokerRound::kTurn, args.turn_donk_bets.value());
  }
  if (args.river_donk_bets.has_value()) {
    SetDonkBets(PokerRound::kRiver, args.river_donk_bets.value());
  }
  SetBetToAllInThreshold(args.bet_to_allin_threshold);
  SetAddAllInThreshold(args.add_allin_threshold);
}

TreeAbstractedBets::TreeAbstractedBets(
    const AbstractedBetConfig& default_bets)
    : TreeAbstractedBets(default_bets, DefaultDonkBets(default_bets)) {}

TreeAbstractedBets::TreeAbstractedBets(
    const AbstractedBetStringConfig& default_bets)
    : TreeAbstractedBets(ParseStringConfig(default_bets)) {}

TreeAbstractedBets::TreeAbstractedBets(
    const AbstractedBetConfig& default_bets,
    const AbstractedDonkBetConfig& default_donk_bets)
    : preflop_bets_(default_bets),
      flop_bets_(default_bets),
      turn_bets_(default_bets),
      river_bets_(default_bets),
      preflop_donk_bets_(default_donk_bets),
      flop_donk_bets_(default_donk_bets),
      turn_donk_bets_(default_donk_bets),
      river_donk_bets_(default_donk_bets) {
  ValidateConfig(default_bets);
  ValidateDonkConfig(default_donk_bets);
}

TreeAbstractedBets::TreeAbstractedBets(
    const AbstractedBetStringConfig& default_bets,
    const AbstractedDonkBetStringConfig& default_donk_bets)
    : TreeAbstractedBets(ParseStringConfig(default_bets),
                         ParseStringDonkConfig(default_donk_bets)) {}

const AbstractedBetConfig& TreeAbstractedBets::StreetBets(
    PokerRound round) const {
  switch (round) {
    case PokerRound::kPreflop:
      return preflop_bets_;
    case PokerRound::kFlop:
      return flop_bets_;
    case PokerRound::kTurn:
      return turn_bets_;
    case PokerRound::kRiver:
      return river_bets_;
  }
  throw std::invalid_argument("Invalid poker round");
}

const std::vector<AbstractedAction>& TreeAbstractedBets::GetBets(
    PokerRound round, int raise_count) const {
  if (raise_count < 0) {
    throw std::invalid_argument("Raise count cannot be negative");
  }

  const AbstractedBetConfig& street_bets = StreetBets(round);
  const int config_index =
      raise_count < static_cast<int>(street_bets.size())
          ? raise_count
          : static_cast<int>(street_bets.size()) - 1;
  return street_bets[static_cast<std::size_t>(config_index)];
}

const AbstractedDonkBetConfig& TreeAbstractedBets::GetDonkBets(
    PokerRound round) const {
  switch (round) {
    case PokerRound::kPreflop:
      return preflop_donk_bets_;
    case PokerRound::kFlop:
      return flop_donk_bets_;
    case PokerRound::kTurn:
      return turn_donk_bets_;
    case PokerRound::kRiver:
      return river_donk_bets_;
  }
  throw std::invalid_argument("Invalid poker round");
}

float TreeAbstractedBets::BetToAllInThreshold() const {
  return bet_to_allin_threshold_;
}

float TreeAbstractedBets::AddAllInThreshold() const {
  return add_allin_threshold_;
}

void TreeAbstractedBets::SetStreetBets(PokerRound round,
                                       const AbstractedBetConfig& bets) {
  ValidateConfig(bets);
  switch (round) {
    case PokerRound::kPreflop:
      preflop_bets_ = bets;
      return;
    case PokerRound::kFlop:
      flop_bets_ = bets;
      return;
    case PokerRound::kTurn:
      turn_bets_ = bets;
      return;
    case PokerRound::kRiver:
      river_bets_ = bets;
      return;
  }
  throw std::invalid_argument("Invalid poker round");
}

void TreeAbstractedBets::SetStreetBets(
    PokerRound round, const AbstractedBetStringConfig& bets) {
  SetStreetBets(round, ParseStringConfig(bets));
}

void TreeAbstractedBets::SetDonkBets(
    PokerRound round, const AbstractedDonkBetConfig& bets) {
  ValidateDonkConfig(bets);
  switch (round) {
    case PokerRound::kPreflop:
      preflop_donk_bets_ = bets;
      return;
    case PokerRound::kFlop:
      flop_donk_bets_ = bets;
      return;
    case PokerRound::kTurn:
      turn_donk_bets_ = bets;
      return;
    case PokerRound::kRiver:
      river_donk_bets_ = bets;
      return;
  }
  throw std::invalid_argument("Invalid poker round");
}

void TreeAbstractedBets::SetDonkBets(
    PokerRound round, const AbstractedDonkBetStringConfig& bets) {
  SetDonkBets(round, ParseStringDonkConfig(bets));
}

void TreeAbstractedBets::SetBetToAllInThreshold(float threshold) {
  if (threshold < 0.0f) {
    throw std::invalid_argument("Bet-to-allin threshold cannot be negative");
  }
  bet_to_allin_threshold_ = threshold;
}

void TreeAbstractedBets::SetAddAllInThreshold(float threshold) {
  if (threshold < 0.0f) {
    throw std::invalid_argument("Add-allin threshold cannot be negative");
  }
  add_allin_threshold_ = threshold;
}

AbstractedBetConfig TreeAbstractedBets::ParseStringConfig(
    const AbstractedBetStringConfig& config) {
  AbstractedBetConfig parsed_config;
  parsed_config.reserve(config.size());
  for (const std::vector<std::string>& round_bets : config) {
    std::vector<AbstractedAction> parsed_round_bets;
    parsed_round_bets.reserve(round_bets.size());
    for (const std::string& bet : round_bets) {
      parsed_round_bets.push_back(ParseAbstractedBet(bet));
    }
    parsed_config.push_back(parsed_round_bets);
  }
  return parsed_config;
}

AbstractedDonkBetConfig TreeAbstractedBets::ParseStringDonkConfig(
    const AbstractedDonkBetStringConfig& config) {
  AbstractedDonkBetConfig parsed_config;
  parsed_config.reserve(config.size());
  for (const std::string& bet : config) {
    parsed_config.push_back(ParseAbstractedBet(bet));
  }
  return parsed_config;
}

AbstractedDonkBetConfig TreeAbstractedBets::DefaultDonkBets(
    const AbstractedBetConfig& config) {
  ValidateConfig(config);
  return config.front();
}

void TreeAbstractedBets::ValidateConfig(const AbstractedBetConfig& config) {
  if (config.empty()) {
    throw std::invalid_argument("Tree abstracted bets config cannot be empty");
  }

  for (const std::vector<AbstractedAction>& raise_bets : config) {
    if (raise_bets.empty()) {
      throw std::invalid_argument(
          "Tree abstracted bets raise config cannot be empty");
    }
    for (const AbstractedAction& action : raise_bets) {
      if (!IsSupportedBetAction(action)) {
        throw std::invalid_argument(
            "Tree abstracted bets only supports bet/allin actions");
      }
    }
  }
}

void TreeAbstractedBets::ValidateDonkConfig(
    const AbstractedDonkBetConfig& config) {
  if (config.empty()) {
    throw std::invalid_argument("Tree abstracted donk bets cannot be empty");
  }
  for (const AbstractedAction& action : config) {
    if (!IsSupportedBetAction(action)) {
      throw std::invalid_argument(
          "Tree abstracted donk bets only supports bet/allin actions");
    }
  }
}

}  // namespace fisher::game::poker
