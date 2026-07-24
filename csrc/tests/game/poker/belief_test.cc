#include <cmath>
#include <stdexcept>
#include <vector>

#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/poker_hand.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, const char* message) {
  if (std::fabs(actual - expected) > 1e-6f) {
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

float Sum(const std::vector<float>& values) {
  float sum = 0.0f;
  for (float value : values) {
    sum += value;
  }
  return sum;
}

}  // namespace

int main() {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::PokerBelief;
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerHand;

  PokerBelief direct(
      std::vector<std::vector<float>>{{1.0f, 3.0f}, {0.0f, 0.0f}});
  ExpectNear(direct.Belief()[0][0], 1.0f, "raw first value mismatch");
  ExpectNear(direct.Belief()[0][1], 3.0f, "raw second value mismatch");
  const std::vector<std::vector<float>> normalized = direct.Normalize();
  ExpectNear(normalized[0][0], 0.25f, "normalized first value mismatch");
  ExpectNear(normalized[0][1], 0.75f, "normalized second value mismatch");
  ExpectNear(Sum(normalized[0]), 1.0f, "normalized row should sum to one");
  ExpectNear(Sum(normalized[1]), 0.0f, "zero row should stay zero");
  ExpectNear(direct.Belief()[0][0], 1.0f, "normalize changed raw belief");

  GameBasic game;
  PokerBelief ranges(
      std::vector<std::string>{"AA,76s:0.25", "AcKh:1,9h8h:0.5"});
  const std::vector<std::vector<float>>& belief = ranges.Belief();
  Expect(belief.size() == 2, "range player count mismatch");
  Expect(belief[0].size() == GameBasic::kNumHands,
         "range hand count mismatch");

  int pocket_aces = 0;
  for (int first_suit = 0; first_suit < 4; ++first_suit) {
    for (int second_suit = first_suit + 1; second_suit < 4; ++second_suit) {
      PokerHand hand(PokerCard(static_cast<uint8_t>(12 * 4 + first_suit)),
                     PokerCard(static_cast<uint8_t>(12 * 4 + second_suit)));
      ExpectNear(belief[0][game.HandIndex(hand)],
                 1.0f / static_cast<float>(GameBasic::kNumHands),
                 "AA combo weight mismatch");
      ++pocket_aces;
    }
  }
  Expect(pocket_aces == 6, "AA combo count mismatch");

  int suited_76 = 0;
  for (int suit = 0; suit < 4; ++suit) {
    PokerHand hand(PokerCard(static_cast<uint8_t>(5 * 4 + suit)),
                   PokerCard(static_cast<uint8_t>(4 * 4 + suit)));
    ExpectNear(belief[0][game.HandIndex(hand)],
               0.25f / static_cast<float>(GameBasic::kNumHands),
               "76s combo weight mismatch");
    ++suited_76;
  }
  Expect(suited_76 == 4, "76s combo count mismatch");

  ExpectNear(belief[1][game.HandIndex(PokerHand("AcKh"))],
             1.0f / static_cast<float>(GameBasic::kNumHands),
             "concrete AcKh weight mismatch");
  ExpectNear(belief[1][game.HandIndex(PokerHand("9h8h"))],
             0.5f / static_cast<float>(GameBasic::kNumHands),
             "concrete 9h8h weight mismatch");

  const std::vector<std::vector<float>> range_normalized = ranges.Normalize();
  ExpectNear(Sum(range_normalized[0]), 1.0f,
             "first range should normalize to one");
  ExpectNear(Sum(range_normalized[1]), 1.0f,
             "second range should normalize to one");

  ExpectInvalidArgument(
      [] { PokerBelief invalid(std::vector<std::vector<float>>{}); },
      "empty direct belief should be invalid");
  ExpectInvalidArgument(
      [] {
        PokerBelief invalid(
            std::vector<std::vector<float>>{{1.0f}, {1.0f, 2.0f}});
      },
      "ragged belief should be invalid");
  ExpectInvalidArgument(
      [] {
        PokerBelief invalid(std::vector<std::vector<float>>{{1.0f, -1.0f}});
      },
      "negative direct belief should be invalid");
  ExpectInvalidArgument(
      [] { PokerBelief invalid(std::vector<std::string>{"AA:-0.1"}); },
      "negative range weight should be invalid");
  ExpectInvalidArgument(
      [] { PokerBelief invalid(std::vector<std::string>{"AA:bad"}); },
      "invalid range weight should be invalid");
  ExpectInvalidArgument(
      [] { PokerBelief invalid(std::vector<std::string>{"AAo"}); },
      "suited marker on pair should be invalid");
  ExpectInvalidArgument(
      [] { PokerBelief invalid(std::vector<std::string>{"AcAc"}); },
      "duplicate concrete hand should be invalid");

  return 0;
}
