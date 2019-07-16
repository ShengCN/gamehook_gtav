#include <windows.h>
#include <unordered_map>
#include <unordered_set>
#include "log.h"
#include "sdk.h"
#include <iomanip>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include "scripthook\main.h"
#include "scripthook\natives.h"
#include "util.h"
#include "gtastate.h"
#include "ps_output.h"
#include "vs_static.h"
#include "ps_flow.h"
#include "ps_noflow.h"
#include "ps_cable.h"
#include "Global_Variable.h"

uint32_t MAX_UNTRACKED_OBJECT_ID = 1 << 14;
Global_Variable* Global_Variable::_instance = nullptr;

enum KB_Keys
{
	Q = 0x51,
	E = 0x45,
	W = 0x57,
	A = 0x41,
	S = 0x53,
	D = 0x44,
	J = 0x4A,
	L = 0x4C,
	I = 0x49,
	K = 0x4B
};
// For debugging, use the current matrices, not the past to estimate the flow
//#define CURRENT_FLOW

double time() {
	auto now = std::chrono::system_clock::now();
	return std::chrono::duration<double>(now.time_since_epoch()).count();
}

struct TrackData: public TrackedFrame::PrivateData {
	struct BoneData {
		float data[255][3][4] = { 0 };
	};
	struct WheelData {
		float4x4 data[2] = { 0 };
	};

	uint32_t id=0, last_frame=0, has_prev_rage=0, has_cur_rage=0;
	float4x4 prev_rage[4] = { 0 }, cur_rage[4] = { 0 };
	std::unordered_map<int, BoneData> prev_bones, cur_bones;
	std::vector<WheelData> prev_wheels, cur_wheels;
	void swap() {
		has_prev_rage = has_cur_rage;
		memcpy(prev_rage, cur_rage, sizeof(prev_rage));
		prev_bones.swap(cur_bones);
		prev_wheels.swap(cur_wheels);
	}
};
struct VehicleTrack {
	float4x4 rage[4];
	float4x4 wheel[16][2];
};

enum class rendering_state
{
	first_pass,
	ready_for_capture,
	second_pass
};

// inject to some specific ps output
struct Texture_Injection
{
	ShaderHash ps_hash;
	std::vector<RenderTargetView> output;
	rendering_state cur_rendering_state;

	Texture_Injection(ShaderHash hash = ShaderHash(), rendering_state rs = rendering_state::first_pass) 
		:ps_hash(hash), cur_rendering_state(rs) {}
};

const size_t RAGE_MAT_SIZE = 4 * sizeof(float4x4);
const size_t VEHICLE_SIZE = RAGE_MAT_SIZE;
const size_t WHEEL_SIZE = RAGE_MAT_SIZE + 2 * sizeof(float4x4);
const size_t BONE_MTX_SIZE = 255 * 4 * 3 * sizeof(float);

struct GTA5 : public GameController {
	GTA5() : GameController() {
	}
	virtual bool keyDown(unsigned char key, unsigned char special_status) {
		auto gv = Global_Variable::Instance();
		// control if begin 
		if (key == KB_Keys::J) {
			
			if (gv->is_record) {
				LOG(INFO) << "Stop recording";
				gv->is_record = false;
			}
			else {
				gv->is_record = true;
				LOG(INFO) << "Begin recording";
			}
			
		}

		return false;
	}
	virtual std::vector<ProvidedTarget> providedTargets() const override {
		return { {"albedo"}, {"normal"},{"final"}, {"water"}, { "prev_disp", TargetType::R32_FLOAT, true }};
	}
	virtual std::vector<ProvidedTarget> providedCustomTargets() const {
		// Write the disparity into a custom render target (this name needs to match the injection shader buffer name!)
		return { { "flow_disp", TargetType::R32G32B32A32_FLOAT, true},
				 { "flow", TargetType::R32G32_FLOAT},
				 { "disparity", TargetType::R32_FLOAT },
				 { "occlusion", TargetType::R32_FLOAT },
				 { "semantic_out", TargetType::R32_UINT },
				 { "cable_disparity", TargetType::R32_FLOAT},
				 { "cable", TargetType::R32_UINT}
		};
	}

