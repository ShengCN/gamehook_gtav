#pragma once
#include <memory>
#include <unordered_map>
#include "json.h"
#include "util.h"

#ifndef _WINDEF_
struct HINSTANCE__; // Forward or never
typedef HINSTANCE__* HINSTANCE;
typedef HINSTANCE HMODULE;
#endif
void initGTA5State(HMODULE hInstance);
void releaseGTA5State(HMODULE hInstance);


struct GameInfo {
	Vec3f position, forward_vector;
	Vec3f cam_pos, cam_ori;
	float fov;
	float near_plane;
	float far_plane;
};
TOJSON(GameInfo, position, forward_vector, cam_pos, cam_ori, fov, near_plane, far_plane)

// N_OBJECTS Maximum number of frames, needs to be a power of 2
#define N_OBJECTS (1<<13)
struct TrackedFrame {
	enum ObjectType {
		UNKNOWN = 0,
		PED = 1,
		VEHICLE = 2,
		OBJECT = 3,
		PICKUP = 4,
		PLAYER = 5,
	};
	struct PrivateData {
		virtual ~PrivateData();
	};
	struct Object {
		uint32_t id = 0;
		uint32_t age = 0;
		Vec3f p;
		Quaternion q;
		std::shared_ptr<PrivateData> private_data;
		ObjectType type() const;
		uint32_t handle() const;
	};
public:
	friend struct Tracker;
	Object objects[N_OBJECTS];
	NNSearch2D<size_t> object_map;
	void fetch();

public:
	TrackedFrame();
	GameInfo info;
	//Object * operator[](uint32_t id);
	//const Object * operator[](uint32_t id) const;
	Object * operator()(const Vec3f & v, const Quaternion & q);
	Object * operator()(const Vec3f & v, const Quaternion & q, ObjectType t);
	Object * operator()(const Vec3f & v, const Quaternion & q, float D, float QD, ObjectType t);
	const Object * operator()(const Vec3f & v, const Quaternion & q) const;
};


TrackedFrame * trackNextFrame();
bool stopTracker();
