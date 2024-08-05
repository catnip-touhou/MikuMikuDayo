

//Ro,Rdから正規化する
float4 MapRdRo(float3 ro, float3 rd)
{
	float4 r;
	r.xy = ro.xy;
	r.zw = rd.xy;
	return r;
}

void UnmapRdRo(float4 r, out float3 ro, out float3 rd)
{
	float pz = 13.8;

	rd.xy = r.zw;
	rd.z = sqrt(1-dot(rd.xy,rd.xy));
	ro = float3(r.xy - rd.x/rd.z/13.8,0);
}



float4 Polynomial(float4 r, int idx)
{
	float4 s;
	if (idx == 0) {
		s.x = 8.8699722 * r.z + 2.0130253 * r.z * r.w * r.w + 2.013025 * r.z * r.z * r.z + 0.039059892 * r.y * r.z * r.w + 0.0020029698 * r.y * r.y * r.z + 1.0057716 * r.x + -0.0025225959 * r.x * r.w * r.w + 0.036537293 * r.x * r.z * r.z + -0.00099516287 * r.x * r.y * r.w + -6.7422479e-05 * r.x * r.y * r.y + 0.001007807 * r.x * r.x * r.z + -6.7422479e-05 * r.x * r.x * r.x;
		s.y = 8.8699722 * r.w + 2.013025 * r.w * r.w * r.w + 2.0130253 * r.z * r.z * r.w + 1.0057716 * r.y + 0.036537293 * r.y * r.w * r.w + -0.0025225959 * r.y * r.z * r.z + 0.001007807 * r.y * r.y * r.w + -6.7422479e-05 * r.y * r.y * r.y + 0.039059892 * r.x * r.z * r.w + -0.00099516299 * r.x * r.y * r.z + 0.0020029698 * r.x * r.x * r.w + -6.7422479e-05 * r.x * r.x * r.y;
		s.z = 0.93202728 * r.z + -0.024942072 * r.z * r.w * r.w + -0.024942072 * r.z * r.z * r.z + 0.0016509357 * r.y * r.z * r.w + 3.9309165e-05 * r.y * r.y * r.z + -0.0070567736 * r.x + -0.0014917396 * r.x * r.w * r.w + 0.00015919592 * r.x * r.z * r.z + 0.00011370747 * r.x * r.y * r.w + -1.0337172e-06 * r.x * r.y * r.y + 0.00015301665 * r.x * r.x * r.z + -1.0337171e-06 * r.x * r.x * r.x;
		s.w = 0.93202728 * r.w + -0.024942072 * r.w * r.w * r.w + -0.024942072 * r.z * r.z * r.w + -0.0070567736 * r.y + 0.00015919592 * r.y * r.w * r.w + -0.0014917399 * r.y * r.z * r.z + 0.00015301665 * r.y * r.y * r.w + -1.0337171e-06 * r.y * r.y * r.y + 0.0016509359 * r.x * r.z * r.w + 0.00011370747 * r.x * r.y * r.z + 3.9309165e-05 * r.x * r.x * r.w + -1.0337172e-06 * r.x * r.x * r.y;
	} else if (idx == 1) {
		s.x = 8.9530268 * r.z + 2.0643215 * r.z * r.w * r.w + 2.0643215 * r.z * r.z * r.z + 0.038565524 * r.y * r.z * r.w + 0.0019821557 * r.y * r.y * r.z + 1.0050545 * r.x + -0.0027840242 * r.x * r.w * r.w + 0.035781503 * r.x * r.z * r.z + -0.00099006284 * r.x * r.y * r.w + -6.5642358e-05 * r.x * r.y * r.y + 0.00099209277 * r.x * r.x * r.z + -6.5642358e-05 * r.x * r.x * r.x;
		s.y = 8.9530268 * r.w + 2.0643215 * r.w * r.w * r.w + 2.0643215 * r.z * r.z * r.w + 1.0050545 * r.y + 0.035781503 * r.y * r.w * r.w + -0.0027840242 * r.y * r.z * r.z + 0.00099209277 * r.y * r.y * r.w + -6.5642358e-05 * r.y * r.y * r.y + 0.038565524 * r.x * r.z * r.w + -0.00099006295 * r.x * r.y * r.z + 0.0019821557 * r.x * r.x * r.w + -6.5642358e-05 * r.x * r.x * r.y;
		s.z = 0.93272328 * r.z + -0.024973257 * r.z * r.w * r.w + -0.024973257 * r.z * r.z * r.z + 0.0016332282 * r.y * r.z * r.w + 3.7926842e-05 * r.y * r.y * r.z + -0.0069878493 * r.x + -0.001482296 * r.x * r.w * r.w + 0.00015093212 * r.x * r.z * r.z + 0.0001103 * r.x * r.y * r.w + -1.0339072e-06 * r.x * r.y * r.y + 0.00014822683 * r.x * r.x * r.z + -1.0339074e-06 * r.x * r.x * r.x;
		s.w = 0.93272328 * r.w + -0.024973257 * r.w * r.w * r.w + -0.024973257 * r.z * r.z * r.w + -0.0069878493 * r.y + 0.00015093212 * r.y * r.w * r.w + -0.0014822962 * r.y * r.z * r.z + 0.00014822683 * r.y * r.y * r.w + -1.0339074e-06 * r.y * r.y * r.y + 0.0016332282 * r.x * r.z * r.w + 0.0001103 * r.x * r.y * r.z + 3.7926839e-05 * r.x * r.x * r.w + -1.0339073e-06 * r.x * r.x * r.y;
	} else {
		s.x = 8.9892044 * r.z + 2.0870454 * r.z * r.w * r.w + 2.0870457 * r.z * r.z * r.z + 0.038366374 * r.y * r.z * r.w + 0.0019714185 * r.y * r.y * r.z + 1.0048113 * r.x + -0.0028662565 * r.x * r.w * r.w + 0.035500113 * r.x * r.z * r.z + -0.00098652684 * r.x * r.y * r.w + -6.4886532e-05 * r.x * r.y * r.y + 0.00098489155 * r.x * r.x * r.z + -6.4886532e-05 * r.x * r.x * r.x;
		s.y = 8.9892044 * r.w + 2.0870457 * r.w * r.w * r.w + 2.0870454 * r.z * r.z * r.w + 1.0048113 * r.y + 0.035500113 * r.y * r.w * r.w + -0.0028662565 * r.y * r.z * r.z + 0.00098489155 * r.y * r.y * r.w + -6.4886532e-05 * r.y * r.y * r.y + 0.038366374 * r.x * r.z * r.w + -0.00098652684 * r.x * r.y * r.z + 0.0019714185 * r.x * r.x * r.w + -6.4886532e-05 * r.x * r.x * r.y;
		s.z = 0.9330759 * r.z + -0.024970582 * r.z * r.w * r.w + -0.024970585 * r.z * r.z * r.z + 0.0016251245 * r.y * r.z * r.w + 3.7412861e-05 * r.y * r.y * r.z + -0.0069455327 * r.x + -0.0014761613 * r.x * r.w * r.w + 0.00014896289 * r.x * r.z * r.z + 0.00010889882 * r.x * r.y * r.w + -1.0299735e-06 * r.x * r.y * r.y + 0.00014631166 * r.x * r.x * r.z + -1.0299734e-06 * r.x * r.x * r.x;
		s.w = 0.9330759 * r.w + -0.024970585 * r.w * r.w * r.w + -0.02497058 * r.z * r.z * r.w + -0.0069455327 * r.y + 0.00014896289 * r.y * r.w * r.w + -0.0014761612 * r.y * r.z * r.z + 0.00014631166 * r.y * r.y * r.w + -1.0299734e-06 * r.y * r.y * r.y + 0.0016251244 * r.x * r.z * r.w + 0.00010889883 * r.x * r.y * r.z + 3.7412858e-05 * r.x * r.x * r.w + -1.0299735e-06 * r.x * r.x * r.y;
	}

	/*
	s.x = 7.1666203 * r.z + 1.4752886 * r.z * r.w * r.w + 1.4752886 * r.z * r.z * r.z + 0.051542707 * r.y * r.z * r.w + 0.0030645526 * r.y * r.y * r.z + 1.0054961 * r.x + -0.0057294876 * r.x * r.w * r.w + 0.04581321 * r.x * r.z * r.z + -0.0027102001 * r.x * r.y * r.w + -0.00018092417 * r.x * r.y * r.y + 0.00035435241 * r.x * r.x * r.z + -0.00018092417 * r.x * r.x * r.x;
	s.y = 7.1666203 * r.w + 1.4752886 * r.w * r.w * r.w + 1.4752886 * r.z * r.z * r.w + 1.0054961 * r.y + 0.04581321 * r.y * r.w * r.w + -0.0057294876 * r.y * r.z * r.z + 0.00035435241 * r.y * r.y * r.w + -0.00018092417 * r.y * r.y * r.y + 0.051542707 * r.x * r.z * r.w + -0.0027101999 * r.x * r.y * r.z + 0.0030645523 * r.x * r.x * r.w + -0.00018092417 * r.x * r.x * r.y;
	s.z = 0.88403523 * r.z + -0.033239361 * r.z * r.w * r.w + -0.033239361 * r.z * r.z * r.z + 0.0034137436 * r.y * r.z * r.w + 5.992661e-05 * r.y * r.y * r.z + -0.015503249 * r.x + -0.0021788999 * r.x * r.w * r.w + 0.0012348442 * r.x * r.z * r.z + 0.00025614217 * r.x * r.y * r.w + -7.2805356e-06 * r.x * r.y * r.y + 0.0003160687 * r.x * r.x * r.z + -7.2805356e-06 * r.x * r.x * r.x;
	s.w = 0.88403523 * r.w + -0.033239361 * r.w * r.w * r.w + -0.033239357 * r.z * r.z * r.w + -0.015503249 * r.y + 0.0012348442 * r.y * r.w * r.w + -0.0021788999 * r.y * r.z * r.z + 0.0003160687 * r.y * r.y * r.w + -7.2805356e-06 * r.y * r.y * r.y + 0.0034137436 * r.x * r.z * r.w + 0.00025614217 * r.x * r.y * r.z + 5.992661e-05 * r.x * r.x * r.w + -7.280536e-06 * r.x * r.x * r.y;
	*/

	/*
	if (idx == 0) {
		//400nm
		s.x = 31.852661 * r.z + 17.570412 * r.z * r.w * r.w + 17.570412 * r.z * r.z * r.z + -0.4626717 * r.y * r.z * r.w + -8.6430693e-05 * r.y * r.y * r.z + 0.77226073 * r.x + -0.28023791 * r.x * r.w * r.w + -0.74290943 * r.x * r.z * r.z + -0.0025440985 * r.x * r.y * r.w + 5.337211e-05 * r.x * r.y * r.y + -0.002630529 * r.x * r.x * r.z + 5.3372103e-05 * r.x * r.x * r.x;
		s.y = 31.852661 * r.w + 17.570412 * r.w * r.w * r.w + 17.570414 * r.z * r.z * r.w + 0.77226073 * r.y + -0.74290943 * r.y * r.w * r.w + -0.28023788 * r.y * r.z * r.z + -0.002630529 * r.y * r.y * r.w + 5.3372103e-05 * r.y * r.y * r.y + -0.4626717 * r.x * r.z * r.w + -0.002544099 * r.x * r.y * r.z + -8.643046e-05 * r.x * r.x * r.w + 5.3372103e-05 * r.x * r.x * r.y;
		s.z = 0.88930148 * r.z + 0.19351238 * r.z * r.w * r.w + 0.19351232 * r.z * r.z * r.z + 0.0073146662 * r.y * r.z * r.w + -3.3359167e-05 * r.y * r.y * r.z + -0.0098336339 * r.x + 0.0022920235 * r.x * r.w * r.w + 0.0096066874 * r.x * r.z * r.z + -3.6523998e-05 * r.x * r.y * r.w + 6.5463973e-07 * r.x * r.y * r.y + -6.9883157e-05 * r.x * r.x * r.z + 6.5463769e-07 * r.x * r.x * r.x;
		s.w = 0.88930148 * r.w + 0.19351232 * r.w * r.w * r.w + 0.19351232 * r.z * r.z * r.w + -0.0098336339 * r.y + 0.0096066874 * r.y * r.w * r.w + 0.002292024 * r.y * r.z * r.z + -6.9883157e-05 * r.y * r.y * r.w + 6.5463769e-07 * r.y * r.y * r.y + 0.0073146662 * r.x * r.z * r.w + -3.6524027e-05 * r.x * r.y * r.z + -3.3359167e-05 * r.x * r.x * r.w + 6.5463814e-07 * r.x * r.x * r.y;
	} else if (idx == 1) {
		//550nm
		s.x = 31.823044 * r.z + 17.283552 * r.z * r.w * r.w + 17.283552 * r.z * r.z * r.z + -0.45244476 * r.y * r.z * r.w + -0.00018812669 * r.y * r.y * r.z + 0.77495438 * r.x + -0.27790752 * r.x * r.w * r.w + -0.73035216 * r.x * r.z * r.z + -0.0028935261 * r.x * r.y * r.w + 5.3026029e-05 * r.x * r.y * r.y + -0.0030816537 * r.x * r.x * r.z + 5.3026059e-05 * r.x * r.x * r.x;
		s.y = 31.823044 * r.w + 17.283552 * r.w * r.w * r.w + 17.283552 * r.z * r.z * r.w + 0.77495438 * r.y + -0.73035216 * r.y * r.w * r.w + -0.27790752 * r.y * r.z * r.z + -0.0030816537 * r.y * r.y * r.w + 5.3026059e-05 * r.y * r.y * r.y + -0.45244479 * r.x * r.z * r.w + -0.0028935275 * r.x * r.y * r.z + -0.00018812693 * r.x * r.x * r.w + 5.3026022e-05 * r.x * r.x * r.y;
		s.z = 0.88500434 * r.z + 0.18608274 * r.z * r.w * r.w + 0.18608277 * r.z * r.z * r.z + 0.0072038127 * r.y * r.z * r.w + -3.8733662e-05 * r.y * r.y * r.z + -0.0098721534 * r.x + 0.0021645338 * r.x * r.w * r.w + 0.0093683526 * r.x * r.z * r.z + -4.5399807e-05 * r.x * r.y * r.w + 4.730598e-07 * r.x * r.y * r.y + -8.4133499e-05 * r.x * r.x * r.z + 4.730598e-07 * r.x * r.x * r.x;
		s.w = 0.88500434 * r.w + 0.18608277 * r.w * r.w * r.w + 0.18608274 * r.z * r.z * r.w + -0.0098721534 * r.y + 0.0093683526 * r.y * r.w * r.w + 0.0021645334 * r.y * r.z * r.z + -8.4133499e-05 * r.y * r.y * r.w + 4.730598e-07 * r.y * r.y * r.y + 0.0072038136 * r.x * r.z * r.w + -4.5399778e-05 * r.x * r.y * r.z + -3.8733655e-05 * r.x * r.x * r.w + 4.7306048e-07 * r.x * r.x * r.y;
	} else {
		//700nm
		s.x = 31.821266 * r.z + 17.188812 * r.z * r.w * r.w + 17.188812 * r.z * r.z * r.z + -0.44768766 * r.y * r.z * r.w + -0.00023167045 * r.y * r.y * r.z + 0.77668577 * r.x + -0.27636337 * r.x * r.w * r.w + -0.724051 * r.x * r.z * r.z + -0.0030271094 * r.x * r.y * r.w + 5.2960218e-05 * r.x * r.y * r.y + -0.0032587796 * r.x * r.x * r.z + 5.2960226e-05 * r.x * r.x * r.x;
		s.y = 31.821266 * r.w + 17.188812 * r.w * r.w * r.w + 17.188812 * r.z * r.z * r.w + 0.77668577 * r.y + -0.724051 * r.y * r.w * r.w + -0.27636337 * r.y * r.z * r.z + -0.0032587796 * r.y * r.y * r.w + 5.2960226e-05 * r.y * r.y * r.y + -0.44768766 * r.x * r.z * r.w + -0.0030271094 * r.x * r.y * r.z + -0.00023167068 * r.x * r.x * r.w + 5.2960218e-05 * r.x * r.x * r.y;
		s.z = 0.88408077 * r.z + 0.18349332 * r.z * r.w * r.w + 0.18349323 * r.z * r.z * r.z + 0.0071575074 * r.y * r.z * r.w + -4.0221974e-05 * r.y * r.y * r.z + -0.009847099 * r.x + 0.0021196203 * r.x * r.w * r.w + 0.0092771314 * r.x * r.z * r.z + -4.7939626e-05 * r.x * r.y * r.w + 4.1795874e-07 * r.x * r.y * r.y + -8.8161527e-05 * r.x * r.x * r.z + 4.1795874e-07 * r.x * r.x * r.x;
		s.w = 0.88408077 * r.w + 0.18349323 * r.w * r.w * r.w + 0.18349335 * r.z * r.z * r.w + -0.009847099 * r.y + 0.0092771314 * r.y * r.w * r.w + 0.0021196199 * r.y * r.z * r.z + -8.8161527e-05 * r.y * r.y * r.w + 4.1795874e-07 * r.y * r.y * r.y + 0.0071575064 * r.x * r.z * r.w + -4.7939597e-05 * r.x * r.y * r.z + -4.0221988e-05 * r.x * r.x * r.w + 4.1795897e-07 * r.x * r.x * r.y;		
	}
	*/

	/*
	if (idx == 0) {
		//400nm
		s.x = 1.1712668 * r.z + -9.2922419e-05 * r.z * r.w * r.w + -9.2922419e-05 * r.z * r.z * r.z + 0.00018595405 * r.y * r.z * r.w + -4.0742212e-05 * r.y * r.y * r.z + -0.3990061 * r.x + 7.045173e-05 * r.x * r.w * r.w + 0.00025640579 * r.x * r.z * r.z + -6.61374e-05 * r.x * r.y * r.w + -3.231642e-06 * r.x * r.y * r.y + -0.00010687959 * r.x * r.x * r.z + -3.2316448e-06 * r.x * r.x * r.x;
		s.y = 1.1712668 * r.w + -9.2922419e-05 * r.w * r.w * r.w + -9.2922448e-05 * r.z * r.z * r.w + -0.3990061 * r.y + 0.00025640579 * r.y * r.w * r.w + 7.0451722e-05 * r.y * r.z * r.z + -0.00010687959 * r.y * r.y * r.w + -3.2316448e-06 * r.y * r.y * r.y + 0.00018595406 * r.x * r.z * r.w + -6.61374e-05 * r.x * r.y * r.z + -4.0742212e-05 * r.x * r.x * r.w + -3.2316402e-06 * r.x * r.x * r.y;
		s.z = 0.0013063047 * r.z + 7.9303572e-07 * r.z * r.w * r.w + 7.9303572e-07 * r.z * r.z * r.z + -4.2228567e-07 * r.y * r.z * r.w + -1.3398983e-07 * r.y * r.y * r.z + -0.011139937 * r.x + -2.3626581e-07 * r.x * r.w * r.w + -6.5855147e-07 * r.x * r.z * r.z + 1.6049876e-07 * r.x * r.y * r.w + 4.9364451e-07 * r.x * r.y * r.y + 2.6509042e-08 * r.x * r.x * r.z + 4.9364439e-07 * r.x * r.x * r.x;
		s.w = 0.0013063047 * r.w + 7.9303572e-07 * r.w * r.w * r.w + 7.9303572e-07 * r.z * r.z * r.w + -0.011139937 * r.y + -6.5855147e-07 * r.y * r.w * r.w + -2.3626535e-07 * r.y * r.z * r.z + 2.6509042e-08 * r.y * r.y * r.w + 4.9364439e-07 * r.y * r.y * r.y + -4.2228658e-07 * r.x * r.z * r.w + 1.6049853e-07 * r.x * r.y * r.z + -1.3398915e-07 * r.x * r.x * r.w + 4.9364462e-07 * r.x * r.x * r.y;
	} else if (idx == 1) {
		//550nm
		s.x = 1.1735896 * r.z + -9.7483862e-05 * r.z * r.w * r.w + -9.7483891e-05 * r.z * r.z * r.z + 0.00018906458 * r.y * r.z * r.w + -4.091128e-05 * r.y * r.y * r.z + -0.39863518 * r.x + 7.0655624e-05 * r.x * r.w * r.w + 0.00025972014 * r.x * r.z * r.z + -6.5602115e-05 * r.x * r.y * r.w + -2.6969074e-06 * r.x * r.y * r.y + -0.00010651338 * r.x * r.x * r.z + -2.6969083e-06 * r.x * r.x * r.x;
		s.y = 1.1735896 * r.w + -9.7483891e-05 * r.w * r.w * r.w + -9.7483862e-05 * r.z * r.z * r.w + -0.39863518 * r.y + 0.00025972014 * r.y * r.w * r.w + 7.0655631e-05 * r.y * r.z * r.z + -0.00010651338 * r.y * r.y * r.w + -2.6969083e-06 * r.y * r.y * r.y + 0.00018906457 * r.x * r.z * r.w + -6.5602107e-05 * r.x * r.y * r.z + -4.0911284e-05 * r.x * r.x * r.w + -2.6969074e-06 * r.x * r.x * r.y;
		s.z = 0.0012139585 * r.z + 3.8517373e-07 * r.z * r.w * r.w + 3.8517373e-07 * r.z * r.z * r.z + -2.3293978e-07 * r.y * r.z * r.w + -1.6437514e-07 * r.y * r.y * r.z + -0.011086112 * r.x + -1.4116767e-07 * r.x * r.w * r.w + -3.7410973e-07 * r.x * r.z * r.z + 1.2234341e-07 * r.x * r.y * r.w + 5.0402559e-07 * r.x * r.y * r.y + -4.2030479e-08 * r.x * r.x * r.z + 5.0402537e-07 * r.x * r.x * r.x;
		s.w = 0.0012139585 * r.w + 3.8517373e-07 * r.w * r.w * r.w + 3.8517464e-07 * r.z * r.z * r.w + -0.011086112 * r.y + -3.7410973e-07 * r.y * r.w * r.w + -1.4116767e-07 * r.y * r.z * r.z + -4.2030479e-08 * r.y * r.y * r.w + 5.0402537e-07 * r.y * r.y * r.y + -2.3293978e-07 * r.x * r.z * r.w + 1.2234364e-07 * r.x * r.y * r.z + -1.6437536e-07 * r.x * r.x * r.w + 5.0402565e-07 * r.x * r.x * r.y;
	} else {
		//700nm
		s.x = 1.1752986 * r.z + -9.896417e-05 * r.z * r.w * r.w + -9.896417e-05 * r.z * r.z * r.z + 0.00018987578 * r.y * r.z * r.w + -4.0853451e-05 * r.y * r.y * r.z + -0.39861289 * r.x + 7.0639071e-05 * r.x * r.w * r.w + 0.00026051491 * r.x * r.z * r.z + -6.5224573e-05 * r.x * r.y * r.w + -2.5124309e-06 * r.x * r.y * r.y + -0.00010607808 * r.x * r.x * r.z + -2.5124282e-06 * r.x * r.x * r.x;
		s.y = 1.1752986 * r.w + -9.896417e-05 * r.w * r.w * r.w + -9.8964141e-05 * r.z * r.z * r.w + -0.39861289 * r.y + 0.00026051491 * r.y * r.w * r.w + 7.0639064e-05 * r.y * r.z * r.z + -0.00010607808 * r.y * r.y * r.w + -2.5124282e-06 * r.y * r.y * r.y + 0.00018987578 * r.x * r.z * r.w + -6.5224573e-05 * r.x * r.y * r.z + -4.0853454e-05 * r.x * r.x * r.w + -2.5124309e-06 * r.x * r.x * r.y;
		s.z = 0.001227444 * r.z + 2.6111502e-07 * r.z * r.w * r.w + 2.6111411e-07 * r.z * r.z * r.z + -1.7139701e-07 * r.y * r.z * r.w + -1.756049e-07 * r.y * r.y * r.z + -0.011074542 * r.x + -1.1107477e-07 * r.x * r.w * r.w + -2.8247086e-07 * r.x * r.z * r.z + 1.067126e-07 * r.x * r.y * r.w + 5.0820768e-07 * r.x * r.y * r.y + -6.8892859e-08 * r.x * r.x * r.z + 5.0820779e-07 * r.x * r.x * r.x;
		s.w = 0.001227444 * r.w + 2.6111411e-07 * r.w * r.w * r.w + 2.6111593e-07 * r.z * r.z * r.w + -0.011074542 * r.y + -2.8247086e-07 * r.y * r.w * r.w + -1.1107568e-07 * r.y * r.z * r.z + -6.8892859e-08 * r.y * r.y * r.w + 5.0820779e-07 * r.y * r.y * r.y + -1.713961e-07 * r.x * r.z * r.w + 1.0671351e-07 * r.x * r.y * r.z + -1.7560512e-07 * r.x * r.x * r.w + 5.0820779e-07 * r.x * r.x * r.y;		
	}
	*/
	return s;
}
/*
//入力 ... d:イメージセンサ上の座標, Xi:乱数, camMat:カメラの姿勢(ビュー→ワールド)
//出力 ... ro, rd : レイの位置・向き(ワールド)
float LensSystem(float2 d, float2 Xi, out float3 ro, inout float3 rd, float3x3 camMat, int wave)
{
	float aspectRatio = (resolution.x / resolution.y);
	float sensorHeight = 24;	//イメージセンサのサイズ[mm]
	float2 sensorSize = {aspectRatio * sensorHeight, sensorHeight};
	float aperture = 0.5;			//絞り半径[mm]
	//イメージセンサ上の座標をmm単位で求める
	float3 s = float3(d*sensorSize/2 * float2(-1,1), -sensorHeight*fov);

	float3 ap = float3(CosSin(Xi.y*2*PI)*aperture, 0);
	rd = normalize(ap-s);

	float beta = pow(max(rd.z,1e-3),1);	//cos4乗則

	float4 r = MapRdRo(ap,rd);

	//ここで多項式の評価
	float4 r1,r2;
	float3 o1,o2,d1,d2;
	float k;
	float l = WaveChannelToLambda(wave);
	if (l < 550) {
		r1 = Polynomial(r, 0);
		r2 = Polynomial(r, 1);
		UnmapRdRo(r1, o1, d1);
		UnmapRdRo(r2, o2, d2);
		k = InvLerp(400,550,l);
	} else {
		r1 = Polynomial(r, 1);
		r2 = Polynomial(r, 2);
		UnmapRdRo(r1, o1,d1);
		UnmapRdRo(r2, o2,d2);
		k = InvLerp(550,700,l);
	}
	ro = lerp(o1,o2,k);
	rd = normalize(lerp(d1,d2,k));

	rd = mul(rd,camMat);
	ro = cameraPosition + (ro.x*camMat[0] + ro.y*camMat[1]) / 80;	//[mm]を[MMD]に直す
	return beta;
}
*/

