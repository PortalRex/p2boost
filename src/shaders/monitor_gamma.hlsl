// DXVK does not apply D3D9's hardware gamma ramp. This tiny final pass
// reproduces Source's monitor-gamma curve on the completed back buffer and
// applies Quake-style r_intensity brightness scaling.
sampler2D frame_sampler : register( s0 );

// x stores mat_monitorgamma / Source's neutral 2.2 reference.
// y stores r_intensity, a linear RGB multiplier applied before the curve.
float4 gamma_curve : register( c0 );

float4 main( float2 texture_coordinate : TEXCOORD0 ) : COLOR0 {
	float4 color = tex2D( frame_sampler, texture_coordinate );
	color.rgb    = pow( saturate( color.rgb * gamma_curve.y ), gamma_curve.xxx );
	return color;
}