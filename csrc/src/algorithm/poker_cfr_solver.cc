#include "algorithm/poker_cfr_solver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "algorithm/best_response_calculator.h"
#include "game/poker/game_basic.h"
#include "game/poker/node_state.h"

namespace fisher::algorithm {
namespace {

constexpr float kEvMassEpsilon = 1e-10f;
constexpr std::size_t kTerminalCfvBatchSize = 64;

struct EvRawHandEntry {
  int iso_index = -1;
  int high_card = 0;
  int low_card = 0;
  double iso_normalization = 0.0;
};

struct EvReachStats {
  double total = 0.0;
  std::array<double, game::poker::GameBasic::kDeckSize> by_card{};
  std::array<std::array<double, game::poker::GameBasic::kDeckSize>,
             game::poker::GameBasic::kDeckSize>
      by_card_pair{};

  void Add(const EvRawHandEntry& hand, double reach) {
    total += reach;
    by_card[static_cast<std::size_t>(hand.high_card)] += reach;
    by_card[static_cast<std::size_t>(hand.low_card)] += reach;
    by_card_pair[static_cast<std::size_t>(hand.high_card)]
                [static_cast<std::size_t>(hand.low_card)] += reach;
    by_card_pair[static_cast<std::size_t>(hand.low_card)]
                [static_cast<std::size_t>(hand.high_card)] += reach;
  }

  double Excluding(const EvRawHandEntry& hand) const {
    return total - by_card[static_cast<std::size_t>(hand.high_card)] -
           by_card[static_cast<std::size_t>(hand.low_card)] +
           by_card_pair[static_cast<std::size_t>(hand.high_card)]
                       [static_cast<std::size_t>(hand.low_card)];
  }
};

void ValidatePlayer(int player) {
  if (player < 0 || player >= game::poker::GameBasic::kNumPlayers) {
    throw std::invalid_argument("Poker CFR player must be 0 or 1");
  }
}

bool IsPlayerNode(const game::poker::PokerTreeNode& node) {
  return !node.node_state->IsTerminal() &&
         node.node_state->ActorPlayer() !=
             game::poker::NodeState::kChancePlayer;
}

std::shared_ptr<game::poker::SubgameSetup> ValidateSetup(
    std::shared_ptr<game::poker::SubgameSetup> setup) {
  if (setup == nullptr) {
    throw std::invalid_argument("Poker CFR setup cannot be null");
  }
  return setup;
}

int ResolveThreadCount(int requested_threads) {
  if (requested_threads < 0) {
    throw std::invalid_argument("Poker CFR thread count cannot be negative");
  }
  if (requested_threads > 0) {
    return requested_threads;
  }
  const unsigned int hardware_threads = std::thread::hardware_concurrency();
  return std::max(1, static_cast<int>(hardware_threads));
}

int ValidatePositiveInt(int value, const char* name) {
  if (value <= 0) {
    throw std::invalid_argument(name);
  }
  return value;
}

float ResolveTargetExploitability(
    const std::shared_ptr<game::poker::SubgameSetup>& setup,
    float requested_target) {
  if (requested_target >= 0.0f) {
    return requested_target;
  }
  return setup->Pot() * 0.001f;
}

float ValidateFiniteFloat(float value, const char* name) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(name);
  }
  return value;
}

float ValidateNonNegativeFloat(float value, const char* name) {
  ValidateFiniteFloat(value, name);
  if (value < 0.0f) {
    throw std::invalid_argument(name);
  }
  return value;
}

float DcfrDiscount(int iteration, float exponent) {
  if (iteration < 0) {
    throw std::invalid_argument("DCFR iteration cannot be negative");
  }
  const double powered =
      std::pow(static_cast<double>(iteration), static_cast<double>(exponent));
  return static_cast<float>(powered / (powered + 1.0));
}

float DcfrAverageStrategyDiscount(int iteration, float exponent) {
  if (iteration < 0) {
    throw std::invalid_argument("DCFR iteration cannot be negative");
  }
  const double ratio = static_cast<double>(iteration) /
                       static_cast<double>(iteration + 1);
  return static_cast<float>(std::pow(ratio, static_cast<double>(exponent)));
}

std::vector<EvRawHandEntry> BuildEvRawHandEntries(
    const game::poker::GameBasic& game_basic,
    const game::poker::IsomorphicMapping& mapping) {
  std::vector<EvRawHandEntry> entries;
  entries.reserve(game::poker::GameBasic::kNumHands);
  for (int raw_index = 0; raw_index < game::poker::GameBasic::kNumHands;
       ++raw_index) {
    const int iso_index = mapping.RawToIso(raw_index);
    if (iso_index == game::poker::IsomorphicMapping::kInvalidIsoIndex) {
      continue;
    }
    const game::poker::PokerHand& hand = game_basic.HandFromIndex(raw_index);
    entries.push_back(EvRawHandEntry{
        iso_index,
        hand.HighCard().Value(),
        hand.LowCard().Value(),
        1.0 / static_cast<double>(mapping.RawHandCount(iso_index)),
    });
  }
  return entries;
}

