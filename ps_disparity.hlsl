Texture2D<float4> flow_disp: register(t0);

SamplerState S : register(s0) {
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = BORDER;
	AddressV = BORDER;
	BorderColor = float4(0, 0, 0, 0);
};
cbuffer disparity_correction {
	float disp_a = 1.0;
	float disp_b = 0.0; // Correct the disparity disp_a * (z + disp_b)
};

void main(in float4 p: SV_POSITION, in float2 t : TEX_COORD, out float disparity : SV_Target0) {
	//float4 f = T.Sample(S, t);
	uint W, H;
	flow_disp.GetDimensions(W, H);
	int x = t.x*(W - 1), y = t.y*(H - 1);
	float4 f = flow_disp.Load(int3(x, y, 0));

	// Get the prior disparity
	disparity = disp_a * (f.w + disp_b);
}