	CBufferVariable rage_matrices = { "rage_matrices", "gWorld", {0}, {4 * 16 * sizeof(float)} },
		rage_gWorld_View = { "rage_matrices", "gWorldView", {4 * 16 * sizeof(float)}, {4 * 16 * sizeof(float)} },
		rage_gWorld_Proj = { "rage_matrices", "gWorldViewProj", {2 * 4 * 16 * sizeof(float)}, {4 * 16 * sizeof(float)} },
		wheel_matrices = { "matWheelBuffer", "matWheelWorld",{ 0 },{ 32 * sizeof(float) } },
		rage_bonemtx = { "rage_bonemtx", "gBoneMtx",{ 0 },{ BONE_MTX_SIZE } },
		cable_locals = { "cable_locals", "shader_radiusScale", {0}, {sizeof(float)} };

	enum ObjectType {
		UNKNOWN=0,
		VEHICLE=1,
		WHEEL=2,
		TREE=3,
		PEDESTRIAN=4,
		BONE_MTX=5,
		PLAYER=6,
	};
	std::unordered_map<ShaderHash, ObjectType> object_type;
	std::unordered_set<ShaderHash> final_shader;
	std::unordered_set<ShaderHash> water_shader;

	// #create_injection_shaders
	std::shared_ptr<Shader> vs_static_shader = Shader::create(ByteCode(VS_STATIC, VS_STATIC + sizeof(VS_STATIC))),
		ps_output_shader = Shader::create(ByteCode(PS_OUTPUT, PS_OUTPUT + sizeof(PS_OUTPUT)), { { "SV_Target6", "flow_disp" }, { "SV_Target7", "semantic_out" } }),
		ps_cable_shader = Shader::create(ByteCode(PS_CABLE, PS_CABLE + sizeof(PS_CABLE)), { {"SV_Target6", "cable_disparity"}, {"SV_Target7", "cable"} }),
		flow_shader = Shader::create(ByteCode(PS_FLOW, PS_FLOW + sizeof(PS_FLOW)), { { "SV_Target0", "flow" },{ "SV_Target1", "disparity" },{ "SV_Target2", "occlusion" } }),
		noflow_shader = Shader::create(ByteCode(PS_NOFLOW, PS_NOFLOW + sizeof(PS_NOFLOW)), { { "SV_Target0", "flow" },{ "SV_Target1", "disparity" },{ "SV_Target2", "occlusion" } });

	std::unordered_set<ShaderHash> int_position = 
	{ 
		ShaderHash("d05510b7:0d9c59d0:612cd23a:f75d5ebd"), 
		ShaderHash("c59148c8:e2f1a5ad:649bb9c7:30454a34"), 
		ShaderHash("8a028f64:80694472:4d55d5dd:14c329f2"), 
		ShaderHash("53a6e156:ff48c806:433fc787:c150b034"), 
		ShaderHash("86de7f78:3ecdef51:f1c6f9e3:c5e6338f"), 
		ShaderHash("f37799c8:b304710d:3296b36c:46ea12d4"), 
		ShaderHash("4b031811:b6bf1c7f:ef4cd0c1:56541537") 
	};

	// have not find some good way to do this
	std::unordered_set<ShaderHash> tree_hash_sets = { ShaderHash("9ededd5c:7ee01e39:8dc68358:6820a65c"), ShaderHash("b2a65ca7:aaa47d5c:1ed49508:45246fe6"), ShaderHash("7c6662bb:8d975d9d:6918a87d:3e1f330c"), ShaderHash("24991f12:b1f6bd73:e4ef5d92:b82790d3")};
	std::unordered_set<ShaderHash> road_hash_sets;
	std::unordered_set<ShaderHash> terrain_hash_sets;
	std::unordered_set<ShaderHash> ped_hash_sets;
	std::unordered_set<ShaderHash> vehicle_hash_sets;
	std::unordered_set<ShaderHash> cable_sets;