std::vector<float> ComputeEvValidMass(
    const std::vector<EvRawHandEntry>& hands,
    const game::poker::IsomorphicMapping& mapping,
    const std::vector<float>& opponent_reach) {
  EvReachStats opponent_stats;
  for (const EvRawHandEntry& hand : hands) {
    const double iso_reach =
        opponent_reach[static_cast<std::size_t>(hand.iso_index)];
    if (iso_reach == 0.0) {
      continue;
    }
    opponent_stats.Add(hand, iso_reach * hand.iso_normalization);
  }

  std::vector<float> valid_mass(
      static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
  for (const EvRawHandEntry& hand : hands) {
    valid_mass[static_cast<std::size_t>(hand.iso_index)] +=
        static_cast<float>(opponent_stats.Excluding(hand) *
                           hand.iso_normalization);
  }
  return valid_mass;
}

int TransitionKey(int parent_iso, int child_iso, int child_num_hands) {
  return parent_iso * child_num_hands + child_iso;
}

void ValidateAdjacentBoardTransition(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child) {
  const int parent_cards = game::poker::BoardCardsForRound(parent.Round());
  const int child_cards = game::poker::BoardCardsForRound(child.Round());
  if (child_cards != parent_cards + 1) {
    throw std::invalid_argument("Iso transition child board must be next street");
  }
  if (parent.RawBoard().Size() >= child.RawBoard().Size()) {
    throw std::invalid_argument(
        "Iso transition child board must contain more cards");
  }
  for (std::size_t index = 0; index < parent.RawBoard().Size(); ++index) {
    if (parent.RawBoard().Cards()[index].Value() !=
        child.RawBoard().Cards()[index].Value()) {
      throw std::invalid_argument(
          "Iso transition child board must contain parent board as prefix");
    }
  }
}

}  // namespace

PokerCfrSolver::Args::Args(std::shared_ptr<game::poker::SubgameSetup> setup,
                           int num_threads, int max_iterations,
                           int exploitability_check_interval,
                           float target_exploitability, bool use_dcfr,
                           float positive_regret_discount_exponent,
                           float negative_regret_discount_exponent,
                           float average_strategy_discount_exponent)
    : setup(std::move(setup)),
      num_threads(num_threads),
      max_iterations(max_iterations),
      exploitability_check_interval(exploitability_check_interval),
      target_exploitability(target_exploitability),
      use_dcfr(use_dcfr),
      positive_regret_discount_exponent(positive_regret_discount_exponent),
      negative_regret_discount_exponent(negative_regret_discount_exponent),
      average_strategy_discount_exponent(average_strategy_discount_exponent) {}

IsoTransition BuildIsoTransition(
    int parent_node_id, int child_node_id,
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child) {
  ValidateAdjacentBoardTransition(parent, child);
  const int child_num_hands = child.NumIsoHands();
  std::unordered_map<int, int> counts;
  counts.reserve(game::poker::GameBasic::kNumHands);
  for (int raw = 0; raw < game::poker::GameBasic::kNumHands; ++raw) {
    const int parent_iso = parent.RawToIso(raw);
    const int child_iso = child.RawToIso(raw);
    if (parent_iso < 0 || child_iso < 0) {
      continue;
    }
    ++counts[TransitionKey(parent_iso, child_iso, child_num_hands)];
  }

  IsoTransition transition;
  transition.parent_node_id = parent_node_id;
  transition.child_node_id = child_node_id;
  transition.chance_prob =
      1.0f / static_cast<float>(game::poker::GameBasic::kDeckSize -
                                parent.RawBoard().Size() - 4);
  transition.edges.reserve(counts.size());
  for (const auto& entry : counts) {
    const int parent_iso = entry.first / child_num_hands;
    const int child_iso = entry.first % child_num_hands;
    transition.edges.push_back(IsoTransitionEdge{
        parent_iso,
        child_iso,
        static_cast<float>(entry.second) /
            static_cast<float>(parent.RawHandCount(parent_iso)),
    });
  }
  return transition;
}

