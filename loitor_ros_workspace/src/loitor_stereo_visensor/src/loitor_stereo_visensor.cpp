#include <ros/ros.h>
#include <ros/package.h>
#include <std_msgs/String.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Imu.h>
#include <camera_info_manager/camera_info_manager.h>

#include <cv.h>
#include <highgui.h>
#include "cxcore.hpp"
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loitorusbcam.h"
#include "loitorimu.h"

#include <sstream>


using namespace std;
using namespace cv;


ros::Publisher pub_imu;
string setup;
string calib_cam0;
string calib_cam1;

/*
*  用于构造cv::Mat 的左右眼图像
*/
cv::Mat img_left;
cv::Mat img_right;

/*
*  当前左右图像的时间戳
*/
timeval left_stamp,right_stamp;

/*
*  imu viewer
*/
bool visensor_Close_IMU_viewer=false;
bool imu_start_transfer=false;
void* imu_data_stream(void *)
{
	int counter=0;
	imu_start_transfer=false;


	while((!visensor_Close_IMU_viewer)&&!imu_start_transfer)usleep(1000);
	while(!visensor_Close_IMU_viewer)
	{
		if(visensor_imu_have_fresh_data())
           	{
			counter++;
			// 每隔20帧显示一次imu数据
			if(counter>=20) 
			{
				//cout<<"visensor_imudata_pack->a : "<<visensor_imudata_pack.ax<<" , "<<visensor_imudata_pack.ay<<" , "<<visensor_imudata_pack.az<<endl;
				float ax=visensor_imudata_pack.ax;
				float ay=visensor_imudata_pack.ay;
				float az=visensor_imudata_pack.az;
				//cout<<"visensor_imudata_pack->a : "<<sqrt(ax*ax+ay*ay+az*az)<<endl;
				//cout<<"imu_time : "<<visensor_imudata_pack.imu_time<<endl;
				//cout<<"imu_time : "<<visensor_imudata_pack.system_time.tv_usec<<endl;
				counter=0;
			}
			sensor_msgs::Imu imu_msg;
			imu_msg.header.frame_id = "/imu";
			ros::Time imu_time;
			imu_time.sec=visensor_imudata_pack.system_time.tv_sec;
			imu_time.nsec=1000*visensor_imudata_pack.system_time.tv_usec;
			imu_msg.header.stamp = imu_time;
			imu_msg.header.seq=0;

			imu_msg.linear_acceleration.x=visensor_imudata_pack.ax;
			imu_msg.linear_acceleration.y=visensor_imudata_pack.ay;
			imu_msg.linear_acceleration.z=visensor_imudata_pack.az;
			imu_msg.angular_velocity.x=3.1415926f*visensor_imudata_pack.rx/180.0f;
			imu_msg.angular_velocity.y=3.1415926f*visensor_imudata_pack.ry/180.0f;
			imu_msg.angular_velocity.z=3.1415926f*visensor_imudata_pack.rz/180.0f;
			imu_msg.orientation.w=visensor_imudata_pack.qw;
			imu_msg.orientation.x=visensor_imudata_pack.qx;
			imu_msg.orientation.y=visensor_imudata_pack.qy;
			imu_msg.orientation.z=visensor_imudata_pack.qz;

			pub_imu.publish(imu_msg);
		}
		usleep(10);
	}
	pthread_exit(NULL);
}


