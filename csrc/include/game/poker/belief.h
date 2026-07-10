#pragma once

#include <string>
#include <vector>

namespace fisher::game::poker {

class PokerBelief {
 public:
  explicit PokerBelief(const std::vector<std::vector<float>>& belief);
  explicit PokerBelief(const std::vector<std::string>& piosolver_ranges);

  const std::vector<std::vector<float>>& Belief() const;
  std::vector<std::vector<float>> Normalize() const;

 private:
  static void ValidateBelief(const std::vector<std::vector<float>>& belief);

  std::vector<std::vector<float>> belief_;
};

}  // namespace fisher::game::poker