	// #inject_shaders
	virtual std::shared_ptr<Shader> injectShader(std::shared_ptr<Shader> shader) {
		if (shader->type() == Shader::VERTEX) {
			bool has_cable = cable_locals.scan(shader);
			if(has_cable) {
				LOG(INFO) << "Find cable vertex";
				auto r = shader->subset({ "SV_Position" });
				r->renameOutput("SV_Position", "DUP_Position");
				return r;
			}

			bool has_rage_matrices = rage_matrices.scan(shader);
			if (has_rage_matrices) {
				bool has_wheel = wheel_matrices.scan(shader);
				bool has_rage_bonemtx = rage_bonemtx.scan(shader);
				ObjectType ot;
				if (has_wheel)
					object_type[shader->hash()] = ot = WHEEL;
				else if (hasCBuffer(shader, "vehicle_globals") && hasCBuffer(shader, "vehicle_damage_locals"))
					object_type[shader->hash()] = ot = VEHICLE;
				else if (hasCBuffer(shader, "trees_common_locals"))
					object_type[shader->hash()] = ot = TREE;
				else if (hasCBuffer(shader, "ped_common_shared_locals"))
					object_type[shader->hash()] = ot = PEDESTRIAN;
				else if (has_rage_bonemtx)
					object_type[shader->hash()] = ot = BONE_MTX;

				bool can_inject = true;
				for (const auto & b : shader->cbuffers())
					if (b.bind_point == 0)
						can_inject = false;
				//if (int_position.count(shader->hash()))
				//	can_inject = false;
				if (can_inject) {
					// Duplicate the shader and copy rage matrices
					auto r = shader->subset({ "SV_Position" });
					r->renameOutput("SV_Position", "PREV_POSITION");
					r->renameCBuffer("rage_matrices", "prev_rage_matrices");
					if (ot == WHEEL)
						r->renameCBuffer("matWheelBuffer", "prev_matWheelBuffer", 5);
					if (ot == PEDESTRIAN || ot == BONE_MTX)
						r->renameCBuffer("rage_bonemtx", "prev_rage_bonemtx", 5);
					// TODO: Handle characters properly
					return r;

					return vs_static_shader;
				}
			}
		}

		// #find_final_shader
		if (shader->type() == Shader::PIXEL) {
			if (containsCBuffer(shader, "trees") || containsCBuffer(shader, "grass"))
			{
				// LOG(INFO) << "Find some trees";
				 tree_hash_sets.insert(shader->hash());
			}
			
			if (containsCBuffer(shader, "ped_"))
			{
				ped_hash_sets.insert(shader->hash());
			}

			if (containsCBuffer(shader, "vehicle"))
			{
				vehicle_hash_sets.insert(shader->hash());
			}

			if (containsCBuffer(shader, "terrain"))
			{
				terrain_hash_sets.insert(shader->hash());
			}
		
			if (containsCBuffer(shader, "detail_map_locals"))
			{
				road_hash_sets.insert(shader->hash());
			}

			if (containsCBuffer(shader, "cable")) {
				LOG(INFO) << "Find cable " << shader->hash();
				cable_sets.insert(shader->hash());
			}

			// prior to v1.0.1365.1
			if (hasTexture(shader, "BackBufferTexture")) {
				final_shader.insert(shader->hash());
			}
			// v1.0.1365.1 and newer
			if (hasTexture(shader, "SSLRSampler") && hasTexture(shader, "HDRSampler")) {
				// Other candidate textures include "MotionBlurSampler", "BlurSampler", but might depend on graphics settings
				final_shader.insert(shader->hash());
			}

			if (hasCBuffer(shader, "cable_locals")) {
				// Inject cable shaders
				LOG(INFO) << "Inject into cable shaders";
				return ps_cable_shader;
			}

			if (hasCBuffer(shader, "misc_globals")) {
				// Inject the shader output
				return ps_output_shader;
			}
		}
		return nullptr;
	}

