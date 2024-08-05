#include "yrz.hlsli"
#include "PMXDefs.hlsli"

#include "ChristensenBurleySSS.hlsl"

//界面のIORを得る
float MaterialIOR(Material m, int waveCh, float etaI)
{
	float mior;

	//ガラスの中からガラスにぶつかってる場合、空気の層に出るとみなす
	//レイの起点がガラスの中であるかどうかの判定はetaIが1かそれ以外かで判定する
	if (etaI!=1 && m.category==CAT_GLASS)
		return 1;

	if (m.IOR.y)
		mior = Cauchy(m.IOR,WaveChannelToLambda(waveCh));
	else
		mior = m.IOR.x;

	return mior;
}

//位置PにおけるBRDFサンプリング
//Vは入射方向(=-rd)
//返り値は更新後のmの発光要素
//PはBSSRDFの場合変更される可能性あり
//wo サンプリングした出射方向
//IOR : レイの出発点の屈折率ηi
//返り値 パッチが発光体だった場合はその輝度
float3 SampleBRDF(inout Payload payload, Material m, float3 P, float3x3 TBN, float3 Ng, float3 V, float etaI, int waveCh, int sssCh, inout bool SSSpassed, out BRDFOut O, uniform bool specularMS = true)
{
	O = (BRDFOut)0;
	O.beta = 1;
	O.IOR = etaI;
	float3 Lt = 0;

	O.dispersion = (m.IOR.y != 0);
	float lambda = WaveChannelToLambda(waveCh);
	float etaO = MaterialIOR(m, waveCh, etaI);

	//まず、マイクロ法線をサンプリングして、反射先と屈折先を求めてしまう
	float2 a = sqr(m.roughness);
	float3 H = VNDF(V, TBN, RNG.xy, a);
	float3 rl = reflect(-V, H);	//反射先
	
	//屈折率比(etaI/etaO), ガラスや水同士が貫通・接触している状態は無いとする
	//↑の前提により、ガラスの中でガラスにぶつかった時は空気に出ていると判定する
	//ηiが1ならηoはマテリアルの屈折率になり、ηiが1以外かつ物体がガラスならηo=1(空気)という事になる、どちらでもない場合は屈折は考慮しなくてよい
	//上記をMaterialIOR関数で計算している
	float etaIO = etaI/etaO;
	float3 rr = refract(-V, H, etaIO);	//屈折先

	float u = FresnelSchlick(abs(dot(V, H)));	//なんでかノーマルマップするとV・Hが1を越えるケースがあるっぽい、一応対策してみた
	//ガラスの中で他の物体に当たった場合
	float F_nonmetal = lerp(IORtoF0(etaO/etaI), 1, u);	//2024-0407修正
	float3 F_metal = lerp(m.albedo, 1, u);

	//発光体に当たった
	Lt +=  m.emission;

	//表面下散乱を起こす物体の場合、実際にそのパスを通るかどうかを問わずBSSRDFの評価をする(NEEのため)
	//但し、以前にSSSパスを通ったことが有る場合は強制的にLambertとする(firefly対策として非常に有効で、しかも見た目はほぼ変わらない)
	if ( (m.category==CAT_SUBSURFACE) && (!SSSpassed)) {
		O.BSSRDF = SSS(payload, m, P, -V, TBN[2], Ng, sssCh, O.POut, O.L, O.NOut, O.NgOut, O.pdf, O.betaSSS);
	}

	if ((F_nonmetal > RNG.x) || (m.category==CAT_METAL) || (m.category==CAT_GLASS && all(rr==0)) ) {
		//鏡面反射
		float3 F0ms = m.category==CAT_METAL ? m.albedo : 1;	//MS項の計算のためのF0
		float weight = GGXAnisoBRDFdivPDF(a, V, H, TBN);
		O.isMirror = any(m.roughness < 1e-3);
		O.pdf = O.isMirror ? 1e+10 : VNDFPDF(a, V, H, TBN);	//完全鏡面反射の場合はとりあえずてきとうに入れとく
		O.beta *= (m.category==CAT_METAL ? F_metal : 1) * weight;	//フレネル項とMS項
		[branch]
		if (specularMS)
			O.beta *= GGXMultiScatteringTerm(a, F0ms, V, H, TBN);
		O.L = rl;
		if (dot(rl,TBN[2])<=0)	//この処理は妥当なんだろうか？
			O.beta = 0;
	} else if (m.category == CAT_GLASS) {
		//屈折
		//O.glassSigmaA = 1/max(m.albedo,1e-10);	//吸収係数の計算
		float weight = GGXAnisoBRDFdivPDF(a, V, H, TBN, true);
		O.isMirror = any(m.roughness < 1e-3);
		//ガラス内に他の透過性物体が入っている事を考慮せず、ガラス内で何かの表面で透過を起こしたらガラスの外に出たと見なす
		O.IOR = etaO;
		//TODO:屈折の場合のPDFを直す...HではなくO.Lについてのpdfなので反射と屈折では変えないといけないはず？
		O.pdf = O.isMirror ? 1e+10 : VNDFPDF_T(a, V, H, TBN, etaI,etaO);	//完全鏡面反射の場合はとりあえずてきとうに入れとく
		O.beta *= weight;
		[branch]
		if (specularMS)
			O.beta *= GGXMultiScatteringTerm(a, 1, V, H, TBN);
		O.L = rr;
	} else {
		O.diffuse = true;
		//拡散反射
		//SSSのサンプリングに失敗した場合もLambertianとして扱う
		if (O.BSSRDF) {
			O.SSS = true;
			O.beta *= O.betaSSS;	//PからPOutに移動する間に吸収される分
			//出口でのフレネル透過率(入口側については確率的に拡散反射パスを選択される所で解決済み)
			O.beta *= 1 - lerp(IORtoF0(etaO/etaI),1,FresnelSchlick(abs(dot(O.L,O.NOut))));
			SSSpassed = true;
		} else {
			O.L = SampleCosWeightedHemisphere(TBN, RNG.xy, O.pdf);
			O.beta *= m.albedo;
		}
	}

	return Lt;
}


