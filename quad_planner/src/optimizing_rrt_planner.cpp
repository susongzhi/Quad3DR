// Adapted by Benjamin Hepp from OMPL [http://ompl.kavrakilab.org/].
//
// Modified work Copyright (c) 2016 Benjamin Hepp.
//
// Original work Copyright (c) 2008, Willow Garage, Inc.
// Original Copyright notice:
/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2008, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

#include <quad_planner/optimizing_rrt_planner.h>

#include <ompl/util/Console.h>
#include <ompl/geometric/planners/rrt/RRT.h>
#include <ompl/base/objectives/PathLengthOptimizationObjective.h>
#include <ompl/base/goals/GoalSampleableRegion.h>
#include <ompl/tools/config/SelfConfig.h>
#include <limits>

using namespace quad_planner;


OptimizingRRT::OptimizingRRT(const ob::SpaceInformationPtr &si) : ob::Planner(si, "RRT")
{
  specs_.approximateSolutions = true;
  specs_.directed = true;

  goalBias_ = 0.05;
  maxDistance_ = 0.0;
  lastGoalMotion_ = nullptr;

  Planner::declareParam<double>("range", this, &OptimizingRRT::setRange, &OptimizingRRT::getRange, "0.:1.:10000.");
  Planner::declareParam<double>("goal_bias", this, &OptimizingRRT::setGoalBias, &OptimizingRRT::getGoalBias, "0.:.05:1.");
}

OptimizingRRT::~OptimizingRRT()
{
  freeMemory();
}

void OptimizingRRT::clear()
{
  setup_ = false;
  Planner::clear();
  sampler_.reset();
  freeMemory();
  if (nn_)
    nn_->clear();
  lastGoalMotion_ = nullptr;
  startMotions_.clear();

  bestCost_ = ob::Cost(std::numeric_limits<double>::quiet_NaN());
}

void OptimizingRRT::setup()
{
  Planner::setup();
  ompl::tools::SelfConfig sc(si_, getName());
  sc.configurePlannerRange(maxDistance_);

  if (!si_->getStateSpace()->hasSymmetricDistance() || !si_->getStateSpace()->hasSymmetricInterpolate()) {
    OMPL_WARN("%s requires a state space with symmetric distance and symmetric interpolation.", getName().c_str());
  }

  if (!nn_) {
    nn_.reset(ompl::tools::SelfConfig::getDefaultNearestNeighbors<Motion*>(this));
  }
  nn_->setDistanceFunction([this](const Motion *a, const Motion *b)
      {
        return distanceFunction(a, b);
      });

  if (pdef_) {
    if (pdef_->hasOptimizationObjective()) {
      opt_ = pdef_->getOptimizationObjective();
    }
    else {
      OMPL_INFORM("%s: No optimization objective specified. Defaulting to optimizing path length for the allowed "
                  "planning time.",
                  getName().c_str());
      opt_ = std::make_shared<ob::PathLengthOptimizationObjective>(si_);

      // Store the new objective in the problem def'n
      pdef_->setOptimizationObjective(opt_);
    }
    // Set the bestCost_ and prunedCost_ as infinite
    bestCost_ = opt_->infiniteCost();
  }
  else
  {
  OMPL_INFORM("%s: problem definition is not set, deferring setup completion...", getName().c_str());
  setup_ = false;
  }

}

void OptimizingRRT::freeMemory()
{
  if (nn_)
  {
    std::vector<Motion*> motions;
    nn_->list(motions);
    for (auto &motion : motions)
    {
      if (motion->state)
        si_->freeState(motion->state);
      delete motion;
    }
  }
}

ob::PlannerTerminationCondition OptimizingRRT::getTimePlannerTerminationCondition(double solve_time) const
{
  if (solve_time < 1.0)
  {
    return ob::timedPlannerTerminationCondition(solve_time);
  }
  else
  {
    return ob::timedPlannerTerminationCondition(solve_time, std::min(solve_time / 100.0, 0.1));
  }
}

ob::PlannerStatus OptimizingRRT::solveWithMinNumOfSamples(int fixed_num_of_samples, double solve_time)
{
  ob::PlannerTerminationCondition num_of_samples_ptc([this, fixed_num_of_samples]() -> bool
  {
    return num_of_sampled_states_ > fixed_num_of_samples;
  });
  if (solve_time <= 0.0)
  {
    return solve(num_of_samples_ptc);
  }
  else
  {
    ob::PlannerTerminationCondition time_ptc = getTimePlannerTerminationCondition(solve_time);
    ob::PlannerTerminationCondition ptc = ob::plannerAndTerminationCondition(num_of_samples_ptc, time_ptc);
    return solve(ptc);
  }
}

ob::PlannerStatus OptimizingRRT::solveWithMinNumOfValidSamples(int fixed_num_of_valid_samples, double solve_time)
{
  ob::PlannerTerminationCondition num_of_samples_ptc([this, fixed_num_of_valid_samples]() -> bool
  {
    return num_of_valid_sampled_states_ > fixed_num_of_valid_samples;
  });
  if (solve_time <= 0.0)
  {
    return solve(num_of_samples_ptc);
  }
  else
  {
    ob::PlannerTerminationCondition time_ptc = getTimePlannerTerminationCondition(solve_time);
    ob::PlannerTerminationCondition ptc = ob::plannerAndTerminationCondition(num_of_samples_ptc, time_ptc);
    return solve(ptc);
  }
}