class PokerCfrSolver::ThreadPool {
 public:
  explicit ThreadPool(int num_threads) {
    if (num_threads <= 1) {
      return;
    }
    workers_.reserve(static_cast<std::size_t>(num_threads));
    for (int worker = 0; worker < num_threads; ++worker) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
      job_available_.notify_all();
    }
    for (std::thread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  template <typename Fn>
  void ParallelFor(std::size_t count, Fn fn) {
    if (count == 0) {
      return;
    }
    if (workers_.empty() || count == 1) {
      for (std::size_t index = 0; index < count; ++index) {
        fn(index);
      }
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (has_job_) {
        throw std::runtime_error("ThreadPool does not support nested jobs");
      }
      task_ = std::move(fn);
      count_ = count;
      next_index_ = 0;
      remaining_ = count;
      first_exception_ = nullptr;
      has_job_ = true;
    }
    job_available_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    job_done_.wait(lock, [this] { return !has_job_; });
    if (first_exception_ != nullptr) {
      std::rethrow_exception(first_exception_);
    }
  }

 private:
  void WorkerLoop() {
    while (true) {
      std::size_t index = 0;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        job_available_.wait(lock, [this] {
          return stop_ || (has_job_ && next_index_ < count_);
        });
        if (stop_) {
          return;
        }
        index = next_index_;
        ++next_index_;
      }

      try {
        task_(index);
      } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (first_exception_ == nullptr) {
          first_exception_ = std::current_exception();
        }
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);
        --remaining_;
        if (remaining_ == 0) {
          has_job_ = false;
          task_ = nullptr;
          job_done_.notify_one();
        } else if (next_index_ < count_) {
          job_available_.notify_one();
        }
      }
    }
  }

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable job_available_;
  std::condition_variable job_done_;
  std::function<void(std::size_t)> task_;
  std::exception_ptr first_exception_ = nullptr;
  std::size_t count_ = 0;
  std::size_t next_index_ = 0;
  std::size_t remaining_ = 0;
  bool has_job_ = false;
  bool stop_ = false;
};

PokerCfrSolver::PokerCfrSolver(const Args& args)
    : setup_(ValidateSetup(args.setup)),
      tree_(setup_),
      mapping_table_(setup_->BasicGame(), setup_->RootBelief()),
      storage_(tree_, &mapping_table_),
      terminal_cfv_calculator_(setup_->BasicGame(), evaluator_),
      num_threads_(ResolveThreadCount(args.num_threads)),
      max_iterations_(ValidatePositiveInt(
          args.max_iterations, "Poker CFR max iterations must be positive")),
      exploitability_check_interval_(ValidatePositiveInt(
          args.exploitability_check_interval,
          "Poker CFR exploitability check interval must be positive")),
      target_exploitability_(
          ResolveTargetExploitability(setup_, args.target_exploitability)),
      use_dcfr_(args.use_dcfr),
      positive_regret_discount_exponent_(ValidateNonNegativeFloat(
          args.positive_regret_discount_exponent,
          "DCFR positive regret discount exponent must be non-negative")),
      negative_regret_discount_exponent_(ValidateFiniteFloat(
          args.negative_regret_discount_exponent,
          "DCFR negative regret discount exponent must be finite")),
      average_strategy_discount_exponent_(ValidateNonNegativeFloat(
          args.average_strategy_discount_exponent,
          "DCFR average strategy discount exponent must be non-negative")) {
  thread_pool_ = std::make_unique<ThreadPool>(num_threads_);
  BuildNodeCaches();
}

PokerCfrSolver::~PokerCfrSolver() = default;

void PokerCfrSolver::RunIteration() {
  average_finalized_ = false;
  RunHeroPass(0);
  RunHeroPass(1);
}

void PokerCfrSolver::RunHeroPass(int hero_player) {
  RunHeroPassProfiled(hero_player);
}

PokerCfrSolver::HeroPassProfile PokerCfrSolver::RunHeroPassProfiled(
    int hero_player) {
  using Clock = std::chrono::steady_clock;
  auto elapsed_ms = [](Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  };

  ValidatePlayer(hero_player);
  average_finalized_ = false;
  RefreshDcfrDiscounts();
  const auto total_begin = Clock::now();

  const auto initialize_begin = Clock::now();
  InitializeRootReach();
  const auto initialize_end = Clock::now();

  const auto forward_begin = Clock::now();
  ForwardReachAndAccumulateAverage(hero_player);
  const auto forward_end = Clock::now();

  const auto terminal_begin = Clock::now();
  ComputeTerminalCfvs(hero_player);
  const auto terminal_end = Clock::now();

  const auto backward_begin = Clock::now();
  BackwardAndUpdate(hero_player);
  const auto backward_end = Clock::now();

  ++hero_pass_count_;

  HeroPassProfile profile;
  profile.initialize_root_reach_ms =
      elapsed_ms(initialize_begin, initialize_end);
  profile.forward_reach_ms = elapsed_ms(forward_begin, forward_end);
  profile.terminal_cfv_ms = elapsed_ms(terminal_begin, terminal_end);
  profile.backward_update_ms = elapsed_ms(backward_begin, backward_end);
  profile.total_ms = elapsed_ms(total_begin, backward_end);
  return profile;
}

PokerCfrSolver::SolveResult PokerCfrSolver::Solve(float average_epsilon) {
  if (average_epsilon <= 0.0f) {
    throw std::invalid_argument("Average strategy epsilon must be positive");
  }

  SolveResult result;
  result.target_exploitability = target_exploitability_;

  for (int iteration = 1; iteration <= max_iterations_; ++iteration) {
    RunIteration();
    const bool should_check =
        iteration % exploitability_check_interval_ == 0 ||
        iteration == max_iterations_;
    if (!should_check) {
      continue;
    }

    const ExploitabilityResult exploitability_result =
        BestResponseCalculator(this).Compute(average_epsilon);
    result.iterations = iteration;
    result.exploitability = exploitability_result.exploitability;
    result.current_ev = exploitability_result.current_ev;
    result.best_response_ev = exploitability_result.best_response_ev;
    result.converged = result.exploitability <= target_exploitability_;
    if (result.converged) {
      break;
    }
  }

  return result;
}

