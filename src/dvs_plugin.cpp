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
 * Copyright 2013 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * IMPLEMENTATION INSPIRED BY
 * https://github.com/PX4/sitl_gazebo/blob/master/src/gazebo_opticalFlow_plugin.cpp
 */

#ifdef _WIN32
// Ensure that Winsock2.h is included before Windows.h, which can get
// pulled in by anybody (e.g., Boost).
#include <Winsock2.h>
#endif

#include <string>
#include <math.h>

#include <ros/ros.h>
#include <ros/console.h>

#include <gazebo/common/Plugin.hh>
#include <gazebo/sensors/CameraSensor.hh>
#include <gazebo/rendering/Camera.hh>
#include <gazebo/util/system.hh>

#include <dvs_msgs/Event.h>
#include <dvs_msgs/EventArray.h>

#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

#include <gazebo_dvs_plugin/dvs_plugin.hpp>

using namespace std;
using namespace cv;

namespace gazebo
{
  template <class T>
  checkAndGet(const std::string field, const sdf::ElementPtr sdf, T & res){
    if (sdf->HasElement("robotNamespace")){
      res = sdf->GetElement(field)->Get<T>();
    } else {
      gzwarn << "[gazebo_ros_dvs_camera] Please specify a " << field << "." << endl;
    }
  }

  // Register this plugin with the simulator
  GZ_REGISTER_SENSOR_PLUGIN(DvsPlugin)

    ////////////////////////////////////////////////////////////////////////////////
    // Constructor
    DvsPlugin::DvsPlugin()
    : SensorPlugin(), width(0), height(0), depth(0), first_frame(true)
  {
  }

  ////////////////////////////////////////////////////////////////////////////////
  // Destructor
  DvsPlugin::~DvsPlugin()
  {
    this->parentSensor.reset();
    this->camera.reset();
  }

  void DvsPlugin::Load(sensors::SensorPtr _sensor, sdf::ElementPtr _sdf)
  {
    if (!_sensor)
      gzerr << "Invalid sensor pointer." << endl;

#if GAZEBO_MAJOR_VERSION >= 7
    this->parentSensor = std::dynamic_pointer_cast<sensors::CameraSensor>(_sensor);
    this->camera = this->parentSensor->Camera();
#else
    this->parentSensor = boost::dynamic_pointer_cast<sensors::CameraSensor>(_sensor);
    this->camera = this->parentSensor->GetCamera();
#endif

    if (!this->parentSensor)
    {
      gzerr << "DvsPlugin not attached to a camera sensor." << endl;
      return;
    }

#if GAZEBO_MAJOR_VERSION >= 7
    this->width = this->camera->ImageWidth();
    this->height = this->camera->ImageHeight();
    this->depth = this->camera->ImageDepth();
    this->format = this->camera->ImageFormat();
#else
    this->width = this->camera->GetImageWidth();
    this->height = this->camera->GetImageHeight();
    this->depth = this->camera->GetImageDepth();
    this->format = this->camera->GetImageFormat();
#endif

    
    checkAndGet<std::string>("robotNamespace", _sdf, namespace_);
    node_handle_ = ros::NodeHandle(namespace_);

    string sensorName = "";
    checkAndGet<std::string>("cameraName", _sdf, sensorName);

    string topicName = "events";
    checkAndGet<std::string>("eventsTopicName", _sdf, topicName);
    const string topic = sensorName + topicName;

    checkAndGet<float>("eventThreshold", _sdf, this->event_threshold);
    this->thresholds.setTo(this->event_threshold);

    //*
#if GAZEBO_MAJOR_VERSION >= 7
    float rate = this->camera->RenderRate();
#else
    float rate = this->camera->GetRenderRate();
#endif
    if (!isfinite(rate))
      rate =  30.0; //N frames per 1 second

    float dt = 1.0 / rate; // seconds
    //*/

    // float fps = 1.0f;
    // checkAndGet<float>("update_rate", _sdf, fps);
    float timestep = 1000.0f * dt; //ms

    float tauThreshold = 1.0f;
    checkAndGet<float>("tauThreshold", _sdf, tauThreshold);
    this->threshold_decay = exp(-timestep/tauThreshold);

    checkAndGet<float>("thresholdIncrement", _sdf, this->threshold_increment);

    float tauLeak = 10000000000000.0f;
    checkAndGet<float>("tauLeak", _sdf, tauLeak);
    this->reference_leak = exp(-timestep/tauLeak);

    checkAndGet<float>("leakProbability", _sdf, this->leak_probability);

    this->curr_image = cv::Mat(this->height, this->width, CV_16FC1);
    this->reference = cv::Mat(this->height, this->width, CV_16FC1);
    this->difference = cv::Mat(this->height, this->width, CV_16FC1);
    this->events = cv::Mat(this->height, this->width, CV_16FC1);
    this->thresholds = \
      cv::Mat(this->height, this->width, CV_16FC1, cv::cvScalar(this->event_threshold));

    this->nvsOp.init(&(this->curr_image), &(this->difference), 
                     &(this->reference), &(this->thresholds), 
                     &(this->events),
                     this->reference_leak, 
                     this->threshold_increment, 
                     this->threshold_decay,
                     this->leak_probability,
                     this->event_threshold 
                    );


    event_pub_ = node_handle_.advertise<dvs_msgs::EventArray>(topic, 10, 10.0);

    this->newFrameConnection = this->camera->ConnectNewImageFrame(
        boost::bind(&DvsPlugin::OnNewFrame, this, _1, this->width, this->height, this->depth, this->format));

    this->parentSensor->SetActive(true);
  }

