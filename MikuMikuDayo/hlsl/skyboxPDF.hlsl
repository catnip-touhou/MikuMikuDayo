#include "yrz.hlsli"

Texture2D<float4>Skybox : register(t0);			//ソース画像

RWTexture2D<float>SkyboxAvgRow : register(u0);	//横一列の平均
RWTexture2D<float>SkyboxAvgAll : register(u1);		//全体の平均
RWTexture2D<float>SkyboxPDFRow : register(u2);		//pdf
RWTexture2D<float>SkyboxPDF : register(u3);			//pdf
RWTexture2D<float>SkyboxCDFRow : register(u4);		//cdf
RWTexture2D<float>SkyboxCDF : register(u5);			//cdf

//ルート2 … PDFExにLi(ωi)-2(1-CI)Li~を代入した後、正規化した物をSkyboxPDFに入れ直す
RWTexture2D<float>SkyboxPDFEx : register(u6);		//pdf改造
RWTexture2D<float>SkyboxPDFExRow : register(u7);			//pdf改造
RWTexture2D<float>SkyboxPDFExAvg : register(u8);			//pdf改造

//SH
RWStructuredBuffer<SHCoeff>SkyboxSHX : register(u9);	//各行をSHした結果 H要素
RWStructuredBuffer<SHCoeff>SkyboxSH : register(u10);	//↑を重みづけ平均したSH係数 1要素

float4 sky(uint2 uv)
{
	float max_lum = 1e+6;
	float4 C = Skybox[uv];
	float L = RGBtoY(C.rgb);

	if (L<=max_lum)
		return C;
	else
		return float4(C.rgb * max_lum / L, C.a);
}

//pdfの単位は何か？
//画像の縦一杯又は横一杯を1.0[ｲﾒｰｼﾞｻｲｽﾞ]とすると
//pdfの単位は[1/ｲﾒｰｼﾞｻｲｽﾞ]
//1ピクセルをサンプリングする場合、縦と横でのピクセルを決める必要が有るので、縦と横の同時確率の確率密度という事になり[1/ｲﾒｰｼﾞｻｲｽﾞ^2]になる

//横一列の平均を取る
[numthreads(32, 1, 1)]
void AvgRowCS( uint3 id : SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);
	float tc = 0;
	for (int x=0; x<W; x++) {
		float4 t = sky(uint2(x,id.x));
		tc += max(RGBtoY(t.rgb), 1e-6);	//1列全部0になってしまうケースに一応対策する
	}
	float sa = sin(PI * (id.x+0.5)/H);	//立体角で重みづけ
	SkyboxAvgRow[uint2(0,id.x)] = tc / W * sa;
}

//AvgRowの平均を取って全体の平均を求める
[numthreads(1, 1, 1)]
void AvgAllCS( uint3 id : SV_DispatchThreadID )
{
	float W,H;
	SkyboxAvgRow.GetDimensions(W,H);	//1xHピクセルのはず
	float tc = 0;
	for (int y=0; y<H; y++) {
		float t = SkyboxAvgRow[uint2(0,y)];
		tc += t;
	}
	SkyboxAvgAll[uint2(0,0)] = tc / H;
}

//各行のpdfを求める : p(v)
[numthreads(32,1,1)]
void PDFRowCS( uint3 id:SV_DispatchThreadID )
{
	SkyboxPDFRow[uint2(0,id.x)] = SkyboxAvgRow[uint2(0,id.x)] / SkyboxAvgAll[uint2(0,0)];
}

//列方向のpdfを各行ごとに求める,vが決まった時のp(u)なので条件付き確率密度 p(u|v)という事になる
//p(u|v) = p(u,v) / p(v) であり、同時確率密度p(u,v)を求めたい場合はp(u|v) * p(v) で良い
[numthreads(8,8,1)]
void PDFCS( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);

	float4 t = Skybox[uint2(id.x,id.y)];
	float sa = sin(PI * (id.y+0.5)/H);
	SkyboxPDF[uint2(id.x,id.y)] = max(RGBtoY(t.rgb),1e-6) / SkyboxAvgRow[uint2(0,id.y)] * sa;
}

//各行のcdfを求める
[numthreads(1,1,1)]
void CDFRowCS( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	SkyboxAvgRow.GetDimensions(W,H);	//1xHピクセルのはず

	SkyboxCDFRow[uint2(0,0)] = SkyboxPDFRow[uint2(0,0)] / H;
	for (int y=1; y<H; y++) {
		SkyboxCDFRow[uint2(0,y)] = SkyboxCDFRow[uint2(0,y-1)] + SkyboxPDFRow[uint2(0,y)] / H;
	}
}

//列方向のcdfを各行ごとに求める
[numthreads(32,1,1)]
void CDFCS( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);

	SkyboxCDF[uint2(0,id.x)] = SkyboxPDF[uint2(0,id.x)] / W;
	for (int x=1; x<W; x++) {
		SkyboxCDF[uint2(x,id.x)] = SkyboxCDF[uint2(x-1,id.x)] + SkyboxPDF[uint2(x,id.x)] / W;
	}
}