void PokerCfrSolver::FinalizeAverageStrategy(float average_epsilon) {
  if (average_epsilon <= 0.0f) {
    throw std::invalid_argument("Average strategy epsilon must be positive");
  }

  InitializeRootReach();
  for (const std::vector<int>& level : node_ids_by_depth_) {
    thread_pool_->ParallelFor(level.size(), [&](std::size_t index) {
      const game::poker::PokerTreeNode& node = tree_.Node(level[index]);
      if (node.node_state->IsTerminal()) {
        return;
      }
      if (node.node_state->ActorPlayer() ==
          game::poker::NodeState::kChancePlayer) {
        PropagateChanceReach(node);
      } else {
        PropagateAveragePlayerReach(node, average_epsilon);
      }
    });
  }

  ComputeTerminalCfvs(0);
  ComputeTerminalCfvs(1);

  for (auto level_it = node_ids_by_depth_.rbegin();
       level_it != node_ids_by_depth_.rend(); ++level_it) {
    const std::vector<int>& level = *level_it;
    thread_pool_->ParallelFor(level.size(), [&](std::size_t index) {
      const game::poker::PokerTreeNode& node = tree_.Node(level[index]);
      if (node.node_state->IsTerminal()) {
        return;
      }
      if (node.node_state->ActorPlayer() ==
          game::poker::NodeState::kChancePlayer) {
        BackwardChanceNode(node, /*player=*/0);
        BackwardChanceNode(node, /*player=*/1);
      } else {
        BackwardAveragePlayerNode(node, average_epsilon);
      }
    });
  }

  average_finalized_ = true;
}

std::vector<float> PokerCfrSolver::CurrentStrategyData() const {
  return storage_.StrategyData();
}

std::vector<float> PokerCfrSolver::AverageStrategyData(float epsilon) const {
  if (epsilon <= 0.0f) {
    throw std::invalid_argument("Average strategy epsilon must be positive");
  }

  std::vector<float> average = storage_.SumStrategyData();
  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    if (!IsPlayerNode(node)) {
      continue;
    }
    const NodeCfrLayout& layout = storage_.Layout(node.node_id);
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      float denominator = 0.0f;
      for (int action = 0; action < layout.num_actions; ++action) {
        denominator +=
            storage_.SumStrategyAt(node.node_id, action, hand) + epsilon;
      }
      for (int action = 0; action < layout.num_actions; ++action) {
        average[static_cast<std::size_t>(
            layout.sum_strategy_offset + action * layout.num_hands + hand)] =
            (storage_.SumStrategyAt(node.node_id, action, hand) + epsilon) /
            denominator;
      }
    }
  }
  return average;
}

float PokerCfrSolver::AverageStrategyAt(int node_id, int action_index,
                                        int hand_index, float epsilon) const {
  if (epsilon <= 0.0f) {
    throw std::invalid_argument("Average strategy epsilon must be positive");
  }
  const NodeCfrLayout& layout = storage_.Layout(node_id);
  if (action_index < 0 || action_index >= layout.num_actions) {
    throw std::invalid_argument("Average strategy action index is out of range");
  }
  if (hand_index < 0 || hand_index >= layout.num_hands) {
    throw std::invalid_argument("Average strategy hand index is out of range");
  }

  float denominator = 0.0f;
  for (int action = 0; action < layout.num_actions; ++action) {
    denominator += storage_.SumStrategyAt(node_id, action, hand_index) +
                   epsilon;
  }
  return (storage_.SumStrategyAt(node_id, action_index, hand_index) +
          epsilon) /
         denominator;
}

PokerCfrSolver::NodeEvDetail PokerCfrSolver::NodeEv(int node_id,
                                                    int player) const {
  if (!average_finalized_) {
    throw std::invalid_argument(
        "Node EV requires FinalizeAverageStrategy first");
  }
  tree_.Node(node_id);
  ValidatePlayer(player);

  NodeEvDetail detail;
  detail.node_id = node_id;
  detail.player = player;
  detail.cfv.resize(static_cast<std::size_t>(storage_.NumHands(node_id)));
  detail.valid_mass = ValidMassVector(node_id, player);
  detail.hand_ev.assign(detail.cfv.size(), 0.0f);

  double numerator = 0.0;
  double denominator = 0.0;
  for (int hand = 0; hand < storage_.NumHands(node_id); ++hand) {
    const std::size_t hand_index = static_cast<std::size_t>(hand);
    detail.cfv[hand_index] = storage_.CfvAt(node_id, player, hand);
    if (detail.valid_mass[hand_index] > kEvMassEpsilon) {
      detail.hand_ev[hand_index] =
          detail.cfv[hand_index] / detail.valid_mass[hand_index];
    }
    const double own_reach = storage_.ReachAt(node_id, player, hand);
    numerator += own_reach * static_cast<double>(detail.cfv[hand_index]);
    denominator +=
        own_reach * static_cast<double>(detail.valid_mass[hand_index]);
  }

  detail.range_mass = static_cast<float>(denominator);
  detail.range_ev =
      denominator > static_cast<double>(kEvMassEpsilon)
          ? static_cast<float>(numerator / denominator)
          : 0.0f;
  return detail;
}

