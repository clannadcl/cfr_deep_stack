#include "game/poker/belief.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/game_basic.h"
#include "game/poker/poker_card.h"
#include "game/poker/poker_hand.h"

namespace fisher::game::poker {
namespace {

constexpr char kRankChars[] = "23456789TJQKA";
constexpr char kSuitChars[] = "cdhs";
constexpr float kNormalizeEpsilon = 1e-15f;

bool IsRankChar(char rank) {
  for (char candidate : kRankChars) {
    if (candidate == '\0') break;
    if (candidate == rank) return true;
  }
  return false;
}

bool IsSuitChar(char suit) {
  for (char candidate : kSuitChars) {
    if (candidate == '\0') break;
    if (candidate == suit) return true;
  }
  return false;
}

int RankIndex(char rank) {
  for (int index = 0; kRankChars[index] != '\0'; ++index) {
    if (kRankChars[index] == rank) return index;
  }
  throw std::invalid_argument("Invalid poker range rank");
}

std::string Trim(const std::string& input) {
  std::size_t begin = 0;
  while (begin < input.size() &&
         std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }

  std::size_t end = input.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(begin, end - begin);
}

std::vector<std::string> SplitComma(const std::string& input) {
  std::vector<std::string> tokens;
  std::size_t begin = 0;
  while (begin <= input.size()) {
    const std::size_t comma = input.find(',', begin);
    const std::size_t end = comma == std::string::npos ? input.size() : comma;
    std::string token = Trim(input.substr(begin, end - begin));
    if (!token.empty()) {
      tokens.push_back(token);
    }
    if (comma == std::string::npos) break;
    begin = comma + 1;
  }
  return tokens;
}

float ParseWeight(const std::string& weight_text) {
  std::size_t parsed = 0;
  float weight = 0.0f;
  try {
    weight = std::stof(weight_text, &parsed);
  } catch (const std::exception&) {
    throw std::invalid_argument("Invalid poker range weight");
  }
  if (parsed != weight_text.size()) {
    throw std::invalid_argument("Invalid poker range weight");
  }
  if (weight < 0.0f) {
    throw std::invalid_argument("Poker range weight cannot be negative");
  }
  return weight;
}

struct RangeToken {
  std::string hand_text;
  float weight = 1.0f;
};

RangeToken ParseRangeToken(const std::string& token) {
  const std::size_t colon = token.find(':');
  if (colon == std::string::npos) {
    return RangeToken{token, 1.0f};
  }
  if (token.find(':', colon + 1) != std::string::npos) {
    throw std::invalid_argument("Poker range token has multiple ':'");
  }

  RangeToken parsed;
  parsed.hand_text = Trim(token.substr(0, colon));
  parsed.weight = ParseWeight(Trim(token.substr(colon + 1)));
  if (parsed.hand_text.empty()) {
    throw std::invalid_argument("Poker range token is empty");
  }
  return parsed;
}

bool IsConcreteHandText(const std::string& text) {
  return text.size() == 4 && IsRankChar(text[0]) && IsSuitChar(text[1]) &&
         IsRankChar(text[2]) && IsSuitChar(text[3]);
}

bool IsPreflopHandText(const std::string& text) {
  if (text.size() != 2 && text.size() != 3) return false;
  if (!IsRankChar(text[0]) || !IsRankChar(text[1])) return false;
  if (text.size() == 2) return true;
  return text[2] == 's' || text[2] == 'o';
}

void ApplyConcreteHand(const GameBasic& game, const RangeToken& token,
                       std::vector<float>* player_belief) {
  const int index = game.HandIndex(PokerHand(token.hand_text));
  (*player_belief)[static_cast<std::size_t>(index)] =
      token.weight / static_cast<float>(GameBasic::kNumHands);
}

void ApplyPreflopHand(const GameBasic& game, const RangeToken& token,
                      std::vector<float>* player_belief) {
  const std::string& text = token.hand_text;
  const int first_rank = RankIndex(text[0]);
  const int second_rank = RankIndex(text[1]);
  const bool pair = first_rank == second_rank;
  const bool suited_only = text.size() == 3 && text[2] == 's';
  const bool offsuit_only = text.size() == 3 && text[2] == 'o';

  if (pair && (suited_only || offsuit_only)) {
    throw std::invalid_argument("Pocket pair range cannot be suited or offsuit");
  }

  for (int first_suit = 0; first_suit < 4; ++first_suit) {
    for (int second_suit = 0; second_suit < 4; ++second_suit) {
      if (pair && first_suit >= second_suit) continue;
      if (!pair && suited_only && first_suit != second_suit) continue;
      if (!pair && offsuit_only && first_suit == second_suit) continue;

      const uint8_t first_card =
          static_cast<uint8_t>(first_rank * 4 + first_suit);
      const uint8_t second_card =
          static_cast<uint8_t>(second_rank * 4 + second_suit);
      const int index =
          game.HandIndex(PokerHand(PokerCard(first_card), PokerCard(second_card)));
      (*player_belief)[static_cast<std::size_t>(index)] =
          token.weight / static_cast<float>(GameBasic::kNumHands);
    }
  }
}

std::vector<float> ParsePiosolverRange(const GameBasic& game,
                                       const std::string& range_text) {
  std::vector<float> player_belief(GameBasic::kNumHands, 0.0f);
  for (const std::string& token_text : SplitComma(range_text)) {
    const RangeToken token = ParseRangeToken(token_text);
    if (IsConcreteHandText(token.hand_text)) {
      ApplyConcreteHand(game, token, &player_belief);
    } else if (IsPreflopHandText(token.hand_text)) {
      ApplyPreflopHand(game, token, &player_belief);
    } else {
      throw std::invalid_argument("Unsupported poker range token");
    }
  }
  return player_belief;
}

}  // namespace

PokerBelief::PokerBelief(const std::vector<std::vector<float>>& belief)
    : belief_(belief) {
  ValidateBelief(belief_);
}

PokerBelief::PokerBelief(const std::vector<std::string>& piosolver_ranges) {
  GameBasic game;
  belief_.reserve(piosolver_ranges.size());
  for (const std::string& range : piosolver_ranges) {
    belief_.push_back(ParsePiosolverRange(game, range));
  }
  ValidateBelief(belief_);
}

const std::vector<std::vector<float>>& PokerBelief::Belief() const {
  return belief_;
}

std::vector<std::vector<float>> PokerBelief::Normalize() const {
  std::vector<std::vector<float>> normalized = belief_;
  for (std::vector<float>& player_belief : normalized) {
    float sum = 0.0f;
    for (float weight : player_belief) {
      sum += weight;
    }

    const float denominator = sum > kNormalizeEpsilon ? sum : kNormalizeEpsilon;
    for (float& weight : player_belief) {
      weight /= denominator;
    }
  }
  return normalized;
}

void PokerBelief::ValidateBelief(
    const std::vector<std::vector<float>>& belief) {
  if (belief.empty()) {
    throw std::invalid_argument("PokerBelief requires at least one player");
  }
  const std::size_t num_hands = belief.front().size();
  if (num_hands == 0) {
    throw std::invalid_argument("PokerBelief requires at least one hand");
  }

  for (const std::vector<float>& player_belief : belief) {
    if (player_belief.size() != num_hands) {
      throw std::invalid_argument("PokerBelief rows must have same size");
    }
    for (float weight : player_belief) {
      if (weight < 0.0f) {
        throw std::invalid_argument("PokerBelief cannot contain negative weight");
      }
    }
  }
}

}  // namespace fisher::game::poker