//Vから入ってL方向に散乱するPDF p(L)、測度は立体角
//etaM : 材質のIOR
//etaV, etaL : 反射・屈折前のIOR, 反射・屈折後のIOR
//sssIn,sssOut:表面下散乱の入り口(鏡面反射分のみ)・出口パッチ(フレネル透過分のみ)かどうか
float BxDFPDF(int waveCh, Material m, float3x3 TBN, float3 V, float etaV, float3 L, float etaL, bool sssIn = false, bool sssOut = false)
{
	//入射方向に法線を向ける
	if (dot(V,TBN[2]) < 0)
		TBN = -TBN;

    float3 N = TBN[2];
	float NoL = dot(N,L);
    float3 H = normalize(L+V);
	float2 a = sqr(m.roughness);
	float vndfPDF = VNDFPDF(a, V, H, TBN);

    //透過あり・拡散反射無しの物体の場合
    if (m.category == CAT_GLASS) {
        if (NoL > 0) {
            //反射の場合
            return vndfPDF;
        } else {
            //屈折の場合
            return VNDFPDF_T(a, V, L, TBN, etaV, etaL);
        }
    }

	//透過なしの物体の場合、裏面に行く事は無い
	if (NoL <= 0)
		return 0;

	//鏡面反射のみの物体の場合
	if (m.category==CAT_METAL || sssIn)
		return vndfPDF;

    //鏡面反射と拡散反射両方するけど透過しない物体の場合
    float u = FresnelSchlick(abs(dot(V, H)));
    float etaM = MaterialIOR(m, waveCh, etaV);
    float kSpec = lerp(IORtoF0(etaM/etaV), 1, u);

	if (sssOut)
		kSpec = 0;

    float pdf_spec = vndfPDF;
    float pdf_diffuse = NoL / PI;
    return lerp(pdf_diffuse, pdf_spec, kSpec);

    //TODO : BSSRDF … Lambertian pdfで出るので1点に注目する限りPDF自体は変わらないと思う

}


//ほぼBSSRDF用
float LambertianPDF(float3 N, float3 L)
{
	return saturate(dot(N,L))/PI;
}