	virtual void postProcess(uint32_t frame_id) override {

		if (currentRecordingType() != NONE) {
			// Estimate the projection matrix (or at least a subset of it's values)
			// a 0 0 0
			// 0 b 0 0
			// 0 0 c e
			// 0 0 d 0
#define DOT(a,b,i,j) (a[0][i]*b[0][j] + a[1][i]*b[1][j] + a[2][i]*b[2][j] + a[3][i]*b[3][j])
		// With a bit of math we can see that avg_world_view_proj[:][3] = d * avg_world_view[:][2], below is the least squares estimate for d
			float d = DOT(avg_world_view_proj, avg_world_view, 3, 2) / DOT(avg_world_view, avg_world_view, 2, 2);
			// and avg_world_view_proj[:][2] = c * avg_world_view[:][2] + e * avg_world_view[:][3], below is the least squares estimate for c and e
			float A00 = DOT(avg_world_view, avg_world_view, 2, 2), A10 = DOT(avg_world_view, avg_world_view, 2, 3), A11 = DOT(avg_world_view, avg_world_view, 3, 3);
			float b0 = DOT(avg_world_view, avg_world_view_proj, 2, 2), b1 = DOT(avg_world_view, avg_world_view_proj, 3, 2);

			float det = 1.f / (A00*A11 - A10 * A10);
			float c = det * (b0 * A11 - b1 * A10), e = det * (b1 * A00 - b0 * A10);

			// The camera params either change, or there is a ceratin degree of numerical instability (and fov changes, but setting the camera properly is hard ;( )
			float disp_ab[2] = { 6., 4e-5 }; // { -d / e, -c / d };
			// LOG(INFO) << "disp_ab " << -d / e << " " << -c / d;
			disparity_correction->set((const float*)disp_ab, 2, 0 * sizeof(float));
			bindCBuffer(disparity_correction);

			if (currentRecordingType() == DRAW)
				callPostFx(flow_shader);
			else
				callPostFx(noflow_shader);
		}
	}

	float4x4 avg_world = 0, avg_world_view = 0, avg_world_view_proj = 0, prev_view = 0, prev_view_proj = 0;
	uint32_t oid = 1, base_id = 1;

	ShaderHash water_hash = ShaderHash("cb8085c2:13bf714f:153d91b3:548e1f2e");
	std::unordered_set<ShaderHash> ao_set;
	// deferred shading injection, albedo, normal
	Texture_Injection main_pass_injection = Texture_Injection(); 
	Texture_Injection water_inject = Texture_Injection(water_hash);

	// #cbuffer
	std::shared_ptr<CBuffer> id_buffer, cable_buffer, prev_buffer, prev_wheel_buffer, prev_rage_bonemtx, disparity_correction;
	TrackedFrame * tracker = nullptr;
	std::shared_ptr<TrackData> last_vehicle;

	std::shared_ptr<CBuffer> cur_rage_buffer;

	double start_time;
	uint32_t current_frame_id = 1, wheel_count = 0;

	size_t TS = 0;

	// #start_frame
	virtual void startFrame(uint32_t frame_id) override {
		start_time = time();

		// reset injection
		main_pass_injection = Texture_Injection();
		main_pass_injection.output = std::vector<RenderTargetView>(2);

		water_inject = Texture_Injection(water_hash);
		water_inject.output = std::vector<RenderTargetView>(1);

		// cbuffer creation
		if (!id_buffer) id_buffer = createCBuffer("IDBuffer", sizeof(int));
		// if (!cable_buffer) cable_buffer = createCBuffer("CableBuffer", sizeof(int));

		if (!cur_rage_buffer) cur_rage_buffer = createCBuffer("rage_matrices", 4 * sizeof(float4x4));
		if (!prev_buffer) prev_buffer = createCBuffer("prev_rage_matrices", 4*sizeof(float4x4));
		if (!prev_wheel_buffer) prev_wheel_buffer = createCBuffer("prev_matWheelBuffer", 4 * sizeof(float4x4));
		if (!prev_rage_bonemtx) prev_rage_bonemtx = createCBuffer("prev_rage_bonemtx", BONE_MTX_SIZE);
		if (!disparity_correction) disparity_correction = createCBuffer("disparity_correction", 2*sizeof(float));
		base_id = oid = 1;
		last_vehicle.reset();
		wheel_count = 0;

		// #track_frame_game_state_information
		tracker = trackNextFrame();

		avg_world = 0;
		avg_world_view = 0;
		avg_world_view_proj = 0;
		TS = 0;
	}