const game::poker::PokerTree& PokerCfrSolver::Tree() const { return tree_; }

int PokerCfrSolver::NumThreads() const { return num_threads_; }

CfrStorage& PokerCfrSolver::Storage() { return storage_; }

const CfrStorage& PokerCfrSolver::Storage() const { return storage_; }

const std::vector<int>& PokerCfrSolver::TerminalNodeIds() const {
  return terminal_node_ids_;
}

const std::vector<int>& PokerCfrSolver::ReverseNodeIds() const {
  return reverse_node_ids_;
}

const IsoTransition& PokerCfrSolver::ChanceTransition(
    int child_node_id) const {
  const auto iterator = chance_transitions_by_child_.find(child_node_id);
  if (iterator == chance_transitions_by_child_.end()) {
    throw std::invalid_argument("Chance transition child node not found");
  }
  return iterator->second;
}

void PokerCfrSolver::BuildNodeCaches() {
  node_mappings_.resize(static_cast<std::size_t>(tree_.NumNodes()), nullptr);
  node_ids_by_depth_.clear();
  terminal_node_ids_.clear();
  reverse_node_ids_.clear();
  chance_transitions_by_child_.clear();

  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    if (node.depth < 0) {
      throw std::runtime_error("Poker tree node depth cannot be negative");
    }
    if (node.depth >= static_cast<int>(node_ids_by_depth_.size())) {
      node_ids_by_depth_.resize(static_cast<std::size_t>(node.depth + 1));
    }
    node_ids_by_depth_[static_cast<std::size_t>(node.depth)].push_back(
        node.node_id);
    node_mappings_[static_cast<std::size_t>(node.node_id)] =
        &mapping_table_.Get(node.node_state->Board());
    reverse_node_ids_.push_back(node.node_id);
    if (node.node_state->IsTerminal()) {
      terminal_node_ids_.push_back(node.node_id);
    }
  }

  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    if (node.node_state->ActorPlayer() ==
        game::poker::NodeState::kChancePlayer) {
      for (int child_index = 0; child_index < node.num_children;
           ++child_index) {
        const int child_id = tree_.ChildNodeIdAt(node.node_id, child_index);
        chance_transitions_by_child_.emplace(
            child_id, BuildChanceTransition(node.node_id, child_id));
      }
    }
  }
  std::reverse(reverse_node_ids_.begin(), reverse_node_ids_.end());
}

IsoTransition PokerCfrSolver::BuildChanceTransition(int parent_node_id,
                                                    int child_node_id) const {
  const game::poker::IsomorphicMapping& parent =
      *node_mappings_[static_cast<std::size_t>(parent_node_id)];
  const game::poker::IsomorphicMapping& child =
      *node_mappings_[static_cast<std::size_t>(child_node_id)];
  return BuildIsoTransition(parent_node_id, child_node_id, parent, child);
}

void PokerCfrSolver::RefreshDcfrDiscounts() {
  if (!use_dcfr_) {
    current_positive_regret_discount_ = 1.0f;
    current_negative_regret_discount_ = 1.0f;
    current_average_strategy_discount_ = 1.0f;
    return;
  }

  const int iteration =
      hero_pass_count_ / game::poker::GameBasic::kNumPlayers;
  current_positive_regret_discount_ =
      DcfrDiscount(iteration, positive_regret_discount_exponent_);
  current_negative_regret_discount_ =
      negative_regret_discount_exponent_ <= -5.0f
          ? 0.0f
          : DcfrDiscount(iteration, negative_regret_discount_exponent_);
  current_average_strategy_discount_ = DcfrAverageStrategyDiscount(
      iteration, average_strategy_discount_exponent_);
}

void PokerCfrSolver::InitializeRootReach() {
  const int root_node_id = tree_.Root().node_id;
  const game::poker::IsomorphicMapping& root_mapping =
      *node_mappings_[static_cast<std::size_t>(root_node_id)];
  const std::vector<std::vector<float>>& belief = setup_->RootBelief().Belief();
  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    for (int hand = 0; hand < root_mapping.NumIsoHands(); ++hand) {
      storage_.ReachAt(root_node_id, player, hand) = 0.0f;
    }
    for (int raw = 0; raw < game::poker::GameBasic::kNumHands; ++raw) {
      const int iso = root_mapping.RawToIso(raw);
      if (iso < 0) {
        continue;
      }
      storage_.ReachAt(root_node_id, player, iso) +=
          belief[static_cast<std::size_t>(player)]
                [static_cast<std::size_t>(raw)];
    }
  }
}

