cbuffer IDBuffer: register(b0) {
	uint semantic_id = 0;
};

void main(in float4 pos: SV_Position, out uint semantic_out : SV_Target6) {
	// id_out = base_id;
	semantic_out = semantic_id;
}