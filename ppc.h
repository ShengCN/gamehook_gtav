#pragma once
#include "util.h"

// pinhole camera model for camera control
class ppc
{
public:
	ppc() {}

	void set_mat(float4x4 view_matrix, float4x4& proj_matrix) { _view = view_matrix; _proj = proj_matrix; };
	void get_mat(float4x4& view_matrix, float4x4& proj_matrix) { view_matrix = _view; proj_matrix = _proj; };

	void pan(float deg) {

	}
	void tilt(float deg) {

	}
	void roll(float deg) {

	}

	void look_at(Vec3f p, Vec3f at, Vec3f up) {
		// construct view matrix

	}

	void translate(Vec3f& v) {
		_view.d[3][0] += v.x;
		_view.d[3][1] += v.y;
		_view.d[3][2] += v.z;
	}

	Vec3f get_cur_pos(){
		return Vec3f(_view.d[3][0], _view.d[3][1], _view.d[3][2]);
	}

private:
	float4x4 _view;
	float4x4 _proj;
};