/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011, Willow Garage, Inc.
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
 *   * Neither the name of Willow Garage nor the names of its
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

/* Author: Ioan Sucan */

#include <moveit/ompl_interface/detail/constrained_sampler.h>
#include <moveit/ompl_interface/model_based_planning_context.h>
#include <moveit/profiler/profiler.h>

#include <ompl/base/spaces/SE3StateSpace.h>
#include "/home/zizo/haptics-ctrl_ws/src/gmm_gmr/include/doRegression.h"


#include <utility>

ompl_interface::ConstrainedSampler::ConstrainedSampler(const ModelBasedPlanningContext* pc,
                                                       constraint_samplers::ConstraintSamplerPtr cs)
  : ob::StateSampler(pc->getOMPLStateSpace().get())
  , planning_context_(pc)
  , default_(space_->allocDefaultStateSampler())
  , constraint_sampler_(std::move(cs))
  , work_state_(pc->getCompleteInitialRobotState())
  , constrained_success_(0)
  , constrained_failure_(0)
{
  inv_dim_ = space_->getDimension() > 0 ? 1.0 / (double)space_->getDimension() : 1.0;

  std::cout << "State Dimension (ConstrainedSampler): " << space_->getDimension() << std::endl;
  std::cout << "StateSpace Name (ConstrainedSampler): " << space_->getName() << std::endl;    
  std::cout << "StateSpace Type (ConstrainedSampler): " << space_->getType() << std::endl;

  getGMMfromBag(gmm_X, gmm_means, gmm_weights, gmm_covariances);
}

double ompl_interface::ConstrainedSampler::getConstrainedSamplingRate() const
{
  if (constrained_success_ == 0)
    return 0.0;
  else
    return (double)constrained_success_ / (double)(constrained_success_ + constrained_failure_);
}

bool ompl_interface::ConstrainedSampler::sampleC(ob::State* state)
{
  std::cout << "ConstrainedSampler sampleC()" << std::endl;
  //  moveit::Profiler::ScopedBlock sblock("sampleWithConstraints");

  if (constraint_sampler_->sample(work_state_, planning_context_->getCompleteInitialRobotState(), //Produces an IK sample - Zizo added this comment
                                  planning_context_->getMaximumStateSamplingAttempts()))
  {
    std::cout << "state.position.x(before): " << 
      state->as<ompl::base::RealVectorStateSpace::StateType>()->values[0] << std::endl;
    std::cout << "state.position.y(before): " << 
      state->as<ompl::base::RealVectorStateSpace::StateType>()->values[1] << std::endl;
    std::cout << "state.position.z(before): " << 
      state->as<ompl::base::RealVectorStateSpace::StateType>()->values[2] << std::endl;

    ompl::RNG rnd_input;
    std::vector<float> prestate = doRegression(rnd_input.uniformReal(0.0, 0.6), gmm_X, gmm_means, gmm_weights, gmm_covariances);
    
    state->as<ompl::base::RealVectorStateSpace::StateType>()->values[0] = prestate[0];
    state->as<ompl::base::RealVectorStateSpace::StateType>()->values[1] = prestate[1];
    state->as<ompl::base::RealVectorStateSpace::StateType>()->values[2] = prestate[2];

    std::cout << "state.position.x: " << 
        state->as<ompl::base::RealVectorStateSpace::StateType>()->values[0] << std::endl;
    std::cout << "state.position.y: " << 
        state->as<ompl::base::RealVectorStateSpace::StateType>()->values[1] << std::endl;
    std::cout << "state.position.z: " << 
        state->as<ompl::base::RealVectorStateSpace::StateType>()->values[2] << std::endl;
            
    planning_context_->getOMPLStateSpace()->copyToOMPLState(state, work_state_);
    if (space_->satisfiesBounds(state))
    {
      ++constrained_success_;
      return true;
    }
  }
  ++constrained_failure_;
  return false;
}

void ompl_interface::ConstrainedSampler::sampleUniform(ob::State* state)
{
  std::cout << "ConstrainedSampler sampleUniform()" << std::endl;
  if (!sampleC(state) && !sampleC(state) && !sampleC(state))
    default_->sampleUniform(state);
}

void ompl_interface::ConstrainedSampler::sampleUniformNear(ob::State* state, const ob::State* near,
                                                           const double distance)
{
  std::cout << "ConstrainedSampler sampleUniformNear()" << std::endl;
  if (sampleC(state) || sampleC(state) || sampleC(state))
  {
    double total_d = space_->distance(state, near);
    if (total_d > distance)
    {
      double dist = pow(rng_.uniform01(), inv_dim_) * distance;
      space_->interpolate(near, state, dist / total_d, state);
    }
  }
  else
    default_->sampleUniformNear(state, near, distance);
}

void ompl_interface::ConstrainedSampler::sampleGaussian(ob::State* state, const ob::State* mean, const double stdDev)
{
  std::cout << "ConstrainedSampler sampleGaussian()" << std::endl;
  if (sampleC(state) || sampleC(state) || sampleC(state))
  {
    double total_d = space_->distance(state, mean);
    double distance = rng_.gaussian(0.0, stdDev);
    if (total_d > distance)
    {
      double dist = pow(rng_.uniform01(), inv_dim_) * distance;
      space_->interpolate(mean, state, dist / total_d, state);
    }
  }
  else
    default_->sampleGaussian(state, mean, stdDev);
}
