//random walk SSS用プローブシェーダ
//SSS()からTraceRayで起動されてdにレイがぶつかるまでの距離、hitにぶつかったインスタンスID、exitNにぶつかった面の法線を返す
struct PayloadSSS {int hit; float d; float3 exitN; };

[shader("closesthit")]
void ClosestHitSSS(inout PayloadSSS payload, Attribute attr)
{
    payload.hit = InstanceID();
    payload.d = RayTCurrent();
    Vertex v = GetVertex(PrimitiveIndex(), attr.uv);
    payload.exitN = v.normal;
}

[shader("miss")]
void MissSSS(inout PayloadSSS payload)
{
    payload.hit = -1;
    payload.d = RayTCurrent();
}

[shader("anyhit")]
void AnyHitSSS(inout PayloadSSS payload, Attribute attr)
{
	//subsurfaceマテリアル以外との当たり判定は無くしとく
	//そうしないと歯や歯茎や表情モーフオブジェクトが変な透け方をするので。
	//マテリアル番号が同一の物のみ探索するようにしてた事もあったが、これでは体のパーツが複数のマテリアルにまたがる場合に
	//パーツの間に切れ目が生じるのでダメだった
	if (materials[faces[PrimitiveIndex()]].subsurface == 0) {
		IgnoreHit();
	}
}

//参考資料というか移植元
//blender cycles
//https://github.com/blender/cycles/blob/main/src/kernel/integrator/subsurface_random_walk.h
// ↑の著作権表示
/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

//マテリアルのalbedo, sssRadius, sssAnisotropyからα(single sacttering albedo)と透過係数[1/m]を得る
void NumericalAlbedoInversion(Material m, inout float3 beta, out float3 alpha, out float3 sigmaT)
{
	float g = m.sssAnisotropy;
	float3 albedo = m.albedo;
	float g2 = g * g; float g3 = g2 * g; float g4 = g2 * g2; float g5 = g2 * g3; float g6 = g3 * g3; float g7 = g3 * g4;

	const float A = 1.8260523782f + -1.28451056436f * g + -1.79904629312f * g2 + 9.19393289202f * g3 + -22.8215585862f * g4 + 32.0234874259f * g5 + -23.6264803333f * g6 + 7.21067002658f * g7;
	const float B = 4.98511194385f + 0.127355959438f * exp(31.1491581433f * g + -201.847017512f * g2 + 841.576016723f * g3 - 2018.09288505f * g4 + 2731.71560286f * g5 + -1935.41424244f * g6 + 559.009054474f * g7);
	const float C = 1.09686102424f + -0.394704063468f * g + 1.05258115941f * g2 - 8.83963712726f * g3 + 28.8643230661f * g4 + -46.8802913581f * g5 + 38.5402837518f * g6 + -12.7181042538f * g7;
	const float D = 0.496310210422f + 0.360146581622f * g + -2.15139309747f * g2 + 17.8896899217f * g3 + -55.2984010333f * g4 + 82.065982243f * g5 - 58.5106008578f * g6 + 15.8478295021f * g7;
	const float E = 4.23190299701f + 0.00310603949088f * exp(76.7316253952f * g + -594.356773233f * g2 + 2448.8834203f * g3 - 5576.68528998f * g4 + 7116.60171912f * g5 + -4763.54467887f * g6 + 1303.5318055f * g7);
	const float F = 2.40602999408f + -2.51814844609f * g + 9.18494908356f * g2 - 79.2191708682f * g3 + 259.082868209f * g4 + -403.613804597f * g5 + 302.85712436f * g6 + -87.4370473567f * g7;

	const float3 blend = pow(albedo, 0.25f);

	alpha = (1 - blend) * A * pow(atan(B * albedo), C) + blend * D * pow(atan(E * albedo), F);
	alpha = clamp(alpha, 0.0, 0.999999);  // because of numerical precision

	float3 sigma_t_prime = 1 / max(m.sssRadius * m.subsurface, 1e-16f);
	sigmaT = sigma_t_prime / (1.0f - g);

	/* With low albedo values (like 0.025) we get diffusion_length 1.0 and
   	* infinite phase functions. To avoid a sharp discontinuity as we go from
   	* such values to 0.0, increase alpha and reduce the throughput to compensate. とのこと*/
	const float min_alpha = 0.2f;
	for (int i=0; i<3; i++) {
		if (alpha[i] < min_alpha) {
			beta[i] *= alpha[i] / min_alpha;
			alpha[i] = min_alpha;
		}
	}
}

float DiffusionLengthDwivedi(float alpha)
{
  return 1.0f / sqrt(1.0f - pow(alpha, 2.44294f - 0.0215813f * alpha + 0.578637f / alpha));
}

