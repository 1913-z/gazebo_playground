//
// Created by lfc on 2021/2/28.
//

#include "livox_laser_simulation/livox_points_plugin_cpu.h"
#include <ros/ros.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud2_iterator.h>
#include <gazebo/physics/Model.hh>
#include <gazebo/physics/MultiRayShape.hh>
#include <gazebo/physics/PhysicsEngine.hh>
#include <gazebo/physics/World.hh>
#include <gazebo/sensors/RaySensor.hh>
#include <gazebo/transport/Node.hh>
#include "livox_laser_simulation/csv_reader.hpp"
#include "livox_laser_simulation/livox_ode_multiray_shape.h"

namespace gazebo {

GZ_REGISTER_SENSOR_PLUGIN(LivoxPointsPlugin)

LivoxPointsPlugin::LivoxPointsPlugin() : RayPlugin() {} 
LivoxPointsPlugin::~LivoxPointsPlugin() {}

void convertDataToRotateInfo(const std::vector<std::vector<double>> &datas, std::vector<AviaRotateInfo> &avia_infos) {
    avia_infos.reserve(datas.size());
    double deg_2_rad = M_PI / 180.0;
    for (auto &data : datas) {
        if (data.size() == 3) {
            avia_infos.emplace_back();
            avia_infos.back().time = data[0];
            avia_infos.back().azimuth = data[1] * deg_2_rad;
            avia_infos.back().zenith = data[2] * deg_2_rad - M_PI_2;  //转化成标准的右手系角度
        } else {
            ROS_INFO_STREAM("data size is not 3!");
        }
    }
}

void LivoxPointsPlugin::Load(gazebo::sensors::SensorPtr _parent,
                             sdf::ElementPtr pluginSdf)
{

  sdfPtr = pluginSdf;                      
  sdf::ElementPtr sensorSdf = pluginSdf->GetParent();  


  std::vector<std::vector<double>> datas;
  std::string file_name = pluginSdf->Get<std::string>("csv_file_name");
  ROS_INFO_STREAM("load csv file name: " << file_name);
  if (!CsvReader::ReadCsvFile(file_name, datas)) {
    ROS_ERROR_STREAM("cannot read csv file: " << file_name);
    return;
  }
  convertDataToRotateInfo(datas, aviaInfos);
  maxPointSize = aviaInfos.size();
  ROS_INFO_STREAM("scan info size: " << maxPointSize);

  std::string scanTopic = pluginSdf->Get<std::string>("ros_topic");
  samplesStep = pluginSdf->Get<int>("samples");
  downSample  = pluginSdf->Get<int>("downsample");
  if (downSample < 1) downSample = 1;
  ROS_INFO_STREAM("ros topic: " << scanTopic
                  << ", samples=" << samplesStep
                  << ", downsample=" << downSample);


  auto rayElem   = sensorSdf->GetElement("ray");
  auto scanElem  = rayElem->GetElement("scan");
  auto rangeElem = rayElem->GetElement("range");
  minDist = rangeElem->Get<double>("min");
  maxDist = rangeElem->Get<double>("max");

  if (!ros::isInitialized()) {
    int argc = 0;
    char **argv = nullptr;
    ros::init(argc, argv, "livox_laser_plugin");
  }
  std::string ns = "";
  if (pluginSdf->HasElement("ros")) {
  auto rosElem = pluginSdf->GetElement("ros");
  if (rosElem->HasElement("namespace"))
    ns = rosElem->Get<std::string>("namespace");
  }
  rosNode    .reset(new ros::NodeHandle(ns));
  rosPointPub = rosNode->advertise<sensor_msgs::PointCloud2>(scanTopic, 5);

  raySensor = _parent;
  node      = transport::NodePtr(new transport::Node());
  node->Init(raySensor->WorldName());
  scanPub = node->Advertise<msgs::LaserScanStamped>(_parent->Topic(), 50);

  RayPlugin::Load(_parent, sensorSdf);
  laserMsg.mutable_scan()->set_frame(_parent->ParentName());

  world = physics::get_world(raySensor->WorldName());
  this->parentEntity = world->EntityByName(raySensor->ParentName());
  auto linkPtr = boost::dynamic_pointer_cast<physics::Link>(parentEntity);
  if (!linkPtr) {
    ROS_ERROR_STREAM("LivoxPointsPlugin: 找不到父 link: " << raySensor->ParentName());
    return;
  }
  this->parentLink = linkPtr;
  auto phys = world->Physics();
  laserCollision = phys->CreateCollision("multiray", parentLink);
  laserCollision->SetName("ray_sensor_collision");
  laserCollision->SetRelativePose(_parent->Pose());
  laserCollision->SetInitialRelativePose(_parent->Pose());

  rayShape.reset(new physics::LivoxOdeMultiRayShape(laserCollision));
  laserCollision->SetShape(rayShape);

  rayShape->Load(sensorSdf);
  rayShape->Init();

  currStartIndex = 0;
  rayShape->RayShapes().reserve(samplesStep/downSample);
  auto offset = laserCollision->RelativePose();
  for (int j = 0; j < samplesStep; j += downSample) {
    int idx = j % maxPointSize;
    auto &info = aviaInfos[idx];
    ignition::math::Quaterniond q;
    q.Euler(ignition::math::Vector3d(0, info.zenith, info.azimuth));
    auto axis = offset.Rot() * q * ignition::math::Vector3d(1,0,0);
    auto start = minDist*axis + offset.Pos();
    auto end   = maxDist*axis + offset.Pos();
    rayShape->AddRay(start, end);
  }
}


void LivoxPointsPlugin::OnNewLaserScans() {
    if (rayShape) {
        std::vector<std::pair<int, AviaRotateInfo>> points_pair;
        InitializeRays(points_pair, rayShape);
        rayShape->
        Update();

        msgs::Set(laserMsg.mutable_time(), world->SimTime());
        msgs::LaserScan *scan = laserMsg.mutable_scan();
        InitializeScan(scan);
        SendRosTf(parentEntity->WorldPose(), world->Name(), raySensor->ParentName());
        auto rayCount = RayCount();
        auto verticalRayCount = VerticalRayCount();
        auto angle_min = AngleMin().Radian();
        auto angle_incre = AngleResolution();
        auto verticle_min = VerticalAngleMin().Radian();
        auto verticle_incre = VerticalAngleResolution();

        sensor_msgs::PointCloud2 scan_point;
        scan_point.header.stamp = ros::Time::now();
        scan_point.header.frame_id = raySensor->Name();
        scan_point.height = 1;                          
        scan_point.width = points_pair.size();  
        sensor_msgs::PointCloud2Modifier modifier(scan_point);
        modifier.setPointCloud2Fields(4,
        "x", 1, sensor_msgs::PointField::FLOAT32,
        "y", 1, sensor_msgs::PointField::FLOAT32,
        "z", 1, sensor_msgs::PointField::FLOAT32,
        "intensity", 1, sensor_msgs::PointField::FLOAT32);
        modifier.resize(points_pair.size());

        sensor_msgs::PointCloud2Iterator<float> iter_x(scan_point, "x");
        sensor_msgs::PointCloud2Iterator<float> iter_y(scan_point, "y");
        sensor_msgs::PointCloud2Iterator<float> iter_z(scan_point, "z");
        sensor_msgs::PointCloud2Iterator<float> iter_intensity(scan_point, "intensity");
        for (auto &pair : points_pair) {
                auto range = rayShape->GetRange(pair.first);
                auto intensity = rayShape->GetRetro(pair.first);
                if (range >= RangeMax() || range <= RangeMin()) {
                    range = 0;
                    intensity = 0;
                }
                auto rotate_info = pair.second;
                ignition::math::Quaterniond ray;
                ray.Euler(ignition::math::Vector3d(0.0, rotate_info.zenith, rotate_info.azimuth));
                auto axis = ray * ignition::math::Vector3d(1.0, 0.0, 0.0);
                auto point = range * axis;
                *iter_x = point.X();
                *iter_y = point.Y();
                *iter_z = point.Z();
                *iter_intensity = intensity;
                ++iter_x; ++iter_y; ++iter_z; ++iter_intensity;
        }
        if (scanPub && scanPub->HasConnections()) scanPub->Publish(laserMsg);
        rosPointPub.publish(scan_point);
        ros::spinOnce();
    }
}

void LivoxPointsPlugin::InitializeRays(std::vector<std::pair<int, AviaRotateInfo>> &points_pair,
                                       boost::shared_ptr<physics::LivoxOdeMultiRayShape> &ray_shape) {
    auto &rays = ray_shape->RayShapes();
    ignition::math::Vector3d start_point, end_point;
    ignition::math::Quaterniond ray;
    auto offset = laserCollision->RelativePose();
    int64_t end_index = currStartIndex + samplesStep;
    int ray_index = 0;
    auto ray_size = rays.size();
    points_pair.reserve(rays.size());
    for (int k = currStartIndex; k < end_index; k += downSample) {
        auto index = k % maxPointSize;
        auto &rotate_info = aviaInfos[index];
        ray.Euler(ignition::math::Vector3d(0.0, rotate_info.zenith, rotate_info.azimuth));
        auto axis = offset.Rot() * ray * ignition::math::Vector3d(1.0, 0.0, 0.0);
        start_point = minDist * axis + offset.Pos();
        end_point = maxDist * axis + offset.Pos();
        if (ray_index < ray_size) {
            rays[ray_index]->SetPoints(start_point, end_point);
            points_pair.emplace_back(ray_index, rotate_info);
        }
        ray_index++;
    }
    currStartIndex += samplesStep;
}

void LivoxPointsPlugin::InitializeScan(msgs::LaserScan *&scan) {
    // Store the latest laser scans into laserMsg
    msgs::Set(scan->mutable_world_pose(), raySensor->Pose() + parentEntity->WorldPose());
    scan->set_angle_min(AngleMin().Radian());
    scan->set_angle_max(AngleMax().Radian());
    scan->set_angle_step(AngleResolution());
    scan->set_count(RangeCount());

    scan->set_vertical_angle_min(VerticalAngleMin().Radian());
    scan->set_vertical_angle_max(VerticalAngleMax().Radian());
    scan->set_vertical_angle_step(VerticalAngleResolution());
    scan->set_vertical_count(VerticalRangeCount());

    scan->set_range_min(RangeMin());
    scan->set_range_max(RangeMax());

    scan->clear_ranges();
    scan->clear_intensities();

    unsigned int rangeCount = RangeCount();
    unsigned int verticalRangeCount = VerticalRangeCount();

    for (unsigned int j = 0; j < verticalRangeCount; ++j) {
        for (unsigned int i = 0; i < rangeCount; ++i) {
            scan->add_ranges(0);
            scan->add_intensities(0);
        }
    }
}

ignition::math::Angle LivoxPointsPlugin::AngleMin() const {
    if (rayShape)
        return rayShape->MinAngle();
    else
        return -1;
}

ignition::math::Angle LivoxPointsPlugin::AngleMax() const {
    if (rayShape) {
        return ignition::math::Angle(rayShape->MaxAngle().Radian());
    } else
        return -1;
}

double LivoxPointsPlugin::GetRangeMin() const { return RangeMin(); }

double LivoxPointsPlugin::RangeMin() const {
    if (rayShape)
        return rayShape->GetMinRange();
    else
        return -1;
}

double LivoxPointsPlugin::GetRangeMax() const { return RangeMax(); }

double LivoxPointsPlugin::RangeMax() const {
    if (rayShape)
        return rayShape->GetMaxRange();
    else
        return -1;
}

double LivoxPointsPlugin::GetAngleResolution() const { return AngleResolution(); }

double LivoxPointsPlugin::AngleResolution() const { return (AngleMax() - AngleMin()).Radian() / (RangeCount() - 1); }

double LivoxPointsPlugin::GetRangeResolution() const { return RangeResolution(); }

double LivoxPointsPlugin::RangeResolution() const {
    if (rayShape)
        return rayShape->GetResRange();
    else
        return -1;
}

int LivoxPointsPlugin::GetRayCount() const { return RayCount(); }

int LivoxPointsPlugin::RayCount() const {
    if (rayShape)
        return rayShape->GetSampleCount();
    else
        return -1;
}

int LivoxPointsPlugin::GetRangeCount() const { return RangeCount(); }

int LivoxPointsPlugin::RangeCount() const {
    if (rayShape)
        return rayShape->GetSampleCount() * rayShape->GetScanResolution();
    else
        return -1;
}

int LivoxPointsPlugin::GetVerticalRayCount() const { return VerticalRayCount(); }

int LivoxPointsPlugin::VerticalRayCount() const {
    if (rayShape)
        return rayShape->GetVerticalSampleCount();
    else
        return -1;
}

int LivoxPointsPlugin::GetVerticalRangeCount() const { return VerticalRangeCount(); }

int LivoxPointsPlugin::VerticalRangeCount() const {
    if (rayShape)
        return rayShape->GetVerticalSampleCount() * rayShape->GetVerticalScanResolution();
    else
        return -1;
}

ignition::math::Angle LivoxPointsPlugin::VerticalAngleMin() const {
    if (rayShape) {
        return ignition::math::Angle(rayShape->VerticalMinAngle().Radian());
    } else
        return -1;
}

ignition::math::Angle LivoxPointsPlugin::VerticalAngleMax() const {
    if (rayShape) {
        return ignition::math::Angle(rayShape->VerticalMaxAngle().Radian());
    } else
        return -1;
}

double LivoxPointsPlugin::GetVerticalAngleResolution() const { return VerticalAngleResolution(); }

double LivoxPointsPlugin::VerticalAngleResolution() const {
    return (VerticalAngleMax() - VerticalAngleMin()).Radian() / (VerticalRangeCount() - 1);
}
void LivoxPointsPlugin::SendRosTf(const ignition::math::Pose3d &pose, const std::string &father_frame,
                                  const std::string &child_frame) {
    if (!tfBroadcaster) {
        tfBroadcaster.reset(new tf::TransformBroadcaster);
    }
    tf::Transform tf;
    auto rot = pose.Rot();
    auto pos = pose.Pos();
    tf.setRotation(tf::Quaternion(rot.X(), rot.Y(), rot.Z(), rot.W()));
    tf.setOrigin(tf::Vector3(pos.X(), pos.Y(), pos.Z()));
    tfBroadcaster->sendTransform(
        tf::StampedTransform(tf, ros::Time::now(), raySensor->ParentName(), raySensor->Name()));
}

}