void PokerCfrSolver::ForwardReachAndAccumulateAverage(int hero_player) {
  for (const std::vector<int>& level : node_ids_by_depth_) {
    thread_pool_->ParallelFor(level.size(), [&](std::size_t index) {
      const game::poker::PokerTreeNode& node =
          tree_.Node(level[index]);
      if (node.node_state->IsTerminal()) {
        return;
      }
      if (node.node_state->ActorPlayer() ==
          game::poker::NodeState::kChancePlayer) {
        PropagateChanceReach(node);
      } else {
        PropagatePlayerReach(node, hero_player);
      }
    });
  }
}

void PokerCfrSolver::PropagatePlayerReach(
    const game::poker::PokerTreeNode& node, int hero_player) {
  const int actor = node.node_state->ActorPlayer();
  const NodeCfrLayout& layout = storage_.Layout(node.node_id);
  const float* parent_reach = storage_.ReachBlock(node.node_id, actor);
  const float* strategy = storage_.StrategyBlock(node.node_id);
  for (int action = 0; action < layout.num_actions; ++action) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
    float* child_reach = storage_.ReachBlock(child_id, actor);
    const float* action_strategy =
        strategy + static_cast<std::size_t>(action * layout.num_hands);
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      child_reach[hand] = parent_reach[hand] * action_strategy[hand];
    }
  }

  if (actor == hero_player) {
    float* sum_strategy = storage_.SumStrategyBlock(node.node_id);
    for (int action = 0; action < layout.num_actions; ++action) {
      const int action_offset = action * layout.num_hands;
      for (int hand = 0; hand < layout.num_hands; ++hand) {
        const int index = action_offset + hand;
        sum_strategy[index] =
            sum_strategy[index] * current_average_strategy_discount_ +
            parent_reach[hand] * strategy[index];
      }
    }
  }
}

void PokerCfrSolver::PropagateAveragePlayerReach(
    const game::poker::PokerTreeNode& node, float average_epsilon) {
  const int actor = node.node_state->ActorPlayer();
  const NodeCfrLayout& layout = storage_.Layout(node.node_id);
  for (int action = 0; action < layout.num_actions; ++action) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      storage_.ReachAt(child_id, actor, hand) =
          storage_.ReachAt(node.node_id, actor, hand) *
          AverageStrategyAt(node.node_id, action, hand, average_epsilon);
    }
  }
}

void PokerCfrSolver::PropagateChanceReach(
    const game::poker::PokerTreeNode& node) {
  for (int child_index = 0; child_index < node.num_children; ++child_index) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, child_index);
    const IsoTransition& transition = ChanceTransition(child_id);
    for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
         ++player) {
      for (int hand = 0; hand < storage_.NumHands(child_id); ++hand) {
        storage_.ReachAt(child_id, player, hand) = 0.0f;
      }
      for (const IsoTransitionEdge& edge : transition.edges) {
        storage_.ReachAt(child_id, player, edge.child_iso) +=
            storage_.ReachAt(node.node_id, player, edge.parent_iso) *
            edge.weight;
      }
    }
  }
}

