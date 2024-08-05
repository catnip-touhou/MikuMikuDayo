
//参考文献

//Alan King氏, Christopher Kulla氏, Alejandro Conty氏, Marcos Fajardo氏 "BSSRDF Importance Sampling"
//http://library.imageworks.com/pdfs/imageworks-library-BSSRDF-sampling.pdf

//A.Wilkie氏, S.Nawaz氏, M.Droske氏, A.Weidlich氏, J.Hanika氏 "Hero Wavelength Spectral Sampling"
//Eurographics Symposium on Rendering 2014

//yumcyawiz氏 "色付きボリュームのモンテカルロレイトレーシング"
//https://blog.teastat.uk/post/2021/12/montecarlo-raytracing-of-colored-volume/

//Jiayin Cao氏 "Practical Tips for Implementing Subsurface Scattering in a Ray Tracer"
//https://agraphicsguynotes.com/posts/practical_tips_for_implementing_subsurface_scattering_in_a_ray_tracer/


//ChristensenBurley SSS用プローブシェーダ
//SSS()からTraceRayで起動されてdにレイがぶつかるまでの距離、hitにぶつかったインスタンスID、exitNにぶつかった面の法線を返す
struct PayloadSSS {
	uint xi;

	//呼び出し元でセットする情報
	uint ID;		//呼び出し元の物体のID
	float3x3 TBN;	//呼び出し元の接空間
	float3 C;		//呼び出し元のパッチの位置
	float3 S;		//SSSSCaleで計算したShape Parameter

	//交点について得られた情報
	float d;		//ヒットした時のRayTCurrent()
	float2 uv;		//ヒットした場所の重心座標
	uint iTri;		//ヒットした三角形番号

	//交点を列挙・選択するための情報
	float pick;		//パッチを選択するための乱数(マイナスの場合は交点列挙モード)
	float weight;	//最後に探索したパッチのウェイト
	float currentWeightSum;	//現在までに列挙されたパッチの合計ウェイト
	float totalWeight;		//交差した点の総ウェイト
	int currentHit;
	int totalHit;
};





//Reflectance Profile
float3 CalcRr(float3 r, float3 s)
{
	return (exp(-s*r) + exp(-s*r/3)) * s / (8*PI*r);
}

//rについての確率密度(点についての確率密度を出す場合は1/rするのでCalcRrと同じになる)
float3 SSSPDF(float3 r, float3 s)
{
	return (exp(-s*r) + exp(-s*r/3)) * s / (8*PI);
}

// CDF(sr) - ξ
float CDF_Xi(float x, float Xi)
{
	return (1.0 - 0.25*exp(-x) - 0.75*exp(-x/3.0) - Xi);
}

// CDF'
float D_CDF(float x)
{
	return 0.25*exp(-x) + 0.25*exp(-x/3);
}

//CDF''
float D2_CDF(float x)
{
	return -0.25*exp(-x) - 1.0/12.0*exp(-x/3);
}


// 0～1の値を取る一様乱数ξを入れるとsrを返す関数
// Halley's Methodを使って CDF(sr)-ξ = 0 となるsrを探す
float CDF_inv(float Xi)
{
	float x = 0;
	// 繰り返し回数はてきとうです。大概は3回もやれば十分らしいよ
	for (int i=0; i<32; i++) {
		float dx = (2*CDF_Xi(x,Xi)*D_CDF(x)) / (2*sqr(D_CDF(x)) - CDF_Xi(x,Xi)*D2_CDF(x));
		x -= dx;
		if (abs(dx) < 1e-6)
			break;
	}
	return x;
}


float SSSSampleSR(float Xi)
{   
	//こっちのが正確ではあるけど値の範囲が0～∞で扱いづらい
	//平均2.5で以下のような傾向がある
	//99.9%のサンプルは21程度、99%は13程度、95%は8程度、90%のサンプルは6程度の範囲に収まる
	return CDF_inv(Xi);

	//前もって32回振った値
	/*
    const float data[32] = {
        0.11993846361301534,0.1291282113291807,0.2155355417166984,0.40405816992549864,
        0.6995739226790069,0.7826023588021281,0.7889911536130073,0.8130457604613858,
        0.9547979437298963,1.0317132711702437,1.0568240335914714,1.1332501547866496,
        1.4126759113451688,1.5323433628175582,1.5489491261004653,1.6734718155906716,
        1.8203597285432191,2.064114284962304,2.096508756762976,2.28690536001651,
        2.64010338139885,2.675754946942186,2.7562846245444343,3.6335930548248747,
        3.8328491713577524,3.8722550751295306,4.1646222848428,5.445946634522373,
        5.518112196058849,5.661615150883857,5.757717003156458,6.508553664806133
    };
    return data[floor(Xi*32)];
	*/
}



