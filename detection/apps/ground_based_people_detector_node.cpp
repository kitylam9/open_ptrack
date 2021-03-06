/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2013-, Matteo Munaro [matteo.munaro@dei.unipd.it]
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * * Neither the name of the copyright holder(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * ground_based_people_detector_node.cpp
 * Created on: Jul 07, 2013
 * Author: Matteo Munaro
 *
 * ROS node which performs people detection assuming that people stand/walk on a ground plane.
 * As a first step, the ground is manually initialized, then people detection is performed with the GroundBasedPeopleDetectionApp class,
 * which implements the people detection algorithm described here:
 * M. Munaro, F. Basso and E. Menegatti,
 * Tracking people within groups with RGB-D data,
 * In Proceedings of the International Conference on Intelligent Robots and Systems (IROS) 2012, Vilamoura (Portugal), 2012.
 */

// ROS includes:
#include <ros/ros.h>

// PCL includes:
#include <pcl/ros/conversions.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/sample_consensus/sac_model_plane.h>
#include <pcl_conversions/pcl_conversions.h>

// Open PTrack includes:
#include <open_ptrack/detection/ground_based_people_detection_app.h>
#include <open_ptrack/opt_utils/conversions.h>

//Publish Messages
#include <opt_msgs/RoiRect.h>
#include <opt_msgs/Rois.h>
#include <std_msgs/String.h>
#include <opt_msgs/Detection.h>
#include <opt_msgs/DetectionArray.h>

using namespace opt_msgs;
using namespace sensor_msgs;

typedef pcl::PointXYZRGB PointT;
typedef pcl::PointCloud<PointT> PointCloudT;

bool new_cloud_available_flag = false;
PointCloudT::Ptr cloud(new PointCloudT);

// PCL viewer //
pcl::visualization::PCLVisualizer viewer("PCL Viewer");

enum { COLS = 640, ROWS = 480 };

void cloud_cb(const PointCloudT::ConstPtr& callback_cloud)
{
	*cloud = *callback_cloud;
	new_cloud_available_flag = true;
}

struct callback_args{
	// structure used to pass arguments to the callback function
	PointCloudT::Ptr clicked_points_3d;
	pcl::visualization::PCLVisualizer* viewerPtr;
};

void
pp_callback (const pcl::visualization::PointPickingEvent& event, void* args)
{
	struct callback_args* data = (struct callback_args *)args;
	if (event.getPointIndex () == -1)
		return;
	PointT current_point;
	event.getPoint(current_point.x, current_point.y, current_point.z);
	data->clicked_points_3d->points.push_back(current_point);
	// Draw clicked points in red:
	pcl::visualization::PointCloudColorHandlerCustom<PointT> red (data->clicked_points_3d, 255, 0, 0);
	data->viewerPtr->removePointCloud("clicked_points");
	data->viewerPtr->addPointCloud(data->clicked_points_3d, red, "clicked_points");
	data->viewerPtr->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 10, "clicked_points");
	std::cout << current_point.x << " " << current_point.y << " " << current_point.z << std::endl;
}

