#pragma once
#include "util.h"

class Global_Variable
{
public:
	static Global_Variable* Instance()
	{
		if (_instance == nullptr)
		{
			_instance = new Global_Variable();
		}

		return _instance;
	}

private:
private:
	Global_Variable() {
		char tmp[256];
		output_folder = "cap";
		if (GetEnvironmentVariableA("CAPTURE_DIR", tmp, sizeof(tmp)))
			output_folder = tmp;


		time_t mytime = time(NULL);
		tm mytm;
		localtime_s(&mytm, &mytime);
		char stamp[128] = { 0 };
		strftime(stamp, sizeof(stamp), "/%Y_%m_%d_%H_%M_%S/", &mytm);
		output_folder = output_folder + stamp;
		CreateDirectory(output_folder.c_str(), NULL);
		LOG(INFO) << "GTAV Camera matrix saved in " << output_folder;
	};
	Global_Variable(Global_Variable&) = delete;
	Global_Variable& operator=(Global_Variable&) = delete;

	static Global_Variable* _instance;

// Global variables
public:
	//int cur_frame_id;
	//float4x4 cur_g_world;
	//float4x4 cur_g_world_view;
	//float4x4 cur_g_world_view_project;

	struct camera_matrix
	{
		int frame_id;
		float4x4 g_world;
		float4x4 g_world_view;
		float4x4 g_world_view_project;

		void dump()
		{
			auto gv = Global_Variable::Instance();

			std::string frame_name = std::to_string(frame_id);
			frame_name = std::string(8 - frame_name.size(), '0') + frame_name;
			std::string fname = gv->output_folder + "\\" + frame_name + "_camera" + ".json";
			
			std::ofstream output(fname);
			if (output.is_open())
			{
				output << "{";
				output << "\"g_world\" :" << toJSON(g_world) << ",";
				output << "\"g_world_view\":" << toJSON(g_world_view) << ",";
				output << "\"g_world_view_proj\":" << toJSON(g_world_view_project) << ",";
				output << "\"frame_id\":" << "[" << frame_id << "]";
				output << "}";
			}
			else
				LOG(INFO) << "Error when dumping camera matrix";

			output.close();
		}

		std::string toJSON(const float4x4 &m)
		{
			return "[" + std::to_string(m.d[0][0]) + "," + std::to_string(m.d[0][1]) + "," + std::to_string(m.d[0][2]) + "," + std::to_string(m.d[0][3]) + ","
				+ std::to_string(m.d[1][0]) + "," + std::to_string(m.d[1][1]) + "," + std::to_string(m.d[1][2]) + "," + std::to_string(m.d[1][3]) + ","
				+ std::to_string(m.d[2][0]) + "," + std::to_string(m.d[2][1]) + "," + std::to_string(m.d[2][2]) + "," + std::to_string(m.d[2][3]) + ","
				+ std::to_string(m.d[3][0]) + "," + std::to_string(m.d[3][1]) + "," + std::to_string(m.d[3][2]) + "," + std::to_string(m.d[3][3]) + "]";
		}
	};

	camera_matrix cur_cam;
	std::string output_folder;
};