  ////////////////////////////////////////////////////////////////////////////////
  // Update the controller
  void DvsPlugin::OnNewFrame(const unsigned char *_image,
      unsigned int _width, unsigned int _height, unsigned int _depth,
      const std::string &_format)
  {
#if GAZEBO_MAJOR_VERSION >= 7
    _image = this->camera->ImageData(0);
#else
    _image = this->camera->GetImageData(0);
#endif


    // convert given frame to opencv image
    // gazebo provides an RGB image
    cv::Mat input_image(_height, _width, CV_8UC3);
    input_image.data = (uchar*)_image;

    // color to grayscale
    cv::Mat curr_image(_height, _width, CV_8UC1);
    cvtColor(input_image, curr_image, CV_RGB2GRAY);
    curr_image.convertTo(this->curr_image, CV_32F);

/* TODO any encoding configuration should be supported
    try {
      cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(*_image, sensor_msgs::image_encodings::BGR8);
      std::cout << "Image: " << std::endl << " " << cv_ptr->image << std::endl << std::endl;
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("cv_bridge exception %s", e.what());
      std::cout << "ERROR";
    }
*/

    assert(_height == height && _width == width);
    if (!(this->first_frame)){
      this->processDelta();
    }
    else if (curr_image.size().area() > 0)
    {
      this->reference = this->curr_image;
      this->first_frame = false;
    }
    else
    {
      gzwarn << "Ignoring empty image." << endl;
    }
  }

  void DvsPlugin::processDelta(cv::Mat *curr_image)
  {
    if (this->curr_image.size() == this->reference.size())
    {
      cv::parallel_for_(cv::Range(0, _gray.rows), this->nvsOp);

      std::vector<dvs_msgs::Event> events;

      this->fillEvents(&events);
      this->fillEvents(&events);

      this->publishEvents(&events);
    }
    else
    {
      gzwarn << "Unexpected change in image size (" << last_image->size() << " -> " << curr_image->size() << "). Publishing no events for this frame change." << endl;
    }
  }

  void DvsPlugin::fillEvents(std::vector<dvs_msgs::Event> *events)
  {
    // findNonZero fails when there are no zeros
    // TODO is there a better workaround then iterating the binary image twice?
    std::vector<cv::Point> locs;

    for (size_t row = 0; row < this->height; row++){
      for (size_t col = 0; col < this->width; col++){
        if (this->events[row][col] != 0.0f){
          dvs_msgs::Event event;
          event.x = col;
          event.y = row;
          event.ts = ros::Time::now();
          event.polarity = this->events[row][col] > 0 ? 1 : 0;
          events->push_back(event);
        }
      }
    }
    
  }

  void DvsPlugin::publishEvents(std::vector<dvs_msgs::Event> *events)
  {
    if (events->size() > 0)
    {
      dvs_msgs::EventArray msg;
      msg.events.clear();
      msg.events.insert(msg.events.end(), events->begin(), events->end());
      msg.width = width;
      msg.height = height;

      // TODO what frame_id is adequate?
      msg.header.frame_id = namespace_;
      msg.header.stamp = ros::Time::now();

      event_pub_.publish(msg);
    }
  }
}
