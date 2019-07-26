
void main(in float4 pos: SV_Position, in float4 dup_pos: DUP_Position,out float cable_disparity : SV_Target6, out uint cable : SV_Target7) {
	// id_out = base_id;
	
	cable = 255;
	cable_disparity = 1.0 / pos.w;
}
