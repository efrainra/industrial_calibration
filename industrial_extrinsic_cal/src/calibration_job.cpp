/*
 * Software License Agreement (Apache License)
 *
 * Copyright (c) 2013, Southwest Research Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ostream>
#include <stdio.h>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <boost/foreach.hpp>
#include "ceres/ceres.h"
#include "ceres/rotation.h"
#include <industrial_extrinsic_cal/basic_types.h>
#include <industrial_extrinsic_cal/calibration_job.hpp>
#include <industrial_extrinsic_cal/camera_observer.hpp>
#include <industrial_extrinsic_cal/ceres_costs_utils.hpp>

// ROS includes
#include <ros/ros.h>
#include <camera_info_manager/camera_info_manager.h>

namespace industrial_extrinsic_cal
{

using std::string;
using boost::shared_ptr;
using boost::make_shared;
using ceres::CostFunction;
Camera::Camera()
{
  camera_name_ = "NONE";
  is_moving_ = false;
}

Camera::Camera(string name, CameraParameters camera_parameters, bool is_moving) :
    camera_name_(name), camera_parameters_(camera_parameters_), is_moving_(is_moving)
{
}

Camera::~Camera()
{
}

bool Camera::isMoving()
{
  return (is_moving_);
}

void ObservationScene::addObservationToScene(ObservationCmd new_obs_cmd)
{
  // this next block of code maintains a list of the cameras in a scene
  bool camera_already_in_scene = false;
  BOOST_FOREACH(ObservationCmd command, observation_command_list_)
  {
    BOOST_FOREACH(shared_ptr<Camera> camera, cameras_in_scene_)
    {
      if (camera->camera_name_ == new_obs_cmd.camera->camera_name_)
      {
        camera_already_in_scene = true;
      }
    }
  }
  if (!camera_already_in_scene)
  {
    cameras_in_scene_.push_back(new_obs_cmd.camera);
  }
  // end of code block to maintain list of cameras in scene

  // add observation
  observation_command_list_.push_back(new_obs_cmd);
}
CeresBlocks::CeresBlocks()
{
}
CeresBlocks::~CeresBlocks()
{
  clearCamerasTargets();
}
void CeresBlocks::clearCamerasTargets()
{
  static_cameras_.clear();
  static_targets_.clear();
  moving_cameras_.clear();
  moving_targets_.clear();
}
P_BLOCK CeresBlocks::getStaticCameraParameterBlockIntrinsics(string camera_name)
{
  // static cameras should have unique name
  BOOST_FOREACH(shared_ptr<Camera> camera, static_cameras_)
  {
    if (camera_name == camera->camera_name_)
    {
      P_BLOCK intrinsics = &(camera->camera_parameters_.pb_intrinsics[0]);
      return (intrinsics);
    }
  }
  return (NULL);
}
P_BLOCK CeresBlocks::getMovingCameraParameterBlockIntrinsics(string camera_name)
{
  // we use the intrinsic parameters from the first time the camera appears in the list
  // subsequent cameras with this name also have intrinsic parameters, but these are
  // never used as parameter blocks, only their extrinsics are used
  BOOST_FOREACH(shared_ptr<MovingCamera> moving_camera, moving_cameras_)
  {
    if (camera_name == moving_camera->cam->camera_name_)
    {
      P_BLOCK intrinsics = &(moving_camera->cam->camera_parameters_.pb_intrinsics[0]);
      return (intrinsics);
    }
  }
  return (NULL);
}
P_BLOCK CeresBlocks::getStaticCameraParameterBlockExtrinsics(string camera_name)
{
  // static cameras should have unique name
  BOOST_FOREACH(shared_ptr<Camera> camera, static_cameras_)
  {
    if (camera_name == camera->camera_name_)
    {
      P_BLOCK extrinsics = &(camera->camera_parameters_.pb_extrinsics[0]);
      return (extrinsics);
    }
  }
  return (NULL);

}
P_BLOCK CeresBlocks::getMovingCameraParameterBlockExtrinsics(string camera_name, int scene_id)
{
  BOOST_FOREACH(shared_ptr<MovingCamera> camera, moving_cameras_)
  {
    if (camera_name == camera->cam->camera_name_ && scene_id == camera->scene_id)
    {
      P_BLOCK extrinsics = &(camera->cam->camera_parameters_.pb_extrinsics[0]);
      return (extrinsics);
    }
  }
  return (NULL);

}
P_BLOCK CeresBlocks::getStaticTargetPoseParameterBlock(string target_name)
{
  BOOST_FOREACH(shared_ptr<Target> target, static_targets_)
  {
    if (target_name == target->target_name)
    {
      P_BLOCK pose = &(target->pose.pb_pose[0]);
      return (pose);
    }
  }
  return (NULL);
}
P_BLOCK CeresBlocks::getStaticTargetPointParameterBlock(string target_name, int point_id)
{
  BOOST_FOREACH(shared_ptr<Target> target, static_targets_)
  {
    if (target_name == target->target_name)
    {
      P_BLOCK point_position = &(target->pts[point_id].pb[0]);
      return (point_position);
    }
  }
  return (NULL);
}
P_BLOCK CeresBlocks::getMovingTargetPoseParameterBlock(string target_name, int scene_id)
{
  BOOST_FOREACH(shared_ptr<MovingTarget> moving_target, moving_targets_)
  {
    if (target_name == moving_target->targ->target_name && scene_id == moving_target->scene_id)
    {
      P_BLOCK pose = &(moving_target->targ->pose.pb_pose[0]);
      return (pose);
    }
  }
  return (NULL);
}
P_BLOCK CeresBlocks::getMovingTargetPointParameterBlock(string target_name, int pnt_id)
{
  // note scene_id unnecessary here since regarless of scene th point's location relative to
  // the target frame does not change
  BOOST_FOREACH(shared_ptr<MovingTarget> moving_target, moving_targets_)
  {
    if (target_name == moving_target->targ->target_name)
    {
      P_BLOCK point_position = &(moving_target->targ->pts[pnt_id].pb[0]);
      return (point_position);
    }
  }
  return (NULL);
}

ObservationDataPointList::ObservationDataPointList()
{
}
;

ObservationDataPointList::~ObservationDataPointList()
{
}
;

void ObservationDataPointList::addObservationPoint(ObservationDataPoint new_data_point)
{
  items.push_back(new_data_point);
}

bool CalibrationJob::run()
{
  runObservations();
  runOptimization();
}

bool CalibrationJob::runObservations()
{
  this->ceres_blocks_.clearCamerasTargets();
  // For each scene
  BOOST_FOREACH(ObservationScene current_scene, scene_list_)
  {
    int scene_id = current_scene.get_id();

    // clear all observations from every camera
    ROS_INFO_STREAM("Processing Scene " << scene_id);

    // add observations to every camera
    BOOST_FOREACH(shared_ptr<Camera> current_camera, current_scene.cameras_in_scene_)
    {
      current_camera->camera_observer_->clearObservations(); // clear any recorded data
      current_camera->camera_observer_->clearTargets(); // clear all targets
    }

    // add each target to each cameras observations
    BOOST_FOREACH(ObservationCmd o_command, current_scene.observation_command_list_)
    {
      // configure to find target in roi
      o_command.camera->camera_observer_->addTarget(o_command.target, o_command.roi);
    }
    // trigger the cameras
    BOOST_FOREACH( shared_ptr<Camera> current_camera, current_scene.cameras_in_scene_)
    {
      current_camera->camera_observer_->triggerCamera();
    }
    // collect results
    P_BLOCK intrinsics;
    P_BLOCK extrinsics;
    P_BLOCK target_pose;
    P_BLOCK pnt_pos;
    std::string camera_name;
    std::string target_name;

    // for each camera in scene
    BOOST_FOREACH( shared_ptr<Camera> camera, current_scene.cameras_in_scene_)
    {
      // wait until observation is done
      while (!camera->camera_observer_->observationsDone())
        ;

      camera_name = camera->camera_name_;
      if (camera->isMoving())
      {
        // next line does nothing if camera already exist in blocks
        ceres_blocks_.addMovingCamera(camera, scene_id);
        intrinsics = ceres_blocks_.getMovingCameraParameterBlockIntrinsics(camera_name);
        extrinsics = ceres_blocks_.getMovingCameraParameterBlockExtrinsics(camera_name, scene_id);
      }
      else
      {
        // next line does nothing if camera already exist in blocks
        ceres_blocks_.addStaticCamera(camera);
        intrinsics = ceres_blocks_.getStaticCameraParameterBlockIntrinsics(camera_name);
        extrinsics = ceres_blocks_.getStaticCameraParameterBlockExtrinsics(camera_name);
      }

      // Get the observations
      CameraObservations camera_observations;
      int number_returned;
      number_returned = camera->camera_observer_->getObservations(camera_observations);

      BOOST_FOREACH(Observation observation, camera_observations.observations)
      {
        target_name = observation.target->target_name;
        int pnt_id = observation.point_id;
        double observation_x = observation.image_loc_x;
        double observation_y = observation.image_loc_y;
        if (observation.target->is_moving)
        {
          ceres_blocks_.addMovingTarget(observation.target, scene_id);
          target_pose = ceres_blocks_.getMovingTargetPoseParameterBlock(target_name, scene_id);
          pnt_pos = ceres_blocks_.getMovingTargetPointParameterBlock(target_name, pnt_id);
        }
        else
        {
          ceres_blocks_.addStaticTarget(observation.target); // if exist, does nothing
          target_pose = ceres_blocks_.getStaticTargetPoseParameterBlock(target_name);
          pnt_pos = ceres_blocks_.getStaticTargetPointParameterBlock(target_name, pnt_id);
        }
        ObservationDataPoint temp_ODP(camera_name, target_name, scene_id, intrinsics, extrinsics, pnt_id, target_pose,
                                      pnt_pos, observation_x, observation_y);
        observation_data_point_list_.addObservationPoint(temp_ODP);
      }
    }
  } // end for each scene
}

bool CalibrationJob::load()
{
  std::ifstream camera_input_file(camera_def_file_name_.c_str());
  std::ifstream target_input_file(target_def_file_name_.c_str());
  std::ifstream caljob_input_file(caljob_def_file_name_.c_str());
  if (camera_input_file.fail())
  {
    ROS_ERROR_STREAM(
        "ERROR CalibrationJob::load(), couldn't open camera_input_file:    "<< camera_def_file_name_.c_str());
    return (false);
  }
  if (target_input_file.fail())
  {
    ROS_ERROR_STREAM(
        "ERROR CalibrationJob::load(), couldn't open target_input_file:    "<< target_def_file_name_.c_str());
    return (false);
  }
  if (caljob_input_file.fail())
  {
    ROS_ERROR_STREAM(
        "ERROR CalibrationJob::load(), couldn't open caljob_input_file:    "<< caljob_def_file_name_.c_str());
    return (false);
  }

  string temp_name;
  CameraParameters temp_parameters;
  unsigned int scene_id;
  try
  {
    YAML::Parser camera_parser(camera_input_file);
    YAML::Node camera_doc;
    camera_parser.GetNextDocument(camera_doc);

    // read in all static cameras
    if (const YAML::Node *camera_parameters = camera_doc.FindValue("static_cameras"))
    {
      ROS_INFO_STREAM("Found "<<camera_parameters->size()<<" static cameras ");
      for (unsigned int i = 0; i < camera_parameters->size(); i++)
      {
        (*camera_parameters)[i]["camera_name"] >> temp_name;
        (*camera_parameters)[i]["angle_axis_ax"] >> temp_parameters.angle_axis[0];
        (*camera_parameters)[i]["angle_axis_ay"] >> temp_parameters.angle_axis[1];
        (*camera_parameters)[i]["angle_axis_az"] >> temp_parameters.angle_axis[2];
        (*camera_parameters)[i]["position_x"] >> temp_parameters.position[0];
        (*camera_parameters)[i]["position_y"] >> temp_parameters.position[1];
        (*camera_parameters)[i]["position_z"] >> temp_parameters.position[2];
        (*camera_parameters)[i]["focal_length_x"] >> temp_parameters.focal_length_x;
        (*camera_parameters)[i]["focal_length_y"] >> temp_parameters.focal_length_y;
        (*camera_parameters)[i]["center_x"] >> temp_parameters.center_x;
        (*camera_parameters)[i]["center_y"] >> temp_parameters.center_y;
        (*camera_parameters)[i]["distortion_k1"] >> temp_parameters.distortion_k1;
        (*camera_parameters)[i]["distortion_k2"] >> temp_parameters.distortion_k2;
        (*camera_parameters)[i]["distortion_k3"] >> temp_parameters.distortion_k3;
        (*camera_parameters)[i]["distortion_p1"] >> temp_parameters.distortion_p1;
        (*camera_parameters)[i]["distortion_p2"] >> temp_parameters.distortion_p2;
        // create a static camera
        shared_ptr<Camera> temp_camera = make_shared<Camera>(temp_name, temp_parameters, false);

        ceres_blocks_.addStaticCamera(temp_camera);
      }
    }

    // read in all moving cameras
    if (const YAML::Node *camera_parameters = camera_doc.FindValue("moving_cameras"))
    {
      ROS_INFO_STREAM("Found "<<camera_parameters->size() << " moving cameras ");
      for (unsigned int i = 0; i < camera_parameters->size(); i++)
      {
        (*camera_parameters)[i]["camera_name"] >> temp_name;
        (*camera_parameters)[i]["angle_axis_ax"] >> temp_parameters.angle_axis[0];
        (*camera_parameters)[i]["angle_axis_ay"] >> temp_parameters.angle_axis[1];
        (*camera_parameters)[i]["angle_axis_az"] >> temp_parameters.angle_axis[2];
        (*camera_parameters)[i]["position_x"] >> temp_parameters.position[0];
        (*camera_parameters)[i]["position_y"] >> temp_parameters.position[1];
        (*camera_parameters)[i]["position_z"] >> temp_parameters.position[2];
        (*camera_parameters)[i]["focal_length_x"] >> temp_parameters.focal_length_x;
        (*camera_parameters)[i]["focal_length_y"] >> temp_parameters.focal_length_y;
        (*camera_parameters)[i]["center_x"] >> temp_parameters.center_x;
        (*camera_parameters)[i]["center_y"] >> temp_parameters.center_y;
        (*camera_parameters)[i]["distortion_k1"] >> temp_parameters.distortion_k1;
        (*camera_parameters)[i]["distortion_k2"] >> temp_parameters.distortion_k2;
        (*camera_parameters)[i]["distortion_k3"] >> temp_parameters.distortion_k3;
        (*camera_parameters)[i]["distortion_p1"] >> temp_parameters.distortion_p1;
        (*camera_parameters)[i]["distortion_p2"] >> temp_parameters.distortion_p2;
        (*camera_parameters)[i]["scene_id"] >> scene_id;
        shared_ptr<Camera> temp_camera = make_shared<Camera>(temp_name, temp_parameters, true);
        ceres_blocks_.addMovingCamera(temp_camera, scene_id);
      }
    }
  } // end try
  catch (YAML::ParserException& e)
  {
    ROS_INFO_STREAM("load() Failed to read in moving cameras from  yaml file ");
    ROS_INFO_STREAM("camera name =     "<<temp_name.c_str());
    ROS_INFO_STREAM("angle_axis_ax =  "<<temp_parameters.angle_axis[0]);
    ROS_INFO_STREAM("angle_axis_ay = "<<temp_parameters.angle_axis[1]);
    ROS_INFO_STREAM("angle_axis_az =  "<<temp_parameters.angle_axis[2]);
    ROS_INFO_STREAM("position_x =  "<<temp_parameters.position[0]);
    ROS_INFO_STREAM("position_y =  "<<temp_parameters.position[1]);
    ROS_INFO_STREAM("position_z =  "<<temp_parameters.position[2]);
    ROS_INFO_STREAM("focal_length_x =  "<<temp_parameters.focal_length_x);
    ROS_INFO_STREAM("focal_length_y =  "<<temp_parameters.focal_length_y);
    ROS_INFO_STREAM("center_x = "<<temp_parameters.center_x);
    ROS_INFO_STREAM("center_y =  "<<temp_parameters.center_y);
    ROS_INFO_STREAM("distortion_k1 =  "<<temp_parameters.distortion_k1);
    ROS_INFO_STREAM("distortion_k2 =  "<<temp_parameters.distortion_k2);
    ROS_INFO_STREAM("distortion_k3 =  "<<temp_parameters.distortion_k3);
    ROS_INFO_STREAM("distortion_p1 =  "<<temp_parameters.distortion_p1);
    ROS_INFO_STREAM("distortion_p2 =  "<<temp_parameters.distortion_p2);
    ROS_INFO_STREAM("scene_id = "<<scene_id);
    ROS_ERROR("load() Failed to read in cameras yaml file");
    ROS_ERROR_STREAM("Failed with exception "<< e.what());
    return (false);
  }
  ROS_INFO_STREAM("Successfully read in cameras ");

  Target temp_target;
  try
  {
    YAML::Parser target_parser(target_input_file);
    YAML::Node target_doc;
    target_parser.GetNextDocument(target_doc);

    // read in all static targets
    if (const YAML::Node *target_parameters = target_doc.FindValue("static_targets"))
    {
      ROS_INFO_STREAM("Found "<<target_parameters->size() <<" targets ");
      shared_ptr<Target> temp_target = make_shared<Target>();
      temp_target->is_moving = false;
      for (unsigned int i = 0; i < target_parameters->size(); i++)
      {
        (*target_parameters)[i]["target_name"] >> temp_target->target_name;
        (*target_parameters)[i]["angle_axis_ax"] >> temp_target->pose.ax;
        (*target_parameters)[i]["angle_axis_ay"] >> temp_target->pose.ay;
        (*target_parameters)[i]["angle_axis_az"] >> temp_target->pose.az;
        (*target_parameters)[i]["position_x"] >> temp_target->pose.x;
        (*target_parameters)[i]["position_y"] >> temp_target->pose.y;
        (*target_parameters)[i]["position_z"] >> temp_target->pose.z;
        (*target_parameters)[i]["num_points"] >> temp_target->num_points;
        const YAML::Node *points_node = (*target_parameters)[i].FindValue("points");
        for (int j = 0; j < points_node->size(); j++)
        {
          const YAML::Node *pnt_node = (*points_node)[j].FindValue("pnt");
          std::vector<float> temp_pnt;
          (*pnt_node) >> temp_pnt;
          Point3d temp_pnt3d;
          temp_pnt3d.x = temp_pnt[0];
          temp_pnt3d.y = temp_pnt[1];
          temp_pnt3d.z = temp_pnt[2];
          temp_target->pts.push_back(temp_pnt3d);
        }
        ceres_blocks_.addStaticTarget(temp_target);
      }
    }

    // read in all moving targets
    if (const YAML::Node *target_parameters = target_doc.FindValue("moving_targets"))
    {
      ROS_INFO_STREAM("Found "<<target_parameters->size() <<"  moving targets ");
      shared_ptr<Target> temp_target = make_shared<Target>();
      unsigned int scene_id;
      temp_target->is_moving = true;
      for (unsigned int i = 0; i < target_parameters->size(); i++)
      {
        (*target_parameters)[i]["target_name"] >> temp_target->target_name;
        (*target_parameters)[i]["angle_axis_ax"] >> temp_target->pose.ax;
        (*target_parameters)[i]["angle_axis_ax"] >> temp_target->pose.ay;
        (*target_parameters)[i]["angle_axis_ay"] >> temp_target->pose.az;
        (*target_parameters)[i]["position_x"] >> temp_target->pose.x;
        (*target_parameters)[i]["position_y"] >> temp_target->pose.y;
        (*target_parameters)[i]["position_z"] >> temp_target->pose.z;
        (*target_parameters)[i]["scene_id"] >> scene_id;
        (*target_parameters)[i]["num_points"] >> temp_target->num_points;
        const YAML::Node *points_node = (*target_parameters)[i].FindValue("points");
        for (int j = 0; j < points_node->size(); j++)
        {
          const YAML::Node *pnt_node = (*points_node)[j].FindValue("pnt");
          std::vector<float> temp_pnt;
          (*pnt_node) >> temp_pnt;
          Point3d temp_pnt3d;
          temp_pnt3d.x = temp_pnt[0];
          temp_pnt3d.y = temp_pnt[1];
          temp_pnt3d.z = temp_pnt[2];
          temp_target->pts.push_back(temp_pnt3d);
        }
        ceres_blocks_.addMovingTarget(temp_target, scene_id);
      }
    }
    ROS_INFO_STREAM("Successfully read targets ");
  } // end try
  catch (YAML::ParserException& e)
  {
    ROS_ERROR("load() Failed to read in target yaml file");
    ROS_ERROR_STREAM("Failed with exception "<< e.what());
    return (false);
  }
  //	ROS_ERROR("load() Failed to read in cameras yaml file");
  //Target temp_target;
  //Read in cal job parameters
  try
  {
    YAML::Parser caljob_parser(caljob_input_file);
    YAML::Node caljob_doc;
    caljob_parser.GetNextDocument(caljob_doc);

    shared_ptr<Target> temp_target = make_shared<Target>();

    caljob_doc["reference_frame"] >> temp_target->target_name;
    caljob_doc["optimization_parameters"] >> temp_target->target_name;
    // read in all scenes
    if (const YAML::Node *caljob_scenes = caljob_doc.FindValue("scenes"))
    {
      ROS_INFO_STREAM("Found "<<caljob_scenes->size() <<" scenes");
      for (unsigned int i = 0; i < caljob_scenes->size(); i++)
      {
        (*caljob_scenes)[i]["scene_id"] >> temp_target->target_name;
        //ROS_INFO_STREAM("scene "<<temp_target->target_name);
        (*caljob_scenes)[i]["trigger_type"] >> temp_target->target_name;
        //ROS_INFO_STREAM("trig type "<<temp_target->target_name);
        const YAML::Node *obs_node = (*caljob_scenes)[i].FindValue("observations");
        ROS_INFO_STREAM("Found "<<obs_node->size() <<" observations within scene "<<i);
        for (unsigned int j = 0; j < obs_node->size(); j++)
        {
          ROS_INFO_STREAM("For obs "<<j);
          (*obs_node)[j]["camera"] >> temp_target->target_name;
          (*obs_node)[j]["target"] >> temp_target->target_name;
        }
      }
    }
    ROS_INFO_STREAM("Successfully read caljob  ");
  } // end try
  catch (YAML::ParserException& e)
  {
    ROS_ERROR("load() Failed to read in caljob yaml file");
    ROS_ERROR_STREAM("Failed with exception "<< e.what());
    return (false);
  }
  //    ROS_INFO("successfuly read in cameras");
  return (true);
} // end load()

bool CeresBlocks::addStaticCamera(shared_ptr<Camera> camera_to_add)
{
  BOOST_FOREACH(shared_ptr<Camera> cam, static_cameras_)
  {
    if (cam->camera_name_ == camera_to_add->camera_name_)
      return (false); // camera already exists
  }
  static_cameras_.push_back(camera_to_add);
  return (true);
}
bool CeresBlocks::addStaticTarget(shared_ptr<Target> target_to_add)
{
  BOOST_FOREACH(shared_ptr<Target> targ, static_targets_)
  {
    if (targ->target_name == target_to_add->target_name)
      return (false); // target already exists
  }
  static_targets_.push_back(target_to_add);
  return (true);
}
bool CeresBlocks::addMovingCamera(shared_ptr<Camera> camera_to_add, int scene_id)
{
  BOOST_FOREACH(shared_ptr<MovingCamera> cam, moving_cameras_)
  {
    if (cam->cam->camera_name_ == camera_to_add->camera_name_ && cam->scene_id == scene_id)
      return (false); // camera already exists
  }
  // this next line allocates the memory for a moving camera
  shared_ptr<MovingCamera> temp_moving_camera = make_shared<MovingCamera>();
  // this next line allocates the memory for the actual camera
  shared_ptr<Camera> temp_camera = make_shared<Camera>(camera_to_add->camera_name_, camera_to_add->camera_parameters_,
                                                       true);
  temp_moving_camera->cam = temp_camera;
  temp_moving_camera->scene_id = scene_id;
  moving_cameras_.push_back(temp_moving_camera);
  return (true);
}
bool CeresBlocks::addMovingTarget(shared_ptr<Target> target_to_add, int scene_id)
{
  BOOST_FOREACH(shared_ptr<MovingTarget> targ, moving_targets_)
  {
    if (targ->targ->target_name == target_to_add->target_name && targ->scene_id == scene_id)
      return (false); // target already exists
  }
  shared_ptr<MovingTarget> temp_moving_target = make_shared<MovingTarget>();
  shared_ptr<Target> temp_camera = make_shared<Target>();
  temp_moving_target->targ = target_to_add;
  temp_moving_target->scene_id = scene_id;
  moving_targets_.push_back(temp_moving_target);
  return (true);
}

bool CalibrationJob::runOptimization()
{
  // take all the data collected and create a Ceres optimization problem and run it

  BOOST_FOREACH(ObservationDataPoint ODP, observation_data_point_list_.items)
  {
    // take all the data collected and create a Ceres optimization problem and run it

    BOOST_FOREACH(ObservationDataPoint ODP, observation_data_point_list_.items){
      // create cost function
      // there are several options
      // 1. the complete reprojection error cost function "Create(obs_x,obs_y)"
      //    this cost function has the following parameters:
      //      a. camera intrinsics
      //      b. camera extrinsics
      //      c. target pose
      //      d. point location in target frame
      // 2. the same as 1, but without d  "Create(obs_x,obs_y,t_pnt_x, t_pnt_y, t_pnt_z)
      // 3. the same as 1, but without a  "Create(obs_x,obs_y,fx,fy,cx,cy,cz)"
      //    Note that this one assumes we are using rectified images to compute the observations
      // 4. the same as 3, point location fixed too "Create(obs_x,obs_y,fx,fy,cx,cy,cz,t_x,t_y,t_z)"
      // 5. the same as 4, but with target in known location
      //    "Create(obs_x,obs_y,fx,fy,cx,cy,cz,t_x,t_y,t_z,p_tx,p_ty,p_tz,p_ax,p_ay,p_az)"

      // pull out the constants from the observation point data
      double focal_length_x = ODP.camera_intrinsics_[0]; // TODO, make this not so ugly
      double focal_length_y = ODP.camera_intrinsics_[1];
      double center_pnt_x   = ODP.camera_intrinsics_[2];
      double center_pnt_y   = ODP.camera_intrinsics_[3];
      double image_x        = ODP.image_x_;
      double image_y        = ODP.image_y_;
      double point_x        = ODP.point_position_[0];// location of point within target frame
      double point_y        = ODP.point_position_[1];
      double point_z        = ODP.point_position_[2];
      
      // create the cost function
      CostFunction* cost_function = TargetCameraReprjErrorNoDistortion::Create(image_x, image_y,
									       focal_length_x, 
									       focal_length_y,
									       center_pnt_x,
									       center_pnt_y,
									       point_x,
									       point_y,
									       point_z);

      // pull out pointers to the parameter blocks in the observation point data
      P_BLOCK extrinsics    = ODP.camera_extrinsics_;
      P_BLOCK target_pose   = ODP.target_pose_;

      // add it as a residual using parameter blocks
      problem_.AddResidualBlock(cost_function, NULL , extrinsics, target_pose);

    }

    //    ceres::CostFunction* cost_function =  Camera_reprj_error::Create(Ob[i].x,Ob[i].y);

    //    problem_.AddResidualBlock(cost_function, NULL ,
    //    C.PB_extrinsics,
    //    C.PB_intrinsics,
    //    Pts[i].PB);
    //    problem.SetParameterBlockConstant(C.PB_intrinsics);
    //    problem.SetParameterBlockConstant(Pts[i].PB);


    // Make Ceres automatically detect the bundle structure. Note that the
    // standard solver, SPARSE_NORMAL_CHOLESKY, also works fine but it is slower
    // for standard bundle adjustment problems.
    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_SCHUR;
    options.minimizer_progress_to_stdout = true;
    options.max_num_iterations = 1000;

    ceres::Solver::Summary summary;
    //    ceres::Solve(options, &problem, &summary);


    return true;
  }
  return true;
}
} // end of namespace

using industrial_extrinsic_cal::CalibrationJob;
using std::string;
int main()
{
  string camera_file_name("camera_def.yaml");
  string target_file_name("target_def.yaml");
  string caljob_file_name("caljob_def.yaml");
  CalibrationJob Cal_job(camera_file_name, target_file_name, caljob_file_name);
  printf("hello world\n");
  Cal_job.load();
}
