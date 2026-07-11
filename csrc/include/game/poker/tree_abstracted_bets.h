#pragma once

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
  explicit TreeAbstractedBets(const AbstractedBetConfig& default_bets);
  explicit TreeAbstractedBets(const AbstractedBetStringConfig& default_bets);
  TreeAbstractedBets(const AbstractedBetConfig& default_bets,
                     const AbstractedDonkBetConfig& default_donk_bets);
  TreeAbstractedBets(const AbstractedBetStringConfig& default_bets,
                     const AbstractedDonkBetStringConfig& default_donk_bets);

  const std::vector<AbstractedAction>& GetBets(PokerRound round,
                                               int raise_count) const;
  const AbstractedDonkBetConfig& GetDonkBets(PokerRound round) const;
  void SetStreetBets(PokerRound round, const AbstractedBetConfig& bets);
  void SetStreetBets(PokerRound round, const AbstractedBetStringConfig& bets);
  void SetDonkBets(PokerRound round, const AbstractedDonkBetConfig& bets);
  void SetDonkBets(PokerRound round,
                   const AbstractedDonkBetStringConfig& bets);

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
};

}  // namespace fisher::game::poker
