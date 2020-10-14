/**---LICENSE-BEGIN - DO NOT CHANGE OR MOVE THIS HEADER
 * This file is part of the Neurorobotics Platform software
 * Copyright (C) 2014,2015,2016,2017 Human Brain Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * ---LICENSE-END**/
/*
 * Copyright 2012 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * bla: Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#ifndef DVS_PLUGIN_HPP
#define DVS_PLUGIN_HPP

#include <string>
#include <ros/ros.h>

#include <gazebo/common/Plugin.hh>
#include <gazebo/sensors/CameraSensor.hh>
#include <gazebo/rendering/Camera.hh>
#include <gazebo/util/system.hh>


#include <dvs_msgs/Event.h>
#include <dvs_msgs/EventArray.h>
#include <opencv2/opencv.hpp>
#include "./nvs_op.hpp"

using namespace std;
using namespace cv;

namespace gazebo
{
  class GAZEBO_VISIBLE DvsPlugin : public SensorPlugin
  {
    public:
      DvsPlugin();
      ~DvsPlugin();
      void Load(sensors::SensorPtr _parent, sdf::ElementPtr _sdf);

    protected:
      virtual void OnNewFrame(const unsigned char *_image,
                   unsigned int _width, unsigned int _height,
                   unsigned int _depth, const string &_format);

      unsigned int width, height, depth;
      string format;
      sensors::CameraSensorPtr parentSensor;
      rendering::CameraPtr camera;
      ros::NodeHandle node_handle_;
      ros::Publisher event_pub_;
      string namespace_;

    private: 
      event::ConnectionPtr newFrameConnection;
      Mat curr_image;
      Mat reference_image;
      Mat thresholds;
      Mat difference;
      Mat events;

      float reference_leak;
      float leak_probability;
      float threshold_decay;
      float threshold_increment;
      NVSOperator nvsOp;
      
      bool first_frame;
      float event_threshold;

      void processDelta();
      void fillEvents(Mat *diff, int polarity, vector<dvs_msgs::Event> *events);
      void publishEvents(vector<dvs_msgs::Event> *events);
  };
}
#endif