void PokerCfrSolver::ComputeTerminalCfvs(int player) {
  ValidatePlayer(player);
  std::vector<int> direct_terminal_node_ids;
  std::unordered_map<std::string, std::vector<int>> runout_node_ids_by_key;
  direct_terminal_node_ids.reserve(terminal_node_ids_.size());
  for (int node_id : terminal_node_ids_) {
    const game::poker::PokerTreeNode& node = tree_.Node(node_id);
    if (node.node_state->Status() ==
            game::poker::TerminalStatus::kShowdownTerminal &&
        node.node_state->Street() != game::poker::PokerRound::kRiver) {
      const game::poker::IsomorphicMapping& mapping =
          *node_mappings_[static_cast<std::size_t>(node_id)];
      const std::string key = mapping.RawBoard().ToString() + "#" +
                              std::to_string(mapping.NumIsoHands());
      runout_node_ids_by_key[key].push_back(node_id);
    } else {
      direct_terminal_node_ids.push_back(node_id);
    }
  }

  thread_pool_->ParallelFor(direct_terminal_node_ids.size(),
                            [&](std::size_t index) {
                              const int node_id =
                                  direct_terminal_node_ids[index];
                              const game::poker::PokerTreeNode& node =
                                  tree_.Node(node_id);
                              const game::poker::IsomorphicMapping& mapping =
                                  *node_mappings_[static_cast<std::size_t>(
                                      node_id)];
                              terminal_cfv_calculator_.CalculateInto(
                                  *node.node_state, player, mapping,
                                  storage_.ReachBlock(node_id, 1 - player),
                                  storage_.NumHands(node_id),
                                  storage_.CfvBlock(node_id, player));
                            });

  struct RunoutBatch {
    const std::vector<int>* node_ids = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0;
  };
  std::vector<RunoutBatch> runout_batches;
  for (const auto& entry : runout_node_ids_by_key) {
    for (std::size_t offset = 0; offset < entry.second.size();
         offset += kTerminalCfvBatchSize) {
      runout_batches.push_back(RunoutBatch{
          &entry.second,
          offset,
          std::min(kTerminalCfvBatchSize, entry.second.size() - offset),
      });
    }
  }

  thread_pool_->ParallelFor(runout_batches.size(), [&](std::size_t index) {
    const RunoutBatch& batch = runout_batches[index];
    std::vector<game::poker::TerminalCfvCalculator::BatchItem> items;
    items.reserve(batch.size);
    for (std::size_t batch_index = 0; batch_index < batch.size;
         ++batch_index) {
      const int node_id = (*batch.node_ids)[batch.offset + batch_index];
      const game::poker::PokerTreeNode& node = tree_.Node(node_id);
      const game::poker::IsomorphicMapping& mapping =
          *node_mappings_[static_cast<std::size_t>(node_id)];
      items.push_back(game::poker::TerminalCfvCalculator::BatchItem{
          node.node_state.get(),
          player,
          &mapping,
          storage_.ReachBlock(node_id, 1 - player),
          storage_.NumHands(node_id),
          storage_.CfvBlock(node_id, player),
      });
    }
    terminal_cfv_calculator_.CalculateRunoutShowdownBatch(items);
  });
}

void PokerCfrSolver::BackwardAndUpdate(int hero_player) {
  for (auto level_it = node_ids_by_depth_.rbegin();
       level_it != node_ids_by_depth_.rend(); ++level_it) {
    const std::vector<int>& level = *level_it;
    thread_pool_->ParallelFor(level.size(), [&](std::size_t index) {
      const game::poker::PokerTreeNode& node =
          tree_.Node(level[index]);
      if (node.node_state->IsTerminal()) {
        return;
      }
      if (node.node_state->ActorPlayer() ==
          game::poker::NodeState::kChancePlayer) {
        BackwardChanceNode(node, hero_player);
      } else {
        BackwardPlayerNode(node, hero_player);
      }
    });
  }
}

void PokerCfrSolver::BackwardChanceNode(
    const game::poker::PokerTreeNode& node, int player) {
  for (int hand = 0; hand < storage_.NumHands(node.node_id); ++hand) {
    storage_.CfvAt(node.node_id, player, hand) = 0.0f;
  }

  for (int child_index = 0; child_index < node.num_children; ++child_index) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, child_index);
    const IsoTransition& transition = ChanceTransition(child_id);
    for (const IsoTransitionEdge& edge : transition.edges) {
      storage_.CfvAt(node.node_id, player, edge.parent_iso) +=
          storage_.CfvAt(child_id, player, edge.child_iso) * edge.weight *
          transition.chance_prob;
    }
  }
}

void PokerCfrSolver::BackwardPlayerNode(
    const game::poker::PokerTreeNode& node, int hero_player) {
  const int actor = node.node_state->ActorPlayer();
  const NodeCfrLayout& layout = storage_.Layout(node.node_id);
  const float* strategy = storage_.StrategyBlock(node.node_id);

  float* node_cfv = storage_.CfvBlock(node.node_id, hero_player);
  for (int hand = 0; hand < layout.num_hands; ++hand) {
    double value = 0.0;
    for (int action = 0; action < layout.num_actions; ++action) {
      const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
      const float* child_cfv = storage_.CfvBlock(child_id, hero_player);
      const double child_value = child_cfv[hand];
      value += hero_player == actor
                   ? static_cast<double>(
                         strategy[action * layout.num_hands + hand]) *
                         child_value
                   : child_value;
    }
    node_cfv[hand] = static_cast<float>(value);
  }

  if (actor != hero_player) {
    return;
  }

  float* regret = storage_.RegretBlock(node.node_id);
  float* mutable_strategy = storage_.StrategyBlock(node.node_id);
  const float* actor_node_cfv = storage_.CfvBlock(node.node_id, actor);
  std::vector<const float*> child_cfvs;
  child_cfvs.reserve(static_cast<std::size_t>(layout.num_actions));
  for (int action = 0; action < layout.num_actions; ++action) {
    child_cfvs.push_back(
        storage_.CfvBlock(tree_.ChildNodeIdAt(node.node_id, action), actor));
  }

  for (int hand = 0; hand < layout.num_hands; ++hand) {
    float positive_sum = 0.0f;
    for (int action = 0; action < layout.num_actions; ++action) {
      const int index = action * layout.num_hands + hand;
      regret[index] += child_cfvs[static_cast<std::size_t>(action)][hand] -
                       actor_node_cfv[hand];
      if (regret[index] > 0.0f) {
        positive_sum += regret[index];
      }
    }

    if (positive_sum <= 0.0f) {
      const float uniform = 1.0f / static_cast<float>(layout.num_actions);
      for (int action = 0; action < layout.num_actions; ++action) {
        mutable_strategy[action * layout.num_hands + hand] = uniform;
      }
    } else {
      for (int action = 0; action < layout.num_actions; ++action) {
        const int index = action * layout.num_hands + hand;
        mutable_strategy[index] =
            std::max(0.0f, regret[index]) / positive_sum;
      }
    }

    for (int action = 0; action < layout.num_actions; ++action) {
      const int index = action * layout.num_hands + hand;
      if (regret[index] > 0.0f) {
        regret[index] *= current_positive_regret_discount_;
      } else if (regret[index] < 0.0f) {
        regret[index] *= current_negative_regret_discount_;
      }
    }
  }
}