//Albedoからサンプリング位置についてのスケールを計算する
float3 SSSScale(float3 A, float3 MFPmm)
{
    //媒質の平均自由行程[mm]
	float3 MFPinMMD = MFPmm / 80;	//MMD単位に直す
	return (1.9 - A + 3.5*(A-0.8)*(A-0.8)) / MFPinMMD;
}

//半径rの円上の1点をサンプリング(pdfは1/(2πr) 単位は[1/MMD長さ])
float3 SampleCircle(float3x3 TBN, float Xi, float r)
{
    float2 d = CosSin(Xi.x * 2 * PI) * r;
    return TBN[0]*d.x + TBN[1]*d.y;
}

//サンプルのウェイト計算 単位は[1/MMD^2]
//C:パッチの位置, V:プローブレイ PHit:ヒットした点, NHit:ヒットした点の法線
float SSSWeight(float3 C, float3x3 TBN, float3 PHit, float3 NHit, float3 S)
{
	//接空間でのパッチ→交点 & 法線
	float3 dLocal = mul(TBN, C - PHit);
	float3 nLocal = mul(TBN, NHit);

	//T,B,N各軸でディスクを作ったとして、そのディスク上に投影したパッチ→交点の長さ
	float3 rProj = float3(length(dLocal.yz), length(dLocal.xz), length(dLocal.xy));
	
	float pdf = 0;
	float3 axisProb = {0.25, 0.25, 0.5};	//各軸が選択される確率
	float chProb = 1/3.0;	//各色が選択される確率
	
	//各色、各軸で 今注目している交点PHit をサンプリングする確率密度を全部足す
	for (int i=0; i<3; i++) {
		float3 rpdf = CalcRr(max(rProj[i],1e-3), S);
		float nokori = abs(nLocal[i]) * chProb * axisProb[i];
		for (int j=0; j<3; j++) {
			//rpdf[j] : 色jの波長の時、軸iで作ったディスク上で交点をサンプリングする確率密度
			pdf += rpdf[j] * nokori;
			//注)nLocal[i] = TBN[i]・NHitの事
			//これは、「ディスクに投影された交点パッチの面積」 / 「投影前の交点パッチの面積」
			//確率密度は 元のパッチの面積あたりの確率
		}
	}

	return pdf;
}


/************ プローブレイシェーダ *************/

[shader("closesthit")]
void ClosestHitSSS(inout PayloadSSS payload, Attribute attr)
{
}

[shader("miss")]
void MissSSS(inout PayloadSSS payload)
{
}