struct Lens {
	float P;			//位置(光軸上の表面のZ値)
	float R;			//曲率半径(+で凸)
	float nd;			//屈折率(nd)
	float v;			//アッベ数
	float OR;			//開口半径
};

//レンズ表面の点Pに対するレンズの法線を得る
float3 LensNormal(float3 P,  Lens L)
{
	return sign(L.R) * normalize(P - float3(0,0,L.P + L.R));
}

//レイとレンズの当たり判定(当たったら0より大きい値、当たらなかったら0)
float RayLens(float3 ro, float3 rd, Lens L, bool backward = false)
{
	float3 C = float3(0,0, L.P + L.R);	//レンズの曲率中心
	float t0,t1;

	if (!RaySphere(ro-C, rd, L.R, t0,t1))
		return 0;

	//レイの発射元から見て 凸なら+、凹なら-
	float convex = (backward ? -1 : 1) * L.R;

	//凹面の場合はt1優先、そうでない場合はt0優先
	if (convex < 0)
		return t1;
	else
		return t0;
}

static int NLens = 2;		//フレネルインターフェイスの数
static Lens Lenses[] = {
	//位置,	曲率r,	屈折率,	アッベ数 半径
	0,		80,		1.67,	5,		10,
	10,		-80, 	1,		10000,	10
};

