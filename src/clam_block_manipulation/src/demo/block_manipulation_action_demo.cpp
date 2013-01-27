/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
  This script combines the three components of block manipulation: block detection,
  interactive block moving, and pick & place into one coherent program using actionlib action
  servers.
*/

// ROS
#include <ros/ros.h>
#include <actionlib/client/simple_action_client.h>

// Clam Msgs
#include <clam_msgs/BlockDetectionAction.h>
#include <clam_msgs/PickPlaceAction.h>
#include <clam_msgs/InteractiveBlockManipulationAction.h>
//#include <clam_msgs/ClamArmAction.h>
#include <clam_msgs/SendHomeService.h>

// C++
//#include <string>
//#include <sstream>

const std::string pick_place_topic = "/pick_place";

namespace clam_msgs
{

class BlockManipulationAction
{
private:
  ros::NodeHandle nh_;

  // Actions
  actionlib::SimpleActionClient<clam_msgs::BlockDetectionAction> block_detection_action_;
  actionlib::SimpleActionClient<clam_msgs::InteractiveBlockManipulationAction> interactive_manipulation_action_;
  actionlib::SimpleActionClient<clam_msgs::PickPlaceAction> pick_place_action_;

  // Action Goals
  clam_msgs::BlockDetectionGoal block_detection_goal_;
  clam_msgs::InteractiveBlockManipulationGoal interactive_manipulation_goal_;
  clam_msgs::PickPlaceGoal pick_place_goal_;

  // Services
  ros::ServiceClient home_service_;
  clam_msgs::SendHomeService home_srv_;;

  // Parameters
  std::string arm_link;
  double gripper_open;
  double gripper_closed;
  double z_up;
  double z_down;
  double block_size;
  bool once;

public:

  BlockManipulationAction() :
    block_detection_action_("block_detection", true),
    interactive_manipulation_action_("interactive_manipulation", true),
    pick_place_action_("pick_place", true)
  {
    // Load parameters -------------------------------------------------------------------

    nh_.param<std::string>("/block_manipulation_action_demo/arm_link", arm_link, "/base_link");
    nh_.param<double>("/block_manipulation_action_demo/gripper_open", gripper_open, 0.042);
    nh_.param<double>("/block_manipulation_action_demo/gripper_closed", gripper_closed, 0.024);
    nh_.param<double>("/block_manipulation_action_demo/z_up", z_up, 0.12);
    nh_.param<double>("/block_manipulation_action_demo/table_height", z_down, 0.01);
    nh_.param<double>("/block_manipulation_action_demo/block_size", block_size, 0.03);
    nh_.param<bool>("once", once, false);

    ROS_INFO("Block size %f", block_size);
    ROS_INFO("Table height %f", z_down);

    // Initialize goals -------------------------------------------------------------------

    // Block Detection
    block_detection_goal_.frame = arm_link;
    block_detection_goal_.table_height = z_down;
    block_detection_goal_.block_size = block_size;

    // Pick and Place
    pick_place_goal_.frame = arm_link;
    pick_place_goal_.z_up = z_up;
    pick_place_goal_.gripper_open = gripper_open;
    pick_place_goal_.gripper_closed = gripper_closed;
    pick_place_goal_.topic = pick_place_topic;

    // Interactive Manipulation
    interactive_manipulation_goal_.block_size = block_size;
    interactive_manipulation_goal_.frame = arm_link;

    // Send home
    home_service_ = nh_.serviceClient<clam_msgs::SendHomeService>("/send_home");
    home_srv_.request.sendHome = true;

    // Wait for servers -------------------------------------------------------------------
    ROS_INFO("Finished initializing, waiting for servers:");

    ROS_INFO("- Waiting for block detection server.");
    block_detection_action_.waitForServer();

    ROS_INFO("- Waiting for interactive manipulation.");
    interactive_manipulation_action_.waitForServer();

    ROS_INFO("- Waiting for pick and place server.");
    pick_place_action_.waitForServer();

    ROS_INFO("- Waiting for send home service.");
    home_service_.waitForExistence();

    ROS_INFO(" ");
    resetArm();
  }

  void resetArm()
  {
    ROS_INFO("1. Resetting arm to home position");
    if (home_service_.call(home_srv_))
    {
      detectBlocks();
      //skipPerception();
    }
    else
    {
      ROS_ERROR_STREAM("Failed to call service /send_home \n" << home_srv_.response);
      return;
    }
  }

  void skipPerception()
  {
    ROS_INFO("1.1 Skipping perception, sending goal");
    pick_place_action_.sendGoal(pick_place_goal_,
                                boost::bind( &BlockManipulationAction::finish, this, _1, _2));
  }

  void detectBlocks()
  {
    ROS_INFO("2. Detecting blocks using PCL");
    block_detection_action_.sendGoal(block_detection_goal_,
                                     boost::bind( &BlockManipulationAction::addBlocks, this, _1, _2));
  }

  void addBlocks(const actionlib::SimpleClientGoalState& state, const BlockDetectionResultConstPtr& result)
  {
    geometry_msgs::Pose block;

    if (state == actionlib::SimpleClientGoalState::SUCCEEDED)
      ROS_INFO("3. Detected blocks, adding to Rviz. Waiting for user input.");
    else
    {
      ROS_ERROR("3. Failed to detect blocks: %s",  state.toString().c_str());
      ros::shutdown();
    }

    interactive_manipulation_action_.sendGoal(interactive_manipulation_goal_,
                                              boost::bind( &BlockManipulationAction::pickAndPlace,
                                                           this, _1, _2));
  }

  void pickAndPlace(const actionlib::SimpleClientGoalState& state,
                    const InteractiveBlockManipulationResultConstPtr& result)
  {
    if (state == actionlib::SimpleClientGoalState::SUCCEEDED)
    {
      ROS_INFO("4. Rviz interactive marker recieved, moving arm");
    }
    else
    {
      ROS_ERROR("4. Rviz interactive marker input did not succeed: %s",  state.toString().c_str());
      ros::shutdown();
    }
    pick_place_action_.sendGoal(pick_place_goal_,
                                boost::bind( &BlockManipulationAction::finish, this, _1, _2));
  }

  void finish(const actionlib::SimpleClientGoalState& state, const PickPlaceResultConstPtr& result)
  {
    if (state == actionlib::SimpleClientGoalState::SUCCEEDED)
      ROS_INFO("5. Pick and place commands successfull");
    else
      ROS_ERROR("6. Pick and place did not succeed: %s",  state.toString().c_str());

    if (once)
    {
      ROS_INFO("Shutting down");
      ros::shutdown();
    }
    else
    {
       ROS_INFO(" ");
       ROS_INFO("Restarting Demo --------------------------------------------- ");

       resetArm();
    }
  }
};

};

int main(int argc, char** argv)
{
  // initialize node
  ros::init(argc, argv, "block_manipulation");

  clam_msgs::BlockManipulationAction demo;

  // everything is done in cloud callback, just spin
  ros::spin();
}