int main(int argc, char **argv)
{ 
	// init ROS
	ros::init(argc, argv, "loitor_stereo_visensor");
	ros::NodeHandle n("~");

	string pkg_path = ros::package::getPath("loitor_stereo_visensor");
	n.param<string>("calib0", calib_cam0, "file://" + pkg_path + "/calib/right.yaml");
	n.param<string>("calib1", calib_cam1, "file://" + pkg_path + "/calib/left.yaml");
	n.param<string>("setup", setup, pkg_path + "/cfg/Loitor_VISensor_Setups.txt");

	// imu publisher
	pub_imu = n.advertise<sensor_msgs::Imu>("imu0", 200);
 
	// publish 到这两个 topic
	image_transport::ImageTransport it0(n);
	image_transport::Publisher pub0 = it0.advertise("/cam0/image_raw", 1);
	ros::Publisher cam0_info = n.advertise<sensor_msgs::CameraInfo>("/cam0/camera_info", 10);
    sensor_msgs::CameraInfo cam0_info_msg;
    sensor_msgs::ImagePtr msg;

	image_transport::ImageTransport it1(n);
	image_transport::Publisher pub1 = it1.advertise("/cam1/image_raw", 1);
    ros::Publisher cam1_info = n.advertise<sensor_msgs::CameraInfo>("/cam1/camera_info", 10);
    sensor_msgs::CameraInfo cam1_info_msg;
	sensor_msgs::ImagePtr msg1;

	// load camera calib
    camera_info_manager::CameraInfoManager cam_manager(n);
    cam_manager.setCameraName("right");
    if(cam_manager.loadCameraInfo(calib_cam0)){
        cam0_info_msg = cam_manager.getCameraInfo();
    }
    cam_manager.setCameraName("left");
    if(cam_manager.loadCameraInfo(calib_cam1)){
        cam1_info_msg = cam_manager.getCameraInfo();
    }

    /************************ Start Cameras ************************/
	visensor_load_settings(setup.c_str());

	// 手动设置相机参数
	//set_current_mode(5);
	//set_auto_EG(0);
	//set_exposure(50);
	//set_gain(200);
	//set_visensor_cam_selection_mode(2);
	//set_resolution(false);
	//set_fps_mode(true);
	// 保存相机参数到原配置文件
	//save_current_settings();

	int r = visensor_Start_Cameras();
	if(r<0)
	{
		ROS_ERROR("Opening cameras failed...");
		return r;
	}
	// 创建用来接收camera数据的图像
	if(!visensor_resolution_status)
	{
		img_left.create(cv::Size(640,480),CV_8U);
		img_right.create(cv::Size(640,480),CV_8U);
		img_left.data=new unsigned char[IMG_WIDTH_VGA*IMG_HEIGHT_VGA];
		img_right.data=new unsigned char[IMG_WIDTH_VGA*IMG_HEIGHT_VGA];
	}
	else
	{
		img_left.create(cv::Size(752,480),CV_8U);
		img_right.create(cv::Size(752,480),CV_8U);
		img_left.data=new unsigned char[IMG_WIDTH_WVGA*IMG_HEIGHT_WVGA];
		img_right.data=new unsigned char[IMG_WIDTH_WVGA*IMG_HEIGHT_WVGA];
	}
	float hardware_fps=visensor_get_hardware_fps();
	/************************** Start IMU **************************/
	int fd=visensor_Start_IMU();
	if(fd<0)
	{
		ROS_ERROR("open_port error...");
		return 0;
	}
	ROS_DEBUG("open_port success...");
	usleep(100000);
	/************************ ************ ************************/

	//Create imu_data_stream thread
	pthread_t imu_data_thread;
	int temp = pthread_create(&imu_data_thread, NULL, imu_data_stream, NULL);
	if(temp){
		ROS_ERROR("Failed to create thread imu_data_stream");
	}

	// 使用camera硬件帧率设置发布频率
	ros::Rate loop_rate((int)hardware_fps);

	int static_ct=0;

	timeval img_time_test,img_time_offset;
	img_time_test.tv_usec=0;
	img_time_test.tv_sec=0;
	img_time_offset.tv_usec=50021;
	img_time_offset.tv_sec=0;

	while (ros::ok())
	{
		imu_start_transfer=true;

		//cout<<"visensor_get_hardware_fps() ==== "<<visensor_get_hardware_fps()<<endl;


		if(visensor_cam_selection==0)
		{

			visensor_imudata paired_imu=visensor_get_stereoImg((char *)img_left.data,(char *)img_right.data,left_stamp,right_stamp);


			// 显示同步数据的时间戳（单位微秒）
			//cout<<"left_time : "<<left_stamp.tv_usec<<endl;
			//cout<<"right_time : "<<right_stamp.tv_usec<<endl;
			//cout<<"paired_imu time ===== "<<paired_imu.system_time.tv_usec<<endl<<endl;
			//cout<<"visensor_get_hardware_fps() ==== "<<1.0f/visensor_get_hardware_fps()<<endl;

			cv_bridge::CvImage t_left=cv_bridge::CvImage(std_msgs::Header(), "mono8", img_left);
			cv_bridge::CvImage t_right=cv_bridge::CvImage(std_msgs::Header(), "mono8", img_right);

			// 加时间戳(right_time=left_time)
			ros::Time msg_time;
			msg_time.sec=left_stamp.tv_sec;
			msg_time.nsec=1000*left_stamp.tv_usec;
			t_left.header.stamp = msg_time;
			
			ros::Time msg1_time;
			msg1_time.sec=left_stamp.tv_sec;
			msg1_time.nsec=1000*left_stamp.tv_usec;
			t_right.header.stamp = msg1_time;
			t_right.header.seq=0;
			t_left.header.seq=0;

			msg = t_left.toImageMsg();
			msg1 = t_right.toImageMsg();

			static_ct++;
			{
				pub0.publish(msg);
				pub1.publish(msg1);
				static_ct=0;
			}
			
			// 显示时间戳
			//cout<<"left_time : "<<left_stamp.tv_usec<<endl;
			//cout<<"right_time : "<<right_stamp.tv_usec<<endl<<endl;

		}
		else if(visensor_cam_selection==1)
		{
			visensor_imudata paired_imu=visensor_get_rightImg((char *)img_right.data,right_stamp);

			// 显示同步数据的时间戳（单位微秒）
			cout<<"right_time : "<<right_stamp.tv_usec<<endl;
			cout<<"paired_imu time ===== "<<paired_imu.system_time.tv_usec<<endl<<endl;

			cv_bridge::CvImage t_right=cv_bridge::CvImage(std_msgs::Header(), "mono8", img_right);

			// 加时间戳
			ros::Time msg1_time;
			msg1_time.sec=right_stamp.tv_sec;
			msg1_time.nsec=1000*right_stamp.tv_usec;
			t_right.header.stamp = msg1_time;
			t_right.header.seq=0;
			
			msg1 = t_right.toImageMsg();

			pub1.publish(msg1);
		}
		else if(visensor_cam_selection==2)
		{
			visensor_imudata paired_imu=visensor_get_leftImg((char *)img_left.data,left_stamp);

			// 显示同步数据的时间戳（单位微秒）
			cout<<"left_time : "<<left_stamp.tv_usec<<endl;
			cout<<"paired_imu time ===== "<<paired_imu.system_time.tv_usec<<endl<<endl;

			cv_bridge::CvImage t_left=cv_bridge::CvImage(std_msgs::Header(), "mono8", img_left);

			// 加时间戳
			ros::Time msg_time;
			msg_time.sec=left_stamp.tv_sec;
			msg_time.nsec=1000*left_stamp.tv_usec;
			t_left.header.stamp = msg_time;
			t_left.header.seq=0;


			msg = t_left.toImageMsg();

			
			static_ct++;
			if(static_ct>=5)
			{
				pub0.publish(msg);
				static_ct=0;
			}
		}

        cam0_info_msg.header.stamp = msg->header.stamp;
        cam1_info_msg.header.stamp = msg1->header.stamp;
        cam0_info.publish(cam0_info_msg);
        cam1_info.publish(cam1_info_msg);

		//ros::spinOnce();

		loop_rate.sleep(); 
		
	}

	/* shut-down viewers */
	visensor_Close_IMU_viewer=true;
	if(imu_data_thread !=0)
	{
		pthread_join(imu_data_thread, NULL);
	}

	cout<<endl<<"shutting-down Cameras"<<endl;

	/* close cameras */
	visensor_Close_Cameras();
	/* close IMU */
	visensor_Close_IMU();
	
	return 0;
}