int
main (int argc, char** argv)
{
	ros::init(argc, argv, "ground_based_people_detector");
	ros::NodeHandle nh("~");

	// Read some parameters from launch file:
	std::string svm_filename;
	nh.param("classifier_file", svm_filename, std::string("./"));
	bool use_rgb;
	nh.param("use_rgb", use_rgb, false);
	double min_confidence;
	nh.param("HogSvmThreshold", min_confidence, -1.5);
	double min_height;
	nh.param("minimum_person_height", min_height, 1.3);
	double max_height;
	nh.param("maximum_person_height", max_height, 2.3);
	int sampling_factor;
	nh.param("sampling_factor", sampling_factor, 1);
	std::string pointcloud_topic;
	nh.param("pointcloud_topic", pointcloud_topic, std::string("/camera/depth_registered/points"));
	double rate_value;
	nh.param("rate", rate_value, 30.0);

	// Fixed parameters:
	float voxel_size = 0.06;
	Eigen::Matrix3f rgb_intrinsics_matrix;
	rgb_intrinsics_matrix << 525, 0.0, 319.5, 0.0, 525, 239.5, 0.0, 0.0, 1.0; // Kinect RGB camera intrinsics

	// Subscribers:
	ros::Subscriber sub = nh.subscribe(pointcloud_topic, 1, cloud_cb);

	// Publishers:
	ros::Publisher detection_pub;
	detection_pub= nh.advertise<DetectionArray>("/ground_based_people_detector/detections",3);
	ros::Publisher pub_rois_;
	pub_rois_= nh.advertise<Rois>("GroundBasedPeopleDetectorOutputRois",3);

	Rois output_rois_;
	open_ptrack::opt_utils::Conversions converter;

	ros::Rate rate(rate_value);
	while(ros::ok() && !new_cloud_available_flag)
	{
		ros::spinOnce();
		rate.sleep();
	}

	// Display pointcloud:
	pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud);
	viewer.addPointCloud<PointT> (cloud, rgb, "input_cloud");
	viewer.setCameraPosition(0,0,-2,0,-1,0,0);

	// Add point picking callback to viewer:
	struct callback_args cb_args;
	PointCloudT::Ptr clicked_points_3d (new PointCloudT);
	cb_args.clicked_points_3d = clicked_points_3d;
	cb_args.viewerPtr = &viewer;
	viewer.registerPointPickingCallback (pp_callback, (void*)&cb_args);
	std::cout << "Shift+click on three floor points, then press 'Q'..." << std::endl;

	// Spin until 'Q' is pressed:
	viewer.spin();
	std::cout << "done." << std::endl;

	// Ground plane estimation:
	Eigen::VectorXf ground_coeffs;
	ground_coeffs.resize(4);
	std::vector<int> clicked_points_indices;
	for (unsigned int i = 0; i < clicked_points_3d->points.size(); i++)
		clicked_points_indices.push_back(i);
	pcl::SampleConsensusModelPlane<PointT> model_plane(clicked_points_3d);
	model_plane.computeModelCoefficients(clicked_points_indices,ground_coeffs);
	ROS_ERROR("Ground plane coefficients: %f, %f, %f, %f.", ground_coeffs(0), ground_coeffs(1), ground_coeffs(2), ground_coeffs(3));

	// Create classifier for people detection:
	pcl::people::PersonClassifier<pcl::RGB> person_classifier;
	person_classifier.loadSVMFromFile(svm_filename);   // load trained SVM

	// People detection app initialization:
	open_ptrack::detection::GroundBasedPeopleDetectionApp<PointT> people_detector;    // people detection object
	people_detector.setVoxelSize(voxel_size);                        // set the voxel size
	people_detector.setIntrinsics(rgb_intrinsics_matrix);            // set RGB camera intrinsic parameters
	people_detector.setClassifier(person_classifier);                // set person classifier
	people_detector.setHeightLimits(min_height, max_height);         // set person classifier
	people_detector.setSamplingFactor(sampling_factor);              // set sampling factor

	// Main loop:
	while(ros::ok())
	{
		if (new_cloud_available_flag)
		{
			new_cloud_available_flag = false;

			// Convert PCL cloud header to ROS header:
			std_msgs::Header cloud_header = pcl_conversions::fromPCL(cloud->header);

			// Perform people detection on the new cloud:
			std::vector<pcl::people::PersonCluster<PointT> > clusters;   // vector containing persons clusters
			people_detector.setInputCloud(cloud);
			people_detector.setGround(ground_coeffs);                    // set floor coefficients
			people_detector.compute(clusters);                           // perform people detection
			people_detector.setUseRGB(false);                            // set if RGB should be used or not

			ground_coeffs = people_detector.getGround();                 // get updated floor coefficients

			/// Write detection message:
			DetectionArray::Ptr detection_array_msg(new DetectionArray);
			// Set camera-specific fields:
			detection_array_msg->header = cloud_header;
			for(int i = 0; i < 3; i++)
				for(int j = 0; j < 3; j++)
					detection_array_msg->intrinsic_matrix.push_back(rgb_intrinsics_matrix(i, j));
			// Add all valid detections:
			for(std::vector<pcl::people::PersonCluster<PointT> >::iterator it = clusters.begin(); it != clusters.end(); ++it)
			{
				if((!use_rgb) | (it->getPersonConfidence() > min_confidence))            // if RGB is used, keep only people with confidence above a threshold
				{
				  // Create detection message:
					Detection detection_msg;
					converter.Vector3fToVector3(it->getMin(), detection_msg.box_3D.p1);
					converter.Vector3fToVector3(it->getMax(), detection_msg.box_3D.p2);

					float head_centroid_compensation = 0.05;

					// theoretical person centroid:
					Eigen::Vector3f centroid3d = it->getTCenter();
					Eigen::Vector3f centroid2d = converter.world2cam(centroid3d, rgb_intrinsics_matrix);
					// theoretical person top point:
					Eigen::Vector3f top3d = it->getTTop();
					Eigen::Vector3f top2d = converter.world2cam(top3d, rgb_intrinsics_matrix);
					// theoretical person bottom point:
					Eigen::Vector3f bottom3d = it->getTBottom();
					Eigen::Vector3f bottom2d = converter.world2cam(bottom3d, rgb_intrinsics_matrix);
          float enlarge_factor = 1.1;
          float pixel_xc = centroid2d(0);
					float pixel_yc = centroid2d(1);
					float pixel_height = (bottom2d(1) - top2d(1)) * enlarge_factor;
					float pixel_width = pixel_height / 2;
					detection_msg.box_2D.x = int(centroid2d(0) - pixel_width/2.0);
					detection_msg.box_2D.y = int(centroid2d(1) - pixel_height/2.0);
					detection_msg.box_2D.width = int(pixel_width);
					detection_msg.box_2D.height = int(pixel_height);
					detection_msg.height = it->getHeight();
					detection_msg.confidence = it->getPersonConfidence();
					detection_msg.distance = it->getDistance();
					converter.Vector3fToVector3((1+head_centroid_compensation/centroid3d.norm())*centroid3d, detection_msg.centroid);
					converter.Vector3fToVector3((1+head_centroid_compensation/top3d.norm())*top3d, detection_msg.top);
					converter.Vector3fToVector3((1+head_centroid_compensation/bottom3d.norm())*bottom3d, detection_msg.bottom);

					// Add message:
					detection_array_msg->detections.push_back(detection_msg);
				}
			}
			detection_pub.publish(detection_array_msg);		 // publish message
		}

		// Execute callbacks:
		ros::spinOnce();
		rate.sleep();
	}
	return 0;
}