//pdfの計算、MISCompensation版
//正規化前のpdfの値
[numthreads(8,8,1)]
void PDFExCS( uint3 id:SV_DispatchThreadID )
{
	float4 t = sky(uint2(id.x,id.y));
	float LiWi = max(RGBtoY(t.rgb),1e-6);
	float CI = 0.75;	//0<CI<=1で指定。1の時は普通。小さい時はよりアグレッシブ
	float pdf = max(0, LiWi - 2*(1-CI)*SkyboxAvgAll[uint2(0,0)]);
	SkyboxPDFEx[id.xy] =  pdf;
}

//PDFExの行ごとの平均を求める
[numthreads(32,1,1)]
void PDFExRowCS( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);

	SkyboxPDFExRow[uint2(0,id.x)] = 0;
	for (int i=0; i<W; i++)
		SkyboxPDFExRow[uint2(0,id.x)] += SkyboxPDFEx[uint2(i,id.x)] / W;
}

//PDFExの全体の平均
[numthreads(1,1,1)]
void PDFExAvgCS( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);

	SkyboxPDFExAvg[uint2(0,0)] = 0;
	for (int i=0; i<H; i++)
		SkyboxPDFExAvg[uint2(0,0)] += SkyboxPDFExRow[uint2(0,i)] / H;
}

//PDFExの正規化
[numthreads(8,8,1)]
void PDF2CS( uint3 id:SV_DispatchThreadID )
{
	SkyboxPDF[id.xy] = SkyboxPDFEx[id.xy] / max(1e-6,SkyboxPDFExRow[uint2(0,id.y)]);
}

//PDFExRowの正規化
[numthreads(32,1,1)]
void PDF2RowCS( uint3 id:SV_DispatchThreadID )
{
	SkyboxPDFRow[uint2(0,id.x)] = SkyboxPDFExRow[uint2(0,id.x)] / SkyboxPDFExAvg[uint2(0,0)];
}

//SHの計算(横方向)
[numthreads(32,1,1)]
/* Y-up版。skyboxテクスチャの上方向をY+とする
void SHComboX( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);
	float3 n;
	float t = (id.x+0.5) / H * PI;
	float ct,st;
	sincos(t, st,ct);
	n.y = ct;
	SHCoeff C = (SHCoeff)0;

	for (int x=0; x<W; x++) {
		float p = (x+0.5) / W * 2*PI;
		float sp,cp;
		sincos(p, sp,cp);
		n.z = -cp * st;
		n.x = -sp * st;
		uint2 uv = {x,id.x};
		C.c[0] += Skybox[uv] * 0.282095;
		C.c[1] += Skybox[uv] * -0.488603 * n.x;
		C.c[2] += Skybox[uv] * 0.488603 * n.y;
		C.c[3] += Skybox[uv] * -0.488603 * n.z;
		C.c[4] += Skybox[uv] * 1.092548 * n.x * n.z;
		C.c[5] += Skybox[uv] * -1.092548 * n.y * n.z;
		C.c[6] += Skybox[uv] * 0.315392 * (3 * n.y * n.y - 1);
		C.c[7] += Skybox[uv] * -1.092548 * n.x * n.y;
		C.c[8] += Skybox[uv] * 0.546274 * (n.x * n.x - n.z * n.z);
	}

	for (int i=0; i<9; i++) {
		C.c[i] /= W;
	}
	SkyboxSHX[id.x] = C;
}
*/

//Z-up 右手版、skyboxテクスチャの上方向をZ+とする
void SHComboX( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);
	float3 n;
	float t = (id.x+0.5) / H * PI;
	float ct,st;
	sincos(t, st,ct);
	n.z = ct;
	SHCoeff C = (SHCoeff)0;

	//係数はPeter-Pike Sloan氏 "Stupid Spherical Harmonics (SH) Tricks"より引用
	for (int x=0; x<W; x++) {
		float p = (x+0.5) / W * 2*PI;
		float sp,cp;
		sincos(p, sp,cp);
		n.x = cp * st;
		n.y = sp * st;
		uint2 uv = {x,id.x};
		float4 s = sky(uv);
		C.c[0] += s * 0.282095;

		C.c[1] += s * -0.488603 * n.y;
		C.c[2] += s * 0.488603 * n.z;
		C.c[3] += s * -0.488603 * n.x;

		C.c[4] += s * 1.092548 * n.x * n.y;
		C.c[5] += s * -1.092548 * n.y * n.z;
		C.c[6] += s * 0.315392 * (3 * n.z * n.z - 1);
		C.c[7] += s * -1.092548 * n.x * n.z;
		C.c[8] += s * 0.546274 * (n.x * n.x - n.y * n.y);
	}

	for (int i=0; i<9; i++) {
		C.c[i] /= W;
	}
	SkyboxSHX[id.x] = C;
}


//SHの計算(縦方向:立体角で重みづけ平均)
[numthreads(1,1,1)]
void SHComboY( uint3 id:SV_DispatchThreadID )
{
	float W,H;
	Skybox.GetDimensions(W,H);

	SHCoeff C = (SHCoeff)0;
	for (int i=0; i<H; i++) {
		float s = sin((i+0.5) / H * PI);
		SHCoeff D = SkyboxSHX[i];
		for (int j=0; j<9; j++)
			C.c[j] += D.c[j] * s;	//sで重みづけするので平均で2/π倍されている
	}

	for (int i=0; i<9; i++)
		C.c[i] *= 2 * PI * PI / H;	//2*PI*PI * (2/PI) = 4*PI
	
	SkyboxSH[0] = C;
}