	// #end_frame
	virtual void endFrame(uint32_t frame_id) override {
		if (currentRecordingType() != NONE) {
			mul(&prev_view_proj, avg_world.affine_inv(), avg_world_view_proj);
			mul(&prev_view, avg_world.affine_inv(), avg_world_view);
			current_frame_id++;

			// Copy the disparity buffer for occlusion testing
			copyTarget("prev_disp", "disparity");

			auto gv = Global_Variable::Instance();
			gv->cur_cam.frame_id = frame_id;
			gv->cur_cam.dump();

			LOG(INFO) <<"Current frame: " << gv->cur_cam.frame_id << " T = " << time() - start_time << "   S = " << TS;
		}
	}

	void set_semantic_buffer(const DrawInfo& info) {
		if (tree_hash_sets.count(info.pixel_shader))
		{
			id_buffer->set(256);
			bindCBuffer(id_buffer);
		}
		else if (vehicle_hash_sets.count(info.pixel_shader))
		{
			id_buffer->set(64);
			bindCBuffer(id_buffer);
		}
		else if (terrain_hash_sets.count(info.pixel_shader))
		{
			id_buffer->set(32);
			bindCBuffer(id_buffer);
		}
		else if (road_hash_sets.count(info.pixel_shader))
		{
			id_buffer->set(16);
			bindCBuffer(id_buffer);
		}
		else
		{
			id_buffer->set(info.texture_id);
			bindCBuffer(id_buffer);
		}
	}

	int counter_tt = 0;
	// #draw_function
	virtual DrawType startDraw(const DrawInfo & info) override {
		// First time initialize ppc
		auto gv = Global_Variable::Instance();

		if ((currentRecordingType() != NONE) && info.outputs.size() && info.outputs[0].W == defaultWidth() && info.outputs[0].H == defaultHeight() && info.outputs.size() >= 2 
			&& info.type == DrawInfo::INDEX && info.instances == 0 && !cable_sets.count(info.pixel_shader)) {

			set_semantic_buffer(info);

			bool has_rage_matrices = rage_matrices.has(info.vertex_shader);
			ObjectType type = UNKNOWN;
			{
				auto i = object_type.find(info.vertex_shader);
				if (i != object_type.end())
					type = i->second;
			}
			if (has_rage_matrices && main_pass_injection.cur_rendering_state != rendering_state::second_pass) {
				std::shared_ptr<GPUMemory> wp = rage_matrices.fetch(this, info.vertex_shader, info.vs_cbuffers, true);

				if (main_pass_injection.cur_rendering_state == rendering_state::first_pass) {
					// Starting the main render pass
					main_pass_injection.output[0] = info.outputs[0];
					main_pass_injection.output[1] = info.outputs[1];

					main_pass_injection.cur_rendering_state = rendering_state::ready_for_capture;
				}
				if (main_pass_injection.cur_rendering_state == rendering_state::ready_for_capture) {
#pragma region camera_matrix
					uint32_t id = 0;
					if (wp && wp->size() >= 3 * sizeof(float4x4)) {
						// Fetch the rage matrices gWorld, gWorldView, gWorldViewProj
						const float4x4 * rage_mat = (const float4x4 *)wp->data();

						gv->cur_cam.w = info.outputs[0].W;
						gv->cur_cam.h = info.outputs[0].H;

						gv->cur_cam.g_world = rage_mat[0];
						gv->cur_cam.g_world_view = rage_mat[1];
						gv->cur_cam.g_world_view_project = rage_mat[2];

						//prev_buffer->set((float4x4*)prev_rage, 4, 0);
						//bindCBuffer(prev_buffer);
						
						return RIGID;
					}
#pragma endregion camera_matrix
				}
			}
		} else if (main_pass_injection.cur_rendering_state == rendering_state::ready_for_capture) {
			// End of the main render pass
			copyTarget("albedo", main_pass_injection.output[0]);
			copyTarget("normal", main_pass_injection.output[1]);
			main_pass_injection.cur_rendering_state = rendering_state::second_pass;
		}

		if (info.pixel_shader == water_inject.ps_hash)
		{
			water_inject.output[0] = info.outputs[0];
			water_inject.cur_rendering_state = rendering_state::ready_for_capture;
		}
		else if (water_inject.cur_rendering_state == rendering_state::ready_for_capture)
		{
			copyTarget("water", water_inject.output[0]);
			water_inject.cur_rendering_state = rendering_state::second_pass;
		}


		if ((currentRecordingType() != NONE) && info.outputs.size() && info.outputs[0].W == defaultWidth() && info.outputs[0].H == defaultHeight()) {
			if (cable_sets.count(info.pixel_shader)) {
				// LOG(INFO) << "Draw Cable";
				id_buffer->set(100);
				bindCBuffer(id_buffer);
				return RIGID;
			}
		}

		return DEFAULT;
	}
	virtual void endDraw(const DrawInfo & info) override {

		if (final_shader.count(info.pixel_shader)) {
			// Draw the final image (right before the image is distorted)
			copyTarget("final", info.outputs[0]);
		}
	}
	virtual std::string gameState() const override {
		if (tracker && currentRecordingType() == RecordingType::DRAW)
			return toJSON(tracker->info);
		return "";
	}
	virtual bool stop() { return stopTracker(); }

