Texture2D<float> prev_disp: register(t1);
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

void main(in float4 p: SV_POSITION, in float2 t : TEX_COORD, out float2 flow : SV_Target0, out float disparity : SV_Target1, out float occlusion: SV_Target2)
{
	//float4 f = T.Sample(S, t);
	uint W, H;
	flow_disp.GetDimensions(W, H);
	int x = t.x*(W - 1), y = t.y*(H - 1);
	float4 f = flow_disp.Load(int3(x, y, 0));

	float X = (f.x + 1) * 0.5 * W, Y = (1 - f.y) * 0.5 * H, D = f.z;

	// Get the prior disparity
	float DD = prev_disp.Sample(S, float2((f.x + 1) * 0.5, (1 - f.y) * 0.5));
	disparity = disp_a * (f.w + disp_b);

	float prev_disparity = disp_a * (f.z + disp_b);
	occlusion = 1. / prev_disparity - 1. / DD;
	if (f.z <= 0)
		flow = 0. / 0.; // NaN
	else
		flow = float2(X-x, Y-y) - 0.5;
}
