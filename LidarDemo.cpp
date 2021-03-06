#include "stdafx.h"

#define _USE_MATH_DEFINES
#include "opencv2/opencv.hpp"
#include <thread>
#include <math.h>
#include <iostream>
#include <string>


using namespace cv;
using namespace std;

Mat frame;

int fps = 30;

char* portName = (char*) "\\\\.\\COM5";

//Declare a global object
SerialPort *arduino;

const char MotorOn[] = "MotorOn\nSetRPM 349\n";
const char MotorOff[] = "MotorOff\n";

uint8_t init_level = 0, angle = 0, index = 0;

uint16_t dists[72];

uint16_t checksum(uint8_t* data) {
	uint16_t data_list[10];

	for (int i = 0; i < 10; ++i)
		data_list[i] = (uint16_t)(data[2 * i]) + ((uint16_t)(data[2 * i + 1]) << 8);

	uint32_t chk32 = 0;

	for (int i = 0; i < 10; ++i)
		chk32 = (chk32 << 1) + data_list[i];

	uint32_t checksum = (chk32 & 0x7FFF) + (chk32 >> 15);
	checksum = checksum & 0x7FFF;
	return (uint16_t)checksum;
}

void update_data(int angle, char* data)
{
	uint8_t x = data[0];
	uint8_t x1 = data[1];
	uint8_t x2 = data[2];
	uint8_t x3 = data[3];

	uint16_t dist_mm;
	uint16_t quality;
	dist_mm = (uint16_t) x | (((uint16_t) x1 & 0x3f) << 8);
	quality = (uint16_t) x2 | ((uint16_t) x3 << 8);
		
	if (angle < 36 || angle > 323)
		if (x1 & 0x80)
			printf("%i,%i,%i,BAD\n", angle, dist_mm, quality);
		else
			if (angle < 36)
				dists[35 - angle] = (uint16_t)(double(dist_mm) * cos(double(angle) * M_PI / 180) / 10);
			else
				dists[395 - angle] = (uint16_t)(double(dist_mm) * cos((359-double(angle)) * M_PI / 180) / 10);
	
	
		//if (!(x1 & 0x40))
		//printf("%i,%i,%i,OK\n", angle, dist_mm, quality);
	//else
		//printf("%i,%i,%i,NOT OK\n",angle,dist_mm,quality);
}

void read_Lidar() {
	int nb_errors = 0;

	for(;;)
	{
		try
		{
			Sleep(0.00001);
			char b[1];
			
			if (init_level == 0)
			{
				arduino->readSerialPort(b, 1);
				uint8_t bb = b[0];
				if (bb == 0xFA)
					init_level = 1;
				else
					init_level = 0;
			}
			else if (init_level == 1)
			{
				arduino->readSerialPort(b, 1);
				uint8_t bb = b[0];
				if (bb >= 0xA0 && bb <= 0xF9)
				{
					index = bb - 0xA0;
					init_level = 2;
				}
				else if (bb != 0xFA)
					init_level = 0;
			}
			else if (init_level == 2)
			{
				char b_speed[2];
				arduino->readSerialPort(b_speed, 2);

				char b_data0[4];
				arduino->readSerialPort(b_data0, 4);
				char b_data1[4];
				arduino->readSerialPort(b_data1, 4);
				char b_data2[4];
				arduino->readSerialPort(b_data2, 4);
				char b_data3[4];
				arduino->readSerialPort(b_data3, 4);

				char b_checksum[2];
				arduino->readSerialPort(b_checksum, 2);

				uint8_t all_data[20];
				all_data[0] = 0xFA;
				all_data[1] = index + 0xA0;
				memcpy(all_data + 2, b_speed, 2);
				memcpy(all_data + 4, b_data0, 4);
				memcpy(all_data + 8, b_data1, 4);
				memcpy(all_data + 12, b_data2, 4);
				memcpy(all_data + 16, b_data3, 4);



				uint16_t incoming_checksum = uint16_t(b_checksum[0]) + (uint16_t(b_checksum[1]) << 8);
				uint16_t computed_checksum = checksum(all_data);

				if (computed_checksum == incoming_checksum) {
					update_data(index * 4 + 0, b_data0);
					update_data(index * 4 + 1, b_data1);
					update_data(index * 4 + 2, b_data2);
					update_data(index * 4 + 3, b_data3);
				}
				else
					nb_errors++;
				init_level = 0;
			}
			else
				init_level = 0;

		}
		catch (const std::exception& e)
		{
			std::cout << e.what();
		}
	}
}


int main(int argc, char** argv)
{
	VideoCapture cap;
	
	// open the default camera, use something different from 0 otherwise;
	// Check VideoCapture documentation.
	if (!cap.open(0))
		return 0;
	cap.set(CV_CAP_PROP_FRAME_WIDTH, 1280);
	cap.set(CV_CAP_PROP_FRAME_HEIGHT, 720);
	cap.set(CV_CAP_PROP_FPS, fps);
	cap.set(CV_CAP_PROP_FORMAT, CV_8UC3);
	
	double dWidth = cap.get(CV_CAP_PROP_FRAME_WIDTH);
	double dHeight = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
	Size frameSize(static_cast<int>(dWidth), static_cast<int>(dHeight));
	VideoWriter oVideoWriter("out.avi", CV_FOURCC('P', 'I', 'M', '1'), 30, frameSize, true);

	arduino = new SerialPort(portName);
	std::cout << "is connected: " << arduino->isConnected() << std::endl;
	arduino->writeSerialPort((char*)MotorOn, sizeof(MotorOn));


	thread lidar_thread(read_Lidar);

	for (;;)
	{
		cap >> frame;
		if (frame.empty()) break; // end of video stream

		char S[10];
		for (int i = 0; i < 36; ++i)
		{
			uint16_t dist_av = (dists[2 * i] + dists[2 * i + 1]) / 2;

			sprintf_s(S, "%i", dist_av);

			cv::Scalar color;
			if (dist_av < 100)
				color = cv::Scalar(0x00, 0x00, 0xFF);
			else if (dist_av < 200)
				color = cv::Scalar(0x00, 0xFF, 0xFF);
			else
				color = cv::Scalar(0x00, 0xFF, 0x00);

			putText(frame, S, cvPoint(i * 36, 500), cv::FONT_HERSHEY_COMPLEX, 0.5, color, 1, cv::LINE_AA);
		}
		
		oVideoWriter.write(frame);
		imshow("this is you, smile! :)", frame);

		if (waitKey(10) == 27)
		{
			lidar_thread.detach();
			arduino->writeSerialPort((char*)MotorOff, sizeof(MotorOff));
			Sleep(100);
			break; // stop by pressing ESC
		}
	}
	// the camera will be closed automatically upon exit
	return 0;
}