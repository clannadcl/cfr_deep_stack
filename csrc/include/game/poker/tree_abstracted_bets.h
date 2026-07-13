#pragma once

#include <optional>
#include <string>
#include <vector>

#include "game/poker/abstracted_action.h"
#include "game/poker/poker_cards_isomorphic_index.h"

namespace fisher::game::poker {

using AbstractedBetConfig = std::vector<std::vector<AbstractedAction>>;
using AbstractedBetStringConfig = std::vector<std::vector<std::string>>;
using AbstractedDonkBetConfig = std::vector<AbstractedAction>;
using AbstractedDonkBetStringConfig = std::vector<std::string>;

class TreeAbstractedBets {
 public:
  struct Args {
    AbstractedBetConfig default_bets = {
        {AbstractedAction::BetPercent(33.0f),
         AbstractedAction::BetPercent(66.0f),
         AbstractedAction::BetPercent(125.0f),
         AbstractedAction::AllIn()},
        {AbstractedAction::BetPercent(50.0f),
         AbstractedAction::BetPercent(100.0f),
         AbstractedAction::AllIn()},
    };
    AbstractedDonkBetConfig default_donk_bets = {
        AbstractedAction::BetPercent(33.0f),
    };
    std::optional<AbstractedBetConfig> preflop_bets;
    std::optional<AbstractedBetConfig> flop_bets;
    std::optional<AbstractedBetConfig> turn_bets;
    std::optional<AbstractedBetConfig> river_bets;
    std::optional<AbstractedDonkBetConfig> preflop_donk_bets;
    std::optional<AbstractedDonkBetConfig> flop_donk_bets;
    std::optional<AbstractedDonkBetConfig> turn_donk_bets;
    std::optional<AbstractedDonkBetConfig> river_donk_bets;
    float bet_to_allin_threshold = 75.0f;
    float add_allin_threshold = 250.0f;
    float merging_threshold = 0.1f;
  };

  explicit TreeAbstractedBets(const Args& args);
  explicit TreeAbstractedBets(const AbstractedBetConfig& default_bets);
  explicit TreeAbstractedBets(const AbstractedBetStringConfig& default_bets);
  TreeAbstractedBets(const AbstractedBetConfig& default_bets,
                     const AbstractedDonkBetConfig& default_donk_bets);
  TreeAbstractedBets(const AbstractedBetStringConfig& default_bets,
                     const AbstractedDonkBetStringConfig& default_donk_bets);

  const std::vector<AbstractedAction>& GetBets(PokerRound round,
                                               int raise_count) const;
  const AbstractedDonkBetConfig& GetDonkBets(PokerRound round) const;
  float BetToAllInThreshold() const;
  float AddAllInThreshold() const;
  float MergingThreshold() const;
  void SetStreetBets(PokerRound round, const AbstractedBetConfig& bets);
  void SetStreetBets(PokerRound round, const AbstractedBetStringConfig& bets);
  void SetDonkBets(PokerRound round, const AbstractedDonkBetConfig& bets);
  void SetDonkBets(PokerRound round,
                   const AbstractedDonkBetStringConfig& bets);
  void SetBetToAllInThreshold(float threshold);
  void SetAddAllInThreshold(float threshold);
  void SetMergingThreshold(float threshold);

 private:
  static AbstractedBetConfig ParseStringConfig(
      const AbstractedBetStringConfig& config);
  static AbstractedDonkBetConfig ParseStringDonkConfig(
      const AbstractedDonkBetStringConfig& config);
  static AbstractedDonkBetConfig DefaultDonkBets(
      const AbstractedBetConfig& config);
  const AbstractedBetConfig& StreetBets(PokerRound round) const;
  static void ValidateConfig(const AbstractedBetConfig& config);
  static void ValidateDonkConfig(const AbstractedDonkBetConfig& config);

  AbstractedBetConfig preflop_bets_;
  AbstractedBetConfig flop_bets_;
  AbstractedBetConfig turn_bets_;
  AbstractedBetConfig river_bets_;
  AbstractedDonkBetConfig preflop_donk_bets_;
  AbstractedDonkBetConfig flop_donk_bets_;
  AbstractedDonkBetConfig turn_donk_bets_;
  AbstractedDonkBetConfig river_donk_bets_;
  float bet_to_allin_threshold_ = 75.0f;
  float add_allin_threshold_ = 250.0f;
  float merging_threshold_ = 0.1f;
};

}  // namespace fisher::game::poker
