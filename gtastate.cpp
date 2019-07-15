#include "gtastate.h"
#include "log.h"
#include "sdk.h"
#include "scripthook/main.h"
#include "scripthook/types.h"
#include "scripthook/enums.h"
#include "scripthook/natives.h"
#include <string>
#include <ostream>
#include <mutex>
#include <Windows.h>
#include <fstream>
#include <unordered_map>

const float TRACKING_RAD = 0.5f;
const float TRACKING_QUAT = 0.1f;

struct Tracker {
	static std::mutex is_tracking;
	static TrackedFrame current, returned;
	static uint64_t current_id, returned_id;
	static bool stop_tracking, currently_tracking;

	static void Main() {
		current_id = returned_id = 1;
		stop_tracking = false;
		currently_tracking = true;
		while (!stop_tracking) {
			{
				std::lock_guard<std::mutex> lock(is_tracking);
				current.fetch();
				current_id += 1;
			}
			WAIT(0);
		}
		currently_tracking = false;
		TERMINATE();
	}
	static TrackedFrame * nextFrame() {
		if (!current_id) return nullptr;
		std::lock_guard<std::mutex> lock(is_tracking);
		uint64_t delta = current_id - returned_id;
		if (delta > 0) {
			//for (int i = 0; i < N_OBJECTS; i++) {
			//	if (returned.objects[i].id == current.objects[i].id) {
			//		returned.objects[i].age = current.objects[i].age = returned.objects[i].age + (uint32_t)delta;
			//		returned.objects[i].p = current.objects[i].p;
			//		returned.objects[i].q = current.objects[i].q;
			//		// Let's associate the private data with the returned object only [no swapping here]
			//	} else if (current.objects[i].id) {
			//		returned.objects[i] = current.objects[i];
			//	} else if (returned.objects[i].id) {
			//		returned.objects[i].id = 0;
			//		returned.objects[i].private_data.reset();
			//	}
			//}
			//returned.object_map.swap(current.object_map);
			//returned.info = current.info;
			//returned_id = current_id;
		}
		return &returned;
	}
	static bool stop() {
		stop_tracking = true;
		return !currently_tracking;
	}
};
bool stopTracker() {
	return Tracker::stop();
}
std::mutex Tracker::is_tracking;
TrackedFrame Tracker::current;
TrackedFrame Tracker::returned;
uint64_t Tracker::current_id = 0, Tracker::returned_id = 0;
bool Tracker::stop_tracking = false;
bool Tracker::currently_tracking = false;

TrackedFrame * trackNextFrame() {
	return Tracker::nextFrame();
}

void initGTA5State(HMODULE hInstance) {
	scriptRegister(hInstance, Tracker::Main);
}
void releaseGTA5State(HMODULE hInstance) {
	scriptUnregister(hInstance);
}

TrackedFrame::TrackedFrame() :object_map(TRACKING_RAD) {}

uint32_t ID(uint32_t id, TrackedFrame::ObjectType t) {
	return ((uint32_t)t) << 28 | id;
}

// Weather hash map
std::unordered_map<Hash, std::string> weather_map
{
	{2544503417,			"EXTRASUNNY"	}	,
	{916995460,				"CLEAR"			}	,
	{821931868,				"CLOUDS"		}	,
	{282916021,				"SMOG"			}	,
	{2926802500,			"FOGGY"			}	,
	{3146353965,			"OVERCAST"		}	,
	{1420204096,			"RAIN"			}	,
	{3061285535,			"THUNDER"		}	,
	{1840358669,			"CLEARING"		}	,
	{2764706598,			"NEUTRAL"		}	,
	{4021743606,				"SNOW"			}	,
	{669657108 ,				"BLIZZARD"		}	,
	{603685163 ,			"SNOWLIGHT"		}	,
	{2865350805,				"XMAS"			}
};

inline Vec3f Vector3_2_Vec3f(Vector3 v)
{
	return Vec3f{ v.x, v.y, v.z };
}