ob::PlannerStatus OptimizingRRT::solve(const ob::PlannerTerminationCondition &ptc)
{
  checkValidity();
  ob::Goal *goal = pdef_->getGoal().get();
  ob::GoalSampleableRegion *goal_s = dynamic_cast<ob::GoalSampleableRegion *>(goal);

  bool symCost = opt_->isSymmetric();

  // Check if there are more starts
  if (pis_.haveMoreStartStates() == true)
  {
    // There are, add them
    while (const ob::State *st = pis_.nextStart())
    {
      auto *motion = new Motion(si_);
      si_->copyState(motion->state, st);
      motion->cost = opt_->identityCost();
      nn_->add(motion);
      startMotions_.push_back(motion);
    }
  }
  // No else

  if (nn_->size() == 0)
  {
    OMPL_ERROR("%s: There are no valid initial states!", getName().c_str());
    return ob::PlannerStatus::INVALID_START;
  }

  // Allocate a sampler if necessary
  if (!sampler_) {
    sampler_ = si_->allocStateSampler();
  }

  OMPL_INFORM("%s: Starting planning with %u states already in datastructure", getName().c_str(), nn_->size());

  const ob::ReportIntermediateSolutionFn intermediateSolutionCallback = pdef_->getIntermediateSolutionCallback();

  Motion *solution = lastGoalMotion_;
  ob::Cost bestCost(std::numeric_limits<double>::infinity());

  Motion *approximation = nullptr;
  double approximatedist = std::numeric_limits<double>::infinity();
  bool sufficientlyShort = false;

  auto *rmotion = new Motion(si_);
  ob::State *rstate = rmotion->state;
  ob::State *xstate = si_->allocState();

  std::vector<ob::Cost> costs;

  num_of_sampled_states_ = 0;
  num_of_valid_sampled_states_ = 0;

  if (solution) {
    OMPL_INFORM("%s: Starting planning with existing solution of cost %.5f", getName().c_str(),
                solution->cost.value());
  }

  while (ptc == false)
  {
    /* sample random state (with goal biasing) */
    if (goal_s && rng_.uniform01() < goalBias_ && goal_s->canSample())
      goal_s->sampleGoal(rstate);
    else
      sampler_->sampleUniform(rstate);
    ++num_of_sampled_states_;

    /* find closest state in the tree */
    Motion *nmotion = nn_->nearest(rmotion);
    ob::State *dstate = rstate;

    /* find state to add */
    double d = si_->distance(nmotion->state, rstate);
    if (d > maxDistance_)
    {
      si_->getStateSpace()->interpolate(nmotion->state, rstate, maxDistance_ / d, xstate);
      dstate = xstate;
    }

    if (si_->checkMotion(nmotion->state, dstate))
    {
      ++num_of_valid_sampled_states_;

      /* create a motion */
      Motion *motion = new Motion(si_);
      si_->copyState(motion->state, dstate);
      motion->parent = nmotion;
      motion->incCost = opt_->motionCost(nmotion->state, motion->state);
      motion->cost = opt_->combineCosts(nmotion->cost, motion->incCost);

      nn_->add(motion);
      double dist = 0.0;
      bool sat = goal->isSatisfied(motion->state, &dist);
      if (sat)
      {
        approximatedist = dist;
        if (opt_->isCostBetterThan(motion->cost, bestCost)) {
          solution = motion;
          bestCost = motion->cost;
          std::cout << "Cost of best solution so far: " << bestCost << std::endl;
        }
//        break;
      }
      if (dist < approximatedist)
      {
        approximatedist = dist;
        approximation = motion;
        std::cout << "Approximate distance to goal: " << approximatedist << std::endl;
      }
      std::cout << "Sampled states: " << num_of_sampled_states_ << " of which " << num_of_valid_sampled_states_ << " are valid" << std::endl;
    }
  }

  bool solved = false;
  bool approximate = false;
  if (solution == nullptr)
  {
    solution = approximation;
    approximate = true;
  }

  if (solution != nullptr) {
    lastGoalMotion_ = solution;

    std::cout << "Found solution with cost: " << bestCost << std::endl;

    /* construct the solution path */
    std::vector<Motion*> mpath;
    while (solution != nullptr) {
      mpath.push_back(solution);
      solution = solution->parent;
    }

    /* set the solution path */
    og::PathGeometric *path = new og::PathGeometric(si_);
    for (int i = mpath.size() - 1 ; i >= 0 ; --i) {
      path->append(mpath[i]->state);
    }
    pdef_->addSolutionPath(ob::PathPtr(path), approximate, approximatedist, getName());
    solved = true;
  }

  si_->freeState(xstate);
  if (rmotion->state) {
    si_->freeState(rmotion->state);
  }
  delete rmotion;

  OMPL_INFORM("%s: Created %u states", getName().c_str(), nn_->size());

  return ob::PlannerStatus(solved, approximate);
}

void OptimizingRRT::getPlannerData(ob::PlannerData &data) const
{
  Planner::getPlannerData(data);

  std::vector<Motion*> motions;
  if (nn_)
    nn_->list(motions);

  if (lastGoalMotion_)
    data.addGoalVertex(ob::PlannerDataVertex(lastGoalMotion_->state));

  for (unsigned int i = 0 ; i < motions.size() ; ++i)
  {
    if (motions[i]->parent == nullptr)
      data.addStartVertex(ob::PlannerDataVertex(motions[i]->state));
    else
      data.addEdge(ob::PlannerDataVertex(motions[i]->parent->state),
                   ob::PlannerDataVertex(motions[i]->state));
  }
}