/*
static int NLens = 7;		//フレネルインターフェイスの数
static Lens Lenses[] = {
	//位置,	曲率r,	屈折率,	アッベ数
	0,		48.91,	1.691,	54.71,	50,
	7.05,	-183.92,	1,	10000,	50,
	17.69,	-40.93,	1.640,	34.6,	50,
	19.56,	59.06,		1,	10000,	50,
	23.72,	-306.84,1.549,	45.4,	50,
	25.82,	115.33,	1.691,	54.71,	50,
	35.62,	-42.97,		1,	10000,	8,
};
*/

float LensIOR(int i, float lambda)
{
	return DispersedIORFromAbbe(Lenses[i].nd, Lenses[i].v, lambda);
}

//レンズ群の中をレイトレする
//ro,rdはビュー座標系、かつ長さの単位は[mm]
//返り値は透過率
//backward : falseでカメラの中(像側)から物体側へトレース。trueで物体側からカメラの中(像側)へトレース
//last_ref = trueの時、最後のレンズでは反射先と反射率を得る
float3 TraceLens(int iStart, int iEnd, inout float3 ro, inout float3 rd, bool backward, float lambda, bool last_ref = false)
{
	float3 tr = 1;
	int i = iStart;
	int incl = backward ? -1 : 1;

	float ior;
	if (backward)
		ior = LensIOR(i,lambda);
	else
		ior = i ? LensIOR(i-1,lambda) : 1;
		
	while(1) {
		float t = RayLens(ro, rd, Lenses[i], backward);

		if (!t)
			return 0;

		float3 P = ro + t*rd;

		//鏡筒とぶっかった
		if (length(P.xy) > Lenses[i].OR)
			return 0;
		
		float3 N = LensNormal(P, Lenses[i]);
		N = backward ? -N : N;
		float lensIOR = backward ? (i?LensIOR(i-1,lambda):1) : LensIOR(i,lambda);
		float3 rr;
		float F = lerp(IORtoF0(ior, lensIOR), 1, FresnelSchlick(abs(dot(rd,N))));	//TODO:反射防止コートの色を入れる
		if (i == iEnd && last_ref) {
			rr = reflect(rd,N);
			tr *= F;
		} else {
			rr = refract(rd,N, ior/lensIOR);
			tr *= 1 - F;
		}
		rd = rr;
		ro = P;
		ior = lensIOR;

		if (i == iEnd)
			break;
		i += incl;
	}

	return tr;
}