// #json_track_fetch
void TrackedFrame::fetch() {
	//static std::mutex fetching;
	//std::lock_guard<std::mutex> lock(fetching);

	// Clear the tracker
	// object_map.clear();

	Player p = PLAYER::PLAYER_ID();
	Ped pp = PLAYER::PLAYER_PED_ID();

	Vector3 x = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(pp, 0.0, 0.0, 0.0);
	info.position = { x.x, x.y, x.z };
	x = ENTITY::GET_ENTITY_FORWARD_VECTOR(pp);
	info.forward_vector = { x.x, x.y, x.z };

	Vector3 position;
	Vector3 orientation;
	float fov;
	float near_plane;
	float far_plane;

	// #get_cam_param
	if (CAM::IS_GAMEPLAY_CAM_RENDERING())
	{
		position = CAM::GET_GAMEPLAY_CAM_COORD();
		orientation = CAM::GET_GAMEPLAY_CAM_ROT(2);
		fov = CAM::GET_GAMEPLAY_CAM_FOV();
		near_plane = invoke<float>(0xD0082607100D7193);
		far_plane = invoke<float>(0xDFC8CBC606FDB0FC);
	}
	else
	{
		Any cur_camera = CAM::GET_RENDERING_CAM();
		// camera parameters
		position = CAM::GET_CAM_COORD(cur_camera);
		orientation = CAM::GET_CAM_ROT(cur_camera, 2);
		fov = CAM::GET_CAM_FOV(cur_camera);
		near_plane = CAM::GET_CAM_NEAR_CLIP(cur_camera);
		far_plane = CAM::GET_CAM_FAR_CLIP(cur_camera);
	}

	info.cam_pos = { position.x, position.y, position.z };
	info.cam_ori = { orientation.x, orientation.y, orientation.z };
	info.fov = fov;
	info.near_plane = near_plane;
	info.far_plane = far_plane;

	info.hour = TIME::GET_CLOCK_HOURS();

	// get current weather hash
	info.weather = weather_map.at(invoke<Hash>(0x564B884A05EC45A3));

	// save current player id first
	info.skel_spine_root			= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_Spine_Root)));
	info.skel_spine_3				= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_Spine3)));
	info.skel_l_upperarm			= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_L_UpperArm)));
	info.skel_r_upperarm			= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_R_UpperArm)));
	info.skel_pelvis				= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_Pelvis)));
	info.skel_l_hand				= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_L_Hand)));
	info.skel_r_hand				= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_R_Hand)));
	info.skel_l_foot				= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_L_Foot)));
	info.skel_r_foot				= Vector3_2_Vec3f(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(pp, PED::GET_PED_BONE_INDEX(pp, SKEL_R_Foot)));
}

//TrackedFrame::Object * TrackedFrame::operator[](uint32_t id) {
//	uint32_t i = (id >> 8) & (N_OBJECTS-1);
//	if (objects[i].id == id)
//		return objects + i;
//	return nullptr;
//}
//
//const TrackedFrame::Object * TrackedFrame::operator[](uint32_t id) const {
//	uint32_t i = (id >> 8) & (N_OBJECTS - 1);
//	if (objects[i].id == id)
//		return objects + i;
//	return nullptr;
//}

TrackedFrame::Object * TrackedFrame::operator()(const Vec3f & p, const Quaternion & q, TrackedFrame::ObjectType t) {
	return operator()(p, q, TRACKING_RAD, TRACKING_QUAT, t);
}
TrackedFrame::Object * TrackedFrame::operator()(const Vec3f & p, const Quaternion & q, float radius, float angular_dist, TrackedFrame:: ObjectType t) {
	Object * r = nullptr;
	float d = radius * radius;
	object_map.find({ p.x, p.y }, [&](size_t i) {
		float dd = D2(objects[i].p, p);
		if (dd < d && D2(objects[i].q, q) < angular_dist && (t == UNKNOWN || objects[i].type() == t || (t == ObjectType::PED && objects[i].type() == ObjectType::PLAYER))) {
			d = dd;
			r = objects + i;
		}
	});
	return r;
}
TrackedFrame::Object * TrackedFrame::operator()(const Vec3f & p, const Quaternion & q) {
	return operator()(p, q, TRACKING_RAD, TRACKING_QUAT, UNKNOWN);
}

const TrackedFrame::Object * TrackedFrame::operator()(const Vec3f & p, const Quaternion & q) const {
	return ((TrackedFrame*)this)->operator()(p, q);
}

TrackedFrame::ObjectType TrackedFrame::Object::type() const {
	return (ObjectType)((id >> 28) & 0xf);
}

uint32_t TrackedFrame::Object::handle() const {
	return id & ((1<<28)-1);
}

TrackedFrame::PrivateData::~PrivateData() {}