void PokerCfrSolver::ApplyRegretDiscount(int node_id) {
  if (!use_dcfr_) {
    return;
  }

  const NodeCfrLayout& layout = storage_.Layout(node_id);
  for (int action = 0; action < layout.num_actions; ++action) {
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      float& regret = storage_.RegretAt(node_id, action, hand);
      if (regret > 0.0f) {
        regret *= current_positive_regret_discount_;
      } else if (regret < 0.0f) {
        regret *= current_negative_regret_discount_;
      }
    }
  }
}

void PokerCfrSolver::ApplyAverageStrategyDiscount(int node_id) {
  if (!use_dcfr_) {
    return;
  }

  const NodeCfrLayout& layout = storage_.Layout(node_id);
  for (int action = 0; action < layout.num_actions; ++action) {
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      storage_.SumStrategyAt(node_id, action, hand) *=
          current_average_strategy_discount_;
    }
  }
}

void PokerCfrSolver::BackwardAveragePlayerNode(
    const game::poker::PokerTreeNode& node, float average_epsilon) {
  const int actor = node.node_state->ActorPlayer();
  const NodeCfrLayout& layout = storage_.Layout(node.node_id);

  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      double value = 0.0;
      for (int action = 0; action < layout.num_actions; ++action) {
        const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
        const double child_value =
            storage_.CfvAt(child_id, player, hand);
        value += player == actor
                     ? static_cast<double>(AverageStrategyAt(
                           node.node_id, action, hand, average_epsilon)) *
                           child_value
                     : child_value;
      }
      storage_.CfvAt(node.node_id, player, hand) = static_cast<float>(value);
    }
  }
}

void PokerCfrSolver::UpdateStrategyFromRegret(int node_id) {
  const NodeCfrLayout& layout = storage_.Layout(node_id);
  for (int hand = 0; hand < layout.num_hands; ++hand) {
    float positive_sum = 0.0f;
    for (int action = 0; action < layout.num_actions; ++action) {
      positive_sum += std::max(0.0f, storage_.RegretAt(node_id, action, hand));
    }
    if (positive_sum <= 0.0f) {
      const float uniform = 1.0f / static_cast<float>(layout.num_actions);
      for (int action = 0; action < layout.num_actions; ++action) {
        storage_.StrategyAt(node_id, action, hand) = uniform;
      }
      continue;
    }
    for (int action = 0; action < layout.num_actions; ++action) {
      storage_.StrategyAt(node_id, action, hand) =
          std::max(0.0f, storage_.RegretAt(node_id, action, hand)) /
          positive_sum;
    }
  }
}

std::vector<float> PokerCfrSolver::ValidMassVector(int node_id,
                                                   int player) const {
  tree_.Node(node_id);
  ValidatePlayer(player);
  const game::poker::IsomorphicMapping& mapping =
      *node_mappings_[static_cast<std::size_t>(node_id)];
  const std::vector<EvRawHandEntry> hands =
      BuildEvRawHandEntries(setup_->BasicGame(), mapping);
  return ComputeEvValidMass(hands, mapping, ReachVector(node_id, 1 - player));
}

std::vector<float> PokerCfrSolver::ReachVector(int node_id, int player) const {
  std::vector<float> values(static_cast<std::size_t>(storage_.NumHands(node_id)),
                            0.0f);
  for (int hand = 0; hand < storage_.NumHands(node_id); ++hand) {
    values[static_cast<std::size_t>(hand)] =
        storage_.ReachAt(node_id, player, hand);
  }
  return values;
}

void PokerCfrSolver::WriteCfvVector(int node_id, int player,
                                    const std::vector<float>& cfv) {
  if (cfv.size() != static_cast<std::size_t>(storage_.NumHands(node_id))) {
    throw std::invalid_argument("Terminal CFV size does not match node layout");
  }
  for (int hand = 0; hand < storage_.NumHands(node_id); ++hand) {
    storage_.CfvAt(node_id, player, hand) =
        cfv[static_cast<std::size_t>(hand)];
  }
}

}  // namespace fisher::algorithm