//XY平面上の原点中心に置かれた半径rのディスクとレイの当たり判定
bool RayDiscXY(float r, float3 ro, float3 rd, out float t, out float2 uv)
{
	if (rd.z == 0)
		return false;

	t = - ro.z /rd.z;
	float3 P = ro + t * rd;	//XY平面と交差するレイの位置
	if ( (t > 0) && (dot(P.xy,P.xy) <= r*r) ) {
		uv = P.xy/r * 0.5 + 0.5;
		uv.y = 1-uv.y;
		return true;
	} else {
		return false;
	}
}

//レンズ表面をサンプリング
float3 SampleLens(Lens L, float2 Xi)
{
	float3 C = float3(0,0,L.P+L.R);	//レンズの曲率中心
	float cosTMax = sqrt(1-sqr(L.OR/abs(L.R)));
	float ct = 1- Xi.x + Xi.x * cosTMax;
	float st = sqrt(1-ct*ct);
	float sp,cp;
	sincos(Xi.y*2*PI, sp,cp);
	return C + float3(st*cp,st*sp,-ct)*L.R;
}

//入力 ... d:スクリーンスペースでの位置(0が中心、-1が左上、+1が右下), Xi:乱数, camMat:カメラの姿勢(ビュー→ワールド)
//出力 ... ro, rd : レイの位置・向き(ワールド), flarePos:スクリーンスペースでのレンズフレアの位置
float LensSystem(float2 d, float2 Xi, out float3 ro, inout float3 rd, float3x3 camMat, int waveCh, out float2 flarePos, out float3 flareBeta)
{
	//この関数内ではビュー座標系、長さの単位を[mm]として計算する
	float aspectRatio = (resolution.x / resolution.y);
	float sensorHeight = 24;	//イメージセンサのサイズ[mm]
	float2 sensorSize = {aspectRatio * sensorHeight, sensorHeight};
	float bf = 79.83;//sensorHeight*fov;		//センサから直近のレンズまでの距離[mm]
	float aperture = 1;			//絞り半径[mm]
	float3 ap = float3(CosSin(Xi.x*2*PI) * sqrt(Xi.y) * aperture,-0.1);	//絞り上の一点
	
	ro = float3(sensorSize/2 * d * float2(-1,1), -bf);	//センサー上の点
	float3 sensorPos = ro;
	rd = normalize(ap-ro);
	float beta = pow(rd.z,4);	//cos^4則

	float lambda = WaveChannelToLambda(waveCh);

	//レンズを全部通った後の透過率を求める。ro,rdには最後のレンズ上でのレイの位置と向きが入る
	beta *= TraceLens(0,NLens-1, ro, rd, false, lambda, false);

	//鏡筒にぶっかったなどでレイが物体に向かわなかった場合は帰る
	if (!beta) { flarePos = 0;	flareBeta = 0;	return 0; }

	//レンズフレアの計算
	flareBeta = 1;
	int lastLens = NLens - 1;
	flarePos = 0;
	float4 Xi2 = Hash4(uint4(DispatchRaysIndex().xy, iFrame, 268));
	int iFlare1 = floor(Xi2.x * lastLens);						//反射して物体側へ戻るレンズ番号
	int iFlare2 = lastLens - floor(Xi2.y * (lastLens-iFlare1));	//反射して像側へまた進むレンズ番号

	//ライトサンプル版
	float4 Xi3 = Hash4(uint4(DispatchRaysIndex().xy, iFrame, 409));
	float3 LeSky, LeLight;
	float pdfSky, pdfLight;
	float3 LSkyWorld = SampleSkybox(Xi3.xy, LeSky, pdfSky);
	float3 LSky = mul(camMat, LSkyWorld);
	float3 lightN; bool twosided;
	//float3 LLight = SampleLightList(rof, lensN, Xi3.xyz, LeLight, pdfLight, lightN, twosided);
	float3 rdf = -LSky;
	flareBeta *= LeSky * Visibility(cameraPosition, cameraPosition + LSkyWorld * 1e+5, -LSky, true) / pdfSky;
	//最も物体側のレンズ上のどこかをサンプル…画面の解像度でまんべんなくサンプルしてみる
	float3 rof = SampleLens(Lenses[lastLens], Xi2.zw);

	//float3 rof = float3(CosSin(Xi2.z*2*PI)*Lenses[lastLens].OR * sqrt(Xi2.w),Lenses[lastLens].P);

	//float resN = DispatchRaysDimensions().x * DispatchRaysDimensions().y;
	//float resI = DispatchRaysDimensions().x * DispatchRaysIndex().y + DispatchRaysIndex().x;
	//float3 rof = float3(CosSin(2*PI*resI*RCP_G)*Lenses[lastLens].OR * sqrt(resI/resN),Lenses[lastLens].P); 

	float3 lensN = LensNormal(rof, Lenses[lastLens]);
	flareBeta *= abs(dot(LSky,lensN));
	float3 llp = float3(0,0,Lenses[lastLens].P);
	flareBeta *= sqr(Lenses[lastLens].OR / aperture);


	if (any(flareBeta))
		flareBeta *= TraceLens(lastLens, iFlare1, rof, rdf, true, lambda, true);	//最後にtrue付けると最後のレンズ上では透過ではなく反射の計算をする
	if (any(flareBeta))
		flareBeta *= TraceLens(iFlare1+1, iFlare2, rof, rdf, false, lambda, true);
	if (any(flareBeta))
		flareBeta *= TraceLens(iFlare2-1, 0, rof, rdf, true, lambda, false);

	/*
	float3 rof = ro + rd*1e-3;
	float3 rdf = -rd;
	flareBeta *= TraceLens(lastLens, iFlare1, rof, rdf, true, lambda, true);	//最後にtrue付けると最後のレンズ上では透過ではなく反射の計算をする
	if (any(flareBeta))
		flareBeta *= TraceLens(iFlare1+1, iFlare2, rof, rdf, false, lambda, true);
	if (any(flareBeta))
		flareBeta *= TraceLens(iFlare2-1, 0, rof, rdf, true, lambda, false);

	flareBeta /= max(beta,1e-10);	//betaで割る … d方向へレイトレして最終的に得られる輝度への倍率がflareBetaなので
	//flareBeta *= smoothstep(1,0.75,length(d.xy));	//端をてきとうに薄めてヨシ！
	*/

	flareBeta *= NLens * (NLens-1) / 2; //選択のpdfで割る
	flareBeta *= pow(abs(rdf.z),4);	//cos^4則

	//絞りを通り抜けたか？
	float dmy;
	float2 auv;	//絞りテクスチャ参照用uv
	if (!RayDiscXY(aperture, rof - float3(0,0,ap.z),rdf, dmy,auv))
		flareBeta = 0;
	else 
		flareBeta *= 1;//apertureTex.SampleLevel(samp, auv, 0);


	//まず、[mm]単位のflareの位置を求める
	float3 fpos = rof + rdf * (-bf - rof.z) / rdf.z;

	//センサーサイズで割って、2倍すると、センサーの中心を0として、端を±1とする座標が得られる
	flarePos = fpos.xy / sensorSize * float2(-2,2);
	//flareBeta = 1;

	//mmをMMDに直してワールド座標系に変換
	ro = cameraPosition + mul(ro/80, camMat);
	rd = mul(rd, camMat);

	return beta;
}