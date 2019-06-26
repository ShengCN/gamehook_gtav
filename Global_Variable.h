#pragma once
#include "util.h"

class Global_Variable
{
public:
	static Global_Variable* Instance()
	{
		if (_instance == nullptr)
			_instance = new Global_Variable();

		return _instance;
	}

	~Global_Variable() { if (_instance) delete _instance; }
private:
	Global_Variable() {};

	static Global_Variable* _instance;

// Global variables
public:
	int cur_frame_id;
	float4x4 cur_g_world;
	float4x4 cur_g_world_view;
	float4x4 cur_g_world_view_project;
};