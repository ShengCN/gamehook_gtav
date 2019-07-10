#pragma once
#include "util.h"

// pinhole camera model for camera control
class ppc
{
public:
	ppc() {}
	
	void compute_cbuffer(float4x4* out_matrix, const float4x4* in_matrix) {
		// gWorld, gWorldView, gWorldViewProj, gViewInverse
		float4x4 gWorld			= in_matrix[0];
		float4x4 gWorldView		= in_matrix[1];
		float4x4 gWorldViewProj = in_matrix[2];
		float4x4 gViewInverse	= in_matrix[3];

		float4x4 view;
		float4x4 proj;

		mul(&view, gWorld.affine_inv(), gWorldView);
		mul(&proj, gWorldView.affine_inv(), gWorldViewProj);

		out_matrix[0] = in_matrix[0];
		// Add offest
		translate(view, offset);
		mul(&out_matrix[1], gWorld, view);
		mul(&out_matrix[2], gWorldView, proj);
		out_matrix[3] = view.affine_inv();
	}
	
	void pan(float deg) {

	}
	void tilt(float deg) {

	}
	void roll(float deg) {

	}

	void look_at(Vec3f p, Vec3f at, Vec3f up) {
		// construct view matrix

	}

	void translate(float4x4 &view_mat, Vec3f& v) {
		view_mat.d[3][0] += v.x;
		view_mat.d[3][1] += v.y;
		view_mat.d[3][2] += v.z;
	}

	//Vec3f get_cur_pos(){
	//	return Vec3f{_view.d[3][0], _view.d[3][1], _view.d[3][2] };
	//}

	Vec3f offset = {10.0f, 0.0f, 0.0f};
};