	double last_recorded_frame = 0, frame_timestamp = 0;
	float fps = 0.5;
	std::mutex get_cam;
	virtual RecordingType recordFrame(uint32_t frame_id) override {
		auto gv = Global_Variable::Instance();
		if (gv->is_record) {

			frame_timestamp = time();
			if ((frame_timestamp - last_recorded_frame) * fps >= 1) {
				bool is_need_wait = true;
				Vector3 position;
				{
					std::lock_guard<std::mutex> lock(get_cam);

					if (CAM::IS_GAMEPLAY_CAM_RENDERING()) {
						position = CAM::GET_GAMEPLAY_CAM_COORD();
					}
					else {
						Any cur_camera = CAM::GET_RENDERING_CAM();
						// camera parameters
						position = CAM::GET_CAM_COORD(cur_camera);
					}
				}

				float diff_x = gv->last_pos.x - position.x;
				float diff_y = gv->last_pos.y - position.y;
				float diff_z = gv->last_pos.z - position.z;

				float diff = std::sqrt(diff_x * diff_x + diff_y * diff_y + diff_z * diff_z);
				gv->last_pos = position;

				if (diff > 50.0f) {
					LOG(INFO) << diff << "Distances between frames are too large, wait to loading...";
					gv->time_counter = 0;
				}
				else if (gv->time_counter < gv->wait_counter) {
					gv->time_counter++;
				}
				else {
					is_need_wait = false;
				}

				// do a check for game loading
				if (is_need_wait) {
					return RecordingType::NONE;
				}
				else {
					last_recorded_frame = frame_timestamp;
					return RecordingType::DRAW;
				}

			}
			else {
				return RecordingType::NONE;
			}
			
			
		}

		return RecordingType::NONE;
	}
};
REGISTER_CONTROLLER(GTA5);


BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		LOG(INFO) << "GTA5 turned on";
		initGTA5State(hInst);
	}

	if (reason == DLL_PROCESS_DETACH) {
		releaseGTA5State(hInst);
		LOG(INFO) << "GTA5 turned off";
	}
	return TRUE;
}