[shader("anyhit")]
void AnyHitSSS(inout PayloadSSS payload, Attribute attr)
{
	//自モデル以外との当たり判定は無し
	if (InstanceID() != payload.ID) {
		IgnoreHit();
		return;
	}

	//subsurfaceマテリアル以外との当たり判定は無くしとく
	//そうしないと歯や歯茎や表情モーフオブジェクトが変な透け方をするので。
	//マテリアル番号が同一の物のみ探索するようにしてた事もあったが、これでは体のパーツが「肩」「腕」など複数の材質にまたがる場合に
	//パーツの間に切れ目が生じるのでダメだった
	if (materials[faces[PrimitiveIndex()].iMaterial].category != CAT_SUBSURFACE) {
		IgnoreHit();
		return;
	}

	//マテリアル情報ゲットしてαテスト
	Vertex v = GetVertex(PrimitiveIndex(), attr.uv);
	Material m = GetMaterial(PrimitiveIndex(), v.uv);
	if (RNG.x > m.alpha && m.alpha < 0.98) {
		IgnoreHit();
		return;
	}

	/* 現在の交点のウェイト(Rr / ディスク上にその点1つしか無かった場合のpdf)を計算 */
	payload.iTri = PrimitiveIndex();
	payload.uv = attr.uv;
	payload.d = RayTCurrent();
	
	float3 PHit = WorldRayOrigin() + normalize(WorldRayDirection()) * payload.d;
	float3 NHit = v.normal;
	float3 NHitg = GeometryNormal(payload.iTri);

	float pdf = SSSWeight(payload.C, payload.TBN, PHit, NHit, payload.S);
	float Rr = dot(CalcRr(max(length(payload.C-PHit),1e-3), payload.S), 1);	//サンプルされる確率を概算するための物なので、とりあえずRGBの合計にしとく
	payload.weight = pdf>0 ? Rr/pdf : 0;
	payload.currentWeightSum += payload.weight;
	payload.currentHit++;

	if (payload.pick >= 0) {
		//交点選択モード

		//等確率で選択
		/*
		if (payload.pick < (float)payload.currentHit/(float)payload.totalHit) {
			AcceptHitAndEndSearch();
			return;
		}
		*/
		if (payload.pick < payload.currentWeightSum/payload.totalWeight) {
			//レイと複数交点がある時はウェイトに比例した確率に従って選択
			AcceptHitAndEndSearch();
			return;
		}
	}

	//レイと交差するポリゴンはとりあえず全部列挙するので基本的にはIgnoreHitを呼ぶ
	IgnoreHit();
}


/************ 本体 *************/