float SamplePhaseDwivedi(float v, float logPhase, float Xi)
{
	return v - (v+1) * exp(-Xi * logPhase);
}

float EvalPhaseDwivedi(float v, float logPhase, float cosT)
{
	return 1/((v-cosT)*logPhase);
}

//rdに対してcos(角度) = cosTとなるようなベクトルを得る
float3 DirectionFromCos(float3 rd, float cosT, float Xi)
{
	float sinT = sqrt(1-cosT*cosT);
	float phi = 2 * PI * Xi;
	float3 dir = {sinT * cos(phi), sinT * sin(phi), cosT};
	return mul(dir,ComputeTBN(rd));
}

//ステップ長sをサンプリングする時のpdf
float3 RandomWalkPDF(float3 sigmaT, float s, bool hit, out float3 transmittance)
{
	float3 T = exp(-sigmaT * s);
	transmittance = T;
	return hit ? T : sigmaT * T;
}

//表面化散乱インテグレータ
//返り値：有効なサンプリングが出来た場合はtrue, サンプリングが無効なのでLambertianなどで代用すべき時はfalse
//入力
//m  : マテリアル
//ro : パッチの座標
//rd : レイの進入方向
//N  : パッチの法線
//出力
// ro_next, rd_next : レイの出口と射出方向
// payload.betaを更新する
bool SSS(inout Payload payload, Material m, float3 ro, float3 rd, float3 N, out float3 ro_next, out float3 rd_next, out float3 exitN, out float pdf_exit)
{
	//変数の初期化
	exitN = N;
	rd_next = rd;
	ro_next = ro;
	float3 P = ro;	//レイが表面に侵入した地点
	pdf_exit = 0;
	//N = (dot(rd,N)<=0) ? N : -N;	//TODO:cyclesのソース読むとNに代入するのはNじゃなくてrdかもしれない

	//散乱係数を求めるなど
	//テスト用 skin1
	//sigmaS = float3(0.74e+3, 0.88e+3, 1.01e+3);
	//sigmaT = sigmaS + float3(0.032e+3, 0.17e+3, 0.48e+3 );
	float3 sigmaT,sigmaS, alpha;
	float3 beta = payload.beta;
	m.albedo *= float3(0.623,0.433,0.343);	//テスト用skin2

	//マテリアルのパラメータからαとσtを計算する。αが極端に低くなりそうな場合、スループットβも補正される
	NumericalAlbedoInversion(m, beta, alpha, sigmaT);

	sigmaT *= 0.08; //mmd単位に直す[1/m]→[1/MMD]
	sigmaS = sigmaT * alpha;

	float diffusionLength =  DiffusionLengthDwivedi(ReduceMax(alpha));
	if (diffusionLength == 1) {
		return false;
	}
	float logPhase = log((diffusionLength + 1) / (diffusionLength - 1));

	//その他変数
	bool haveOppositeInterface = false;	//反対側の面を持っている閉じた物体なのかペラペラなのか
	float oppositeDistance = 0;	//反対側の面までの距離
	float backFraction = 0;		//後方に伸ばす確率
	float pdf_f = 1, pdf_b = 1;	//pdfに対する係数
	float pdf_hg = 1;
	float anisotropy = m.sssAnisotropy;
	float cosT;		//最初に入ってきた面の法線に対するcos(角度)

	float guideFraction = 1- max(0.5, pow(anisotropy,0.125));	//ガイドさんを使う確率
	bool guided = false, guideBackward = false;

	//SUBSURFACE_RANDOM_WALK_SIMILARITY_LEVEL
	float3 sigmaS_star = sigmaS * (1-anisotropy);
	float3 sigmaT_star = sigmaT - sigmaS + sigmaS_star;
	float3 sigmaS_org = sigmaS;
	float3 sigmaT_org = sigmaT;
	float anisotropy_org = anisotropy;
	float guideFraction_org = guideFraction;

	//らんだむうぉーく
	for (int i=0; i<256; i++) {
		//0.バウンス回数が増えると等方性になるという調整
		//「back to isotropic heuristic from Blender」とのこと
		//ポリ割りが目立たなくなるなど落ち着いた見た目になるけど、ディテール感も失われる。一長一短。
		const int similarityLevel = 9;
		//バウンス回数が増えると等方的になる
		if (i <= similarityLevel) {
			anisotropy = guideFraction_org;
			guideFraction = guideFraction_org;
			sigmaT = sigmaT_org;
			sigmaS = sigmaS_org;
		} else {
			anisotropy = 0;
			guideFraction = 0.75;
			sigmaT = sigmaT_star;
			sigmaS = sigmaS_star;
		}

		//1.サイコロを振ってR,G,Bの中からheroを選択してください
		float4 Xi = RNG;
		float3 weights = beta*alpha;
		float tweight = dot(weights,1);
		if (tweight < 1e-10) {
			payload.beta = 0;  //全員吸い尽くされとる、干物に用は無いからから帰りなさい
			rd_next = SampleCosWeightedHemisphere(ComputeTBN(N), Xi.xy, pdf_exit);
			ro_next = P;
			return true;
		}
		float3 pdf_ch = weights / tweight;  //各chがheroになる確率
		int ch = 2;
		if (pdf_ch.r > Xi.x) 
			ch = 0;
		else if (pdf_ch.r+pdf_ch.g > Xi.x)
			ch = 1;

		//2.パスガイドさん
		if (i == 0) {
			//最初のステップはガイド無し
			float3x3 insideTBN = ComputeTBN(-N);
			float dmy;
			rd_next = SampleCosWeightedHemisphere(insideTBN, Xi.yz, dmy);
			cosT = dot(N,rd_next);
		} else {
			//ガイド
			float4 XiG = RNG;
			guided = XiG.x < guideFraction;
			if (guided) {
				if (haveOppositeInterface) {
					float x = clamp(abs(dot(ro-P, N)), 0, oppositeDistance);	//現在のレイの位置は物体の反対側までの距離に対してどの辺りか
					backFraction = 1 / (1+exp((oppositeDistance-2*x)/diffusionLength));
					guideBackward = XiG.y < backFraction;
				}
				cosT = SamplePhaseDwivedi(diffusionLength, logPhase, XiG.z);
				if (guideBackward)
					cosT = -cosT;
				float3 newD = DirectionFromCos(N, cosT, XiG.w);
				pdf_hg = HenyeyGreenstein(dot(newD,rd_next), anisotropy);
				rd_next = newD;
			} else {
				rd_next = SampleHG(ComputeTBN(rd_next), RNG.xy, anisotropy);
				cosT = dot(rd_next,N);
			}
		}

		//3.レイ進行
		pdf_f = EvalPhaseDwivedi(diffusionLength, logPhase, cosT) / pdf_hg / (2*PI);
		pdf_b = EvalPhaseDwivedi(diffusionLength, logPhase, -cosT) / pdf_hg / (2*PI);
		float kf = (1 - cosT/diffusionLength);	//前方ストレッチ係数
		float kb = (1 + cosT/diffusionLength);	//後方ストレッチ係数
		float3 sampleSigmaT = sigmaT;	//ステップ長さをサンプリングするための補正済みσt
		if (guided) {
			sampleSigmaT *= guideBackward ? kb : kf;
		}
		//ステップ長の決定
		float s = -log(1-Xi.w)/sampleSigmaT[ch];   //heroの移動距離(1-が付いてるのはlog(0)を踏まないようにするため)
		float probeDist = max(s, 10.0f / (ReduceMin(sigmaT)));	//平均自由行程の10倍までの間に面があれば「向こうの面あり」とする

		//sだけ進んでみる
		RayDesc sssDesc;
		sssDesc.Origin = ro;
		sssDesc.Direction = rd_next;
		sssDesc.TMin = max(s*0.01,1e-3);	//マージン。ちょっとデカい気がするけど小さすぎると誤差由来の変な模様が出る
		sssDesc.TMax = (i==0) ? probeDist : s;	//初手は厚さ計測を兼ねる
		PayloadSSS payss;
		TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 1,0,1, sssDesc, payss);
		s = min(payss.d,s);    //ミスった時のpayss.dはどうやらTMaxではなくBLASの中でキリの良い所までっぽい
		bool hit = payss.hit >= 0;
		if (hit) {
			if (i==0) {
				haveOppositeInterface = true;
				oppositeDistance = min(payss.d,probeDist) * abs(dot(rd_next,N));
				if (s < payss.d)
					hit = false;	//初手の場合は厚さ計測を兼ねているので、一発で貫通してない限りno hitとする
			}
		}
		ro += rd_next * s;

		//4.MIS
		float3 pdf,pdf_guided;
		float3 transmittance;
		float3 tr_dmy;
		
		pdf = RandomWalkPDF(sigmaT, s, hit, transmittance);

		if (i != 0) {
			pdf_guided = RandomWalkPDF(sigmaT * kf, s, hit, tr_dmy);
			if (haveOppositeInterface) {
				float3 pdf_back = RandomWalkPDF(sigmaT * kb, s, hit, tr_dmy);
				pdf_guided = lerp(pdf_guided*pdf_f, pdf_back * pdf_b, backFraction);
			} else {
				pdf_guided *= pdf_f;	//ペラ板の場合、前方散乱だけ促進する
			}
			pdf = lerp(pdf, pdf_guided, guideFraction);
		}

		//完成
		//pdfはσt * transmittanceなので単色の場合、平均自由行程分動くごとにβをα倍するだけ、という事になる。
		//ただし、hitしてる場合はα倍をしない
		beta *= hit ? transmittance / dot(pdf_ch, pdf) : sigmaS * transmittance / dot(pdf_ch, pdf);

		if (hit) {
			//レイが境界に達したら終了
			if (dot(payss.exitN, rd_next) < 0)
				payss.exitN = -payss.exitN;
			float3x3 exitTBN = ComputeTBN(payss.exitN);
			rd_next = SampleCosWeightedHemisphere(exitTBN, RNG.xy, pdf_exit);
			payload.beta = beta;
			ro_next = ro;
			exitN = payss.exitN;
			return true;
		}
		//ヒットしなかったらさらにらんだむうぉーく
	}
	//payload.beta *= float3(100,0,100);
	return false;
}
/*
void SSS(inout Payload payload, float3 ro, float3 rd, inout float3 rd_next, Vertex v, Material m)
{
	int Xidx = payload.bounce * 10000;	//サイコロ番号
	float4 Xi = HASH(Xidx++);
	float3x3 insideTBN = computeTBN(-v.normal);
	float pdf;
	rd_next = sampleCosWeightedHemisphere(insideTBN, Xi.xy, pdf);
	float3 sigmaS = m.sssSigmaS;
	float3 sigmaA = m.sssSigmaA;

	for (int i=0; i<32; i++) {
		//サイコロを振ってR,G,Bの中からheroを選択してください
		float4 Xi2 = HASH(Xidx++);
		//方針：スループットの高いheroを頻繁に選ぶ代わり、ちょっとずつ吸い取ろう
		float tbeta = dot(payload.beta,1);
		if (tbeta < 1e-10) {
			payload.Lt = 0;  //全員吸い尽くされとる、干物に用は無いからから帰りなさい
			return;
		}

		float3 Pch = payload.beta / tbeta;  //各chがheroになる確率
		int ch = 2;
		if (Pch.r > Xi2.y) 
			ch = 0;
		else if (Pch.r+Pch.g > Xi2.y)
			ch = 1;
		float s = -log(1-Xi2.x)/sigmaS[ch];     //heroの移動距離(1-が付いてるのはlog(0)を踏まないようにするため)

		RayDesc sssDesc;
		sssDesc.Origin = ro;
		sssDesc.Direction = rd_next;
		sssDesc.TMin = 1e-5;
		sssDesc.TMax = 1e-5 + s;
		PayloadSSS payss;
		TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 1,0,1, sssDesc, payss);
		s = min(payss.d,s+1e-5);    //ミスった時のpayss.dはどうやらTMaxではなくBLASの中でキリの良い所までっぽい

		//heroもそうじゃない奴もとりあえず吸われる運命です
		float3 pdf = sigmaS * exp(-s*sigmaS);   //各々がheroの真似をした場合のPDF
		float3 MISw = pdf/dot(pdf,1);           //バランスヒューリスティックによるMIS weight。heroの真似が上手い奴ほど高得点→heroっぽいのでちょっと吸われるだけで済む
		float3 weight = MISw / Pch[ch];   //吸収係数に対するウェイト
		payload.beta *= exp(-s*sigmaA*weight);  //吸収(weightが高い→heroっぽくないけど迷い込んできたショタなので一杯吸われる)

		ro += rd_next * s;
		float4 Xi3 = HASH(Xidx++);
		if (payss.hit == InstanceID()) {
			//レイが境界に達した
			if (dot(payss.exitN, rd_next) < 0)
				payss.exitN = -payss.exitN;
			float3x3 exitTBN = computeTBN(payss.exitN);
			float pdf;
			rd_next = sampleCosWeightedHemisphere(exitTBN, Xi3.xy, pdf);
		} else {
			//さらにらんだむうぉーく
			float3x3 hgTBN = computeTBN(rd_next);
			rd_next = sampleHG(hgTBN,Xi3.xy, m.sssAnisotropy);
			//ロシアンルーレット…中に入り込んで迷った奴はどうせ出られない。出てきた奴に褒美をやる代わり、迷い込んだ奴の事はさっさと忘れる
			payload.beta /= 0.9;
			if (Xi3.z < 0.1) {  
				payload.Lt = 0;
				return;
			}
		}
	}
}
*/