//表面化散乱さんぷら
//返り値：有効なサンプリングが出来た場合はtrue, サンプリングが無効なのでLambertianなどで代用すべき時はfalse
//入力
//m  : マテリアル
//ro : パッチの座標
//rd : レイの進入方向
//N  : パッチの法線
//ch : RGBチャンネルのどれを選択しているか？
//出力
// ro_next, rd_next : レイの出口と射出方向
// payload.xiを更新する
bool SSS(inout Payload payload, Material m, float3 ro, float3 rd, float3 N, float3 Ng, int ch, out float3 ro_next, out float3 rd_next, out float3 exitN, out float3 exitNg, out float pdf_exit, out float3 beta)
{
	//変数の初期化
	rd_next = rd;
	ro_next = ro;
	float3 P = ro;	//レイが表面に侵入した地点
	pdf_exit = 0;
	exitN = N;
	exitNg = Ng;
	beta = m.albedo;

	//サンプリング

	//ヒーロー波長をchで選択して、ヒーロー波長でのサンプリングディスクをセットアップする
	//バウンスのたびにchが変わると色がおかしくなるので1本のパスでchを変えない事が大事
    //float3 mfp = {0.909, 0.602, 0.515};		//skin2の平均自由行程[mm]
	float3 mfp = m.cat.xyz * m.cat.w * 100;
    float3 S = SSSScale(m.albedo, mfp);		//各波長毎の反射率でのshape parameter(ヒーロー波長以外の要素は後で使う)
    float r = SSSSampleSR(RNG.x) / S[ch];	//S*rをサンプリングしてSで割ってディスクの中心からの距離rを出す
	float rmax = 20 / S[ch];				//20はSampleSRの大体の最大値(広くしすぎるとポリ割りが目立つ)
	float h = rmax*rmax - r*r;				//rmaxを斜辺、rを底辺とする直角三角形の高さ=ディスクをパッチからどれだけ離すか？
	if (h < 0)
		return false;	//極端な外れ値は取り扱わない(cyclesの実装ではlambertian扱いではなく黒いピクセルにして、打切り誤差分をSSS分に追加しているらしい)
		
	h = sqrt(h);

	//単純にT,B,Nのどれかでディスクサンプリングという方式にすると時々ポリ割りが目立つので
	//Nを軸にした半球上をcos-weightedでサンプリングした方向をN'として、そこを基準にT',B',N'を作る
	//ポリ割りが見えなくなるというわけではないが目立たなくはなる
	bool anyhit = false;
	float dmy;
	float3 Ndash;
	float3x3 TBN;
	float3x3 TBN_orig;	//あとでpdfを計算するために保持しとく
	float xiAxis;
	PayloadSSS payss = (PayloadSSS)0;
	float3 V;
	RayDesc ray;
	//何回かレイを撃ってサンプリングの成功を促したけどいまいち見た目が変わらないので1回にした
	for (int i=0; i<1; i++) {
		Ndash = SampleCosWeightedHemisphere(ComputeTBN(N), RNG.xy, dmy);
		TBN = ComputeTBN(Ndash);
		TBN_orig = TBN;	//あとでpdfを計算するために保持しとく
		xiAxis = RNG.x;
		//T'B'N'のうち1つを実際にディスクを作る軸とする
		//50%の確率でN'をそのまま使うが25%ずつの確率でT',B'を採用
		//軸を入れ替えてTBN''を作り、V = N''を軸とする事で実装している
		if (0.5 <= xiAxis && xiAxis < 0.75) {
			TBN = float3x3(TBN[2], TBN[1], TBN[0]);
		} else if (0.75 <= xiAxis) {
			TBN = float3x3(TBN[0], TBN[2], TBN[1]);
		}
		V = TBN[2];

		//ディスクからレイを発射して交点を得る
		ray.Origin = P + V * h + SampleCircle(TBN, RNG.x, r);
		ray.Direction = -V;
		ray.TMin = 0;
		ray.TMax = 2*h;

		//まず、ディスクからレイを撃って交差する点を列挙する
		payss.ID = payload.ID;
		payss.TBN = TBN_orig;
		payss.C = P;
		payss.S = S;
		payss.pick = -1;
		TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 1,0,1, ray, payss);
		payss.totalWeight = payss.currentWeightSum;
		payss.totalHit = payss.currentHit;

		//何にも当たらなかった or 何らかの理由で無効な交点しか得られなかったら失敗
		if (payss.totalHit != 0) {
			anyhit = true;
			break;
		}
	}
	if (!anyhit)
		return false;
	
	//1つ以上当たってたらそのうち1つをウェイトで按分した確率で選択
	payss.currentHit = 0;
	payss.currentWeightSum = 0;
	payss.pick = RNG.x;
	TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 1,0,1, ray, payss);

	//挙がってきた交点についての情報をまとめる
	Vertex v = GetVertex(payss.iTri, payss.uv);
	Material mhit = GetMaterial(payss.iTri, v.uv);
	float3 Phit = ray.Origin - V * payss.d;
	float3 Nhit = v.normal;

	/* いろいろおかしかった素案
	float pj; 		//各ch,axisのpdf
	float tpj = 0;	//全pdf合計
	float3 pC = 0;	//各chのpdf合計
	float3 pdisk = SSSPDF(length(P-Phit), S);	//この距離になるような点がサンプリングされる確率密度(寄与と比例)
	for (int i=0; i<3; i++) {
		for (int j=0; j<3; j++) {
			float paxis = (j==0) ? 0.5 :0.25;	//軸選択のpdf
			float3 axis = TBN_orig[j];
			pj = pdisk[i] * paxis * max((dot(axis,Nhit)),1e-10);
			tpj += pj;
			pC[i] += pj;
		}
	}
	beta = pC / tpj * 3 * sqrt(mhit.albedo*m.albedo);	//3は 1/(色選択のpdf)
	*/

	//float pdf = SSSWeight(payss.C, payss.TBN, Phit, Nhit, payss.S);// / payss.totalHit;

	float pdf = SSSWeight(P, TBN_orig, Phit, Nhit, S) *  payss.weight / payss.totalWeight;
	beta = CalcRr(max(length(P-Phit),1e-3), S) / pdf * sqrt(mhit.albedo*m.albedo);
	
	//Rr/pdfは結局totalWeightを色付きに戻したのとほぼ同じ結果になる
	//つまり、全交点からの寄与を取ろうとした状態になる
	//交点から先からの照明を全部得られるわけではないので、照明を得る点は1箇所分で代用する

	//出口についての情報を書きこんで終わり
	ro_next = Phit;
	exitN = Nhit;
	exitNg = GeometryNormal(payss.iTri);
	rd_next = SampleCosWeightedHemisphere(ComputeTBN(Nhit), RNG.xy, pdf_exit);

	return true;
}
