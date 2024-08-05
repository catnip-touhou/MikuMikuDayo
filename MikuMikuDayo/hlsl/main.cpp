#include "YRZ.h"
#include "PMXLoader.h"
#include <oidn.hpp>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "OpenImageDenoise.lib")
#pragma warning(disable: 4267 4244 4305)

using namespace std;
using namespace YRZ;
using namespace YRZ::PM::Math;

//グーロバル変数・定数
int iTick = 390;	//純粋なフレーム番号
int iFrame = 0;	//操作されるとリセットされるフレーム番号
bool animation = false;
bool denoise = false;
#define SKYBOX LR"(D:\WORK\MMD\MINE\SDPBR\SKYBOXHDR\texture\kiara_8_sunset_4k_lum1man.dds)"


enum MaterialCategory { mcSlab = 0, mcGlass = 1, mcCellophan = 2 };

//材質情報
struct Material {
	XMFLOAT3 albedo;
	XMFLOAT3 emission;
	float alpha;
	float metallic;
	XMFLOAT2 roughness;
	float IOR;
	XMFLOAT2 dispersion;
	float subsurface;
	float sssAnisotropy;
	XMFLOAT3 sssRadius;
	int texture;
	int twosided;	//両面描画フラグ
	int category;
	float autoNormal;
};

//VertexBufferの中身
struct Vertex {
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
};

//変形情報。頂点毎に1つ付く。
struct Skinning {
	int iBone[4];		//ボーン番号
	float weight[4];	//各ボーンのウェイト(SDEFの場合はweight[1-3]に(sdef_r0-sdef_r1)/2.xyzを入れる)
	int weightType;		//変形方式(0-2:BDEF1,2,4 / 3:SDEF)
	XMFLOAT3 sdef_c;	//SDEFパラメータ
};

//頂点UVモーフ情報
struct MorphItem {
	int iMorph = 0;			//対応するモーフ番号
	XMFLOAT3 dPosition = XMFLOAT3(0,0,0);	//モーフ値1につき頂点座標はどれだけ動くか
	XMFLOAT4 dUV = XMFLOAT4(0,0,0,0);		//モーフ値1につきUV座標はどれだけ動くか
};

//各頂点に割り当てられる、頂点モーフテーブルの参照情報
struct MorphPointer {
	int where;	//テーブルのどこから自分に割り当てられたモーフ情報が始まるか？ -1で割り当て無し
	int count;	//いくつアイテムがあるか
};

//ライトポリゴン情報
struct Light {
	XMFLOAT3 Le;	//輝度
	int iFace;		//面番号
	float pdf;		//このライトの放射束/全ライトの放射束
	float cdf;		//0番から↑を累積していったもの
};

//OIDNへの入力
struct OIDNInput {
	XMFLOAT3 color;
	XMFLOAT3 albedo;
	XMFLOAT3 normal;
};



//定数バッファ。float4を跨ぐような変数を置かないようにすべし
struct Constantan {
	UINT iFrame;			//フレーム番号0スタート
	UINT iTick;
	XMFLOAT2 resolution;	//解像度
	XMFLOAT3 cameraRight;	//ワールド座標でのカメラの右方向
	float fov;				//cot(垂直画角/2)
	XMFLOAT3 cameraUp;
	float skyboxPhi;		//skyboxの回転
	XMFLOAT3 cameraForward;
	float pad2;
	XMFLOAT3 cameraPosition;
	int nLights;			//ライトポリゴンの数
};


XMFLOAT3 ToLinear(XMFLOAT3 c)
{
	XMFLOAT3 r;
	float* d = &r.x;
	float* s = &c.x;
	for (int i = 0; i < 3; i++) {
		float gamma = *s;
		*d = (gamma <= 0.04045) ? gamma / 12.92 : pow((gamma + 0.055) / 1.055, 2.4);
		d++;
		s++;
	}
	return r;
}

//nameの中にstrsの中の文字列を1つでも含むかテスト
bool MatchName(const wstring& name, const vector<wstring>& strs)
{
	for (int i = 0; i < strs.size(); i++)
		if (name.find(strs[i]) != std::wstring::npos)
			return true;

	return false;
}


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	const wchar_t* srcpath = L"D:\\documents\\Visual Studio 2022\\YorozuDXR\\YorozuDXR";
	//vector<const wchar_t*> CompileOption = { L"-I", srcpath, L"-HV", L"2021" };
	vector<const wchar_t*> CompileOption = { L"-I", srcpath };

	//ウィンドウの幅と高さ
	const int W = 1600, H = 960;

	//カメラなどの変数
	float CameraDistance = 33;// 150
	float CameraPhi = 0;
	float CameraTheta = PI * 0.45; //PI * 0.5;
	float CameraDX = 0, CameraDY = 10;//25;
	XMFLOAT3 CameraTarget = { 0,10,0 };	//カメラの注視点
	float skyboxPhi = 0;	//skyboxの回転角

	//1. よろずDXRオブジェクトの作成
	YorozuDXR* dxr = new YorozuDXR(L"MikuMikuDayo", W, H);

	//ホットキーの設定(なんかPrintScreenキーが押されたことを知るためのメッセージが無いので)
	RegisterHotKey(dxr->hWnd(), 0, 0, VK_SNAPSHOT);

	//2. モデルを読み込む
	using namespace PMX;
	wstring modelpath = L"model\\dayo\\";
	wstring modelname = L"dayo_gold.pmx";
	PMXModel* pmx = new PMXModel((modelpath + modelname).c_str());

	//頂点データ
	Vertex* vs = new Vertex[pmx->m_vcount];
	for (int i = 0; i < pmx->m_vcount; i++) {
		vs[i].position = pmx->m_vs[i].position;
		vs[i].normal = pmx->m_vs[i].normal;
		vs[i].uv = pmx->m_vs[i].uv;
	}
	Buf vb = dxr->CreateBuf(vs, sizeof(Vertex), pmx->m_vcount, L"VertexBuffer");

	//インデクスデータからIBを直接作る
	Buf ib = dxr->CreateBuf(pmx->m_is, sizeof(UINT), pmx->m_icount, L"IndexBuffer");

	//マテリアルデータ
	Material* ms = new Material[pmx->m_mcount];
	for (int i = 0; i < pmx->m_mcount; i++) {
		PMXMaterial m = pmx->m_ms[i];
		ms[i] = {};
		ms[i].albedo = ToLinear(xyz(m.diffuse));
		ms[i].alpha = m.diffuse.w;
		ms[i].metallic = 0;
		ms[i].roughness = { 0.5, 0.5 };
		ms[i].IOR = 1.5f;
		ms[i].texture = m.tex;
		ms[i].twosided = m.drawFlag & 1;
		ms[i].subsurface = 0;
		ms[i].autoNormal = 0;
		if (MatchName(m.name, { L"頭", L"首", L"体", L"かお", L"肌", L"顔", L"手", L"足", L"腕", L"ボディ", L"乳", L"耳", L"唇" })) { ms[i].subsurface = 0.00; }
		if (MatchName(m.name, { L"飾り", L"袋", L"袖", L"服", L"ふく",L"内",L"裏",L"線",L"エフェ",L"光" })) { ms[i].subsurface = 0; }
		if (MatchName(m.name, { L"髪" })) { ms[i].autoNormal = 1; ms[i].roughness = { 0.5,0.1 }; }

		if (ms[i].subsurface) {
			ms[i].sssAnisotropy = 0.8;
			ms[i].sssRadius = { 1,0.2,0.1 }; //{ 0.482,0.169,0.109 }; //{0.909, 0.602, 0.515}; //
		}

		if (MatchName(m.name, { L"白目" })) { ms[i].emission = XMFLOAT3(0.18, 0.18, 0.18) * 1; }
		if (MatchName(m.name, { L"金", L"鉄", L"刀" })) { ms[i].metallic = 1.0; ms[i].roughness = vec2(0); ms[i].subsurface = 0; }
		if (MatchName(m.name, { L"笠" })) { ms[i].emission = ms[i].albedo * 5; }
		if (MatchName(m.name, { L"灯" })) { ms[i].emission = ms[i].albedo * 100; }
		if (MatchName(m.name, { L"平面灯" })) { ms[i].emission = ms[i].albedo * 1000; }
		if (MatchName(m.name, { L"発光" })) { ms[i].emission = ms[i].albedo * 10; }
		if (MatchName(m.name, { L"ガラス",L"氷",L"翼", L"キューブ", L"金"})) {
			ms[i].category = mcGlass; ms[i].roughness = vec2(0); ms[i].metallic = 0; ms[i].subsurface = 0; ms[i].sssRadius = { 1,1,1 };	 ms[i].IOR = 1.7;
			//ms[i].subsurface = 1;
			//ms[i].sssRadius = { 1, 0.2, 0.1 }; //{ 0.482,0.169,0.109 };
			//ms[i].dispersion = { 2.58, 31827};
		}

		//if (MatchName(m.name, { L"リング", L"リボン" })) { ms[i].emission = ms[i].albedo * XMFLOAT3(5, 5, 5); }

	}
	Buf mb = dxr->CreateBuf(ms, sizeof(Material), pmx->m_mcount, L"MaterialBuffer");

	//面番号→マテリアル番号バッファ
	UINT faceCount = pmx->m_icount / 3;
	UINT* fs = new UINT[faceCount];
	UINT faceIndex = 0;
	for (int i = 0; i < pmx->m_mcount; i++) {
		for (int j = 0; j < pmx->m_ms[i].vertexCount / 3; j++) {
			fs[faceIndex] = i;
			faceIndex++;
		}
	}
	Buf fb = dxr->CreateBuf(fs, sizeof(UINT), faceCount, L"FaceBuffer");

	//ライトポリゴンバッファ
	vector<Light> lights;
	faceIndex = 0;
	float totalLightW = 0;
	for (int i = 0; i < pmx->m_mcount; i++) {
		if (Dot(ms[i].emission, vec3(1.0)) > 0.01) {	//輝度0.01より大きい発光体はNEEの対象にする
			for (int j = 0; j < pmx->m_ms[i].vertexCount / 3; j++) {
				//面積の計算
				XMFLOAT3 p[3];
				for (int k = 0; k < 3; k++)
					p[k] = vs[pmx->m_is[faceIndex * 3 + k]].position;
				float area = Length(Cross(p[0] - p[1], p[0] - p[2])) / 2;
				//面積*明度で全光束に比例したウェイトとする
				float weight = area * Dot(ms[i].emission, XMFLOAT3(0.2126729, 0.7151522, 0.0721750));
				totalLightW += weight;

				lights.push_back({ ms[i].emission, (int)faceIndex, weight, 0 });	//pdfには仮に現在のウェイトを入れておく
				faceIndex++;
			}
		} else {
			faceIndex += pmx->m_ms[i].vertexCount / 3;
		}
	}
	//各ライトのpdfとcdfを決定
	float lightCDF = 0;
	for (int i = 0; i < lights.size(); i++) {
		lights[i].pdf /= totalLightW;
		lightCDF += lights[i].pdf;
		lights[i].cdf = lightCDF;
	}
	lights.push_back({ {0,0,0}, 0, 0, 0 });	//要素数0のバッファは作れないのでダミーデータを最後に足しとく
	Buf lightBuf = dxr->CreateBuf(lights.data(), sizeof(Light), lights.size(), L"Light Buffer");

	//skybox
	auto skybox = dxr->CreateTex2D(SKYBOX);

	//絞り
	auto aperture = dxr->CreateTex2D(L"aperture.png");

	//ComputeShaderでskyboxのpdf,cdf,SHを作る。全部で11パス構成
	struct SHCoeff {
		XMFLOAT4 c[9];
	};	//skyboxを2次までのSHをした結果格納用coeff[0]がl=0, coeff[1-3]がl=1,m=-1,0,+1、coeff[4-8]がl=2,m=-2～+2
	UINT SW, SH;	//skyboxの画像のサイズ
	SW = skybox.desc().Width;
	SH = skybox.desc().Height;
	RWTex2D skyboxAvgRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT, L"skybox avg row");
	RWTex2D skyboxAvgAll = dxr->CreateRWTex2D(1, 1, DXGI_FORMAT_R32_FLOAT, L"skybox avg all");
	RWTex2D skyboxPDFRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT, L"skybox pdf row");
	RWTex2D skyboxPDF = dxr->CreateRWTex2D(SW, SH, DXGI_FORMAT_R32_FLOAT, L"skybox pdf");
	RWTex2D skyboxCDFRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT, L"skybox cdf row");
	RWTex2D skyboxCDF = dxr->CreateRWTex2D(SW, SH, DXGI_FORMAT_R32_FLOAT, L"skybox cdf");
	RWTex2D skyboxPDFEx = dxr->CreateRWTex2D(SW, SH, DXGI_FORMAT_R32_FLOAT, L"skybox pdf ex");
	RWTex2D skyboxPDFExRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT, L"skybox pdf ex row");
	RWTex2D skyboxPDFExAvg = dxr->CreateRWTex2D(1, 1, DXGI_FORMAT_R32_FLOAT, L"skybox pdf ex avg");
	RWBuf skyboxSHX = dxr->CreateRWBuf(sizeof(SHCoeff), SH, L"skybox SHX");
	RWBuf skyboxSH = dxr->CreateRWBuf(sizeof(SHCoeff), 1, L"skybox SH");

	bool MISCompensation = true;
	if (MISCompensation) {
		const vector<wstring> skyEntries = { L"AvgRowCS", L"AvgAllCS", L"PDFExCS", L"PDFExRowCS",L"PDFExAvgCS", L"PDF2CS", L"PDF2RowCS", L"CDFRowCS", L"CDFCS", L"SHComboX", L"SHComboY" };
		const vector<XMUINT2> skyThreads = { {SH,1}, {1,1}, {SW,SH}, {SH,1}, {1,1}, {SW,SH}, {SH,1}, {1,1}, {SH,1}, {SH,1}, {1,1} };
		for (int i = 0; i < skyEntries.size(); i++) {
			PassMaker* skyPass = new PassMaker(dxr);
			skyPass->PushSRV(skybox);			//原画
			skyPass->PushUAV(skyboxAvgRow);		//PDF計算用6つ
			skyPass->PushUAV(skyboxAvgAll);
			skyPass->PushUAV(skyboxPDFRow);
			skyPass->PushUAV(skyboxPDF);
			skyPass->PushUAV(skyboxCDFRow);
			skyPass->PushUAV(skyboxCDF);
			skyPass->PushUAV(skyboxPDFEx);		//MIS compensation用3つ
			skyPass->PushUAV(skyboxPDFExRow);
			skyPass->PushUAV(skyboxPDFExAvg);
			skyPass->PushUAV(skyboxSHX);		//SH計算用2つ
			skyPass->PushUAV(skyboxSH);
			Shader skyCS1 = dxr->CompileShader(L"hlsl\\skyboxPDF.hlsl", skyEntries[i].c_str(), L"cs_6_1", CompileOption);
			skyPass->ComputePass(skyCS1);
			skyPass->Compute(skyThreads[i].x, skyThreads[i].y, 1);
			delete skyPass;
			LOG(L"sky CS %d/%d : %s", i + 1, skyEntries.size(), skyEntries[i].c_str());
		}
	} else {
		const vector<wstring> skyEntries = { L"AvgRowCS", L"AvgAllCS", L"PDFRowCS", L"PDFCS", L"CDFRowCS", L"CDFCS" };
		const vector<XMUINT2> skyThreads = { {SH,1}, {1,1}, {SH,1}, {SW,SH}, {1,1}, {SH,1} };
		for (int i = 0; i < 6; i++) {
			PassMaker* skyPass = new PassMaker(dxr);
			skyPass->PushSRV(skybox);
			skyPass->PushUAV(skyboxAvgRow);
			skyPass->PushUAV(skyboxAvgAll);
			skyPass->PushUAV(skyboxPDFRow);
			skyPass->PushUAV(skyboxPDF);
			skyPass->PushUAV(skyboxCDFRow);
			skyPass->PushUAV(skyboxCDF);
			Shader skyCS1 = dxr->CompileShader(L"hlsl\\skyboxPDF.hlsl", skyEntries[i].c_str(), L"cs_6_1", CompileOption);
			skyPass->ComputePass(skyCS1);
			skyPass->Compute(skyThreads[i].x, skyThreads[i].y, 1);
			delete skyPass;
			LOG(L"sky CS %d/6", i + 1);
		}
	}


	//テクスチャ
	vector<Tex2D> ts;
	for (int i = 0; i < pmx->m_tcount; i++) {
		ts.push_back(dxr->CreateTex2D((modelpath + pmx->m_ts[i]).c_str(), pmx->m_ts[i]));
	}
	ts.push_back(dxr->CreateTex2D(L"white.png"));	//テクスチャ数が0だとルートシグネチャでエラーが出るので最低保証として入れとく

	//テスト
	//ts[0] = dxr->CreateTextTexture(1024, 1024, taCenter, taMiddle, L"Dayo!", LogFont(L"Times New Roman", 256), L"Font Texture");


	//2.5 VMDの読み込み
	Physics* physics = new Physics();	//物理エンジン
	physics->fps = 120;
	VMD* vmd = new VMD(L"vmd\\hifi.vmd");
	PoseSolver* solver = new PoseSolver(physics, pmx, vmd);
	solver->Solve(iTick);	//VMDからiTickフレーム目のポーズ(各ボーンのtransform)の取得
	solver->ResetPhysics();
	solver->PrewarmPhysics(30);	//TODO:この実装ではモデル1体動かすごとに物理も進んでしまうのでモデルのステップ更新と物理のステップ更新を切り分けないといけない

	//ComputeShaderで各ボーンのtransformから頂点の移動をさせる
	//頂点に対する各ボーンの影響度マップを作る
	Skinning* skin = new Skinning[pmx->m_vcount];
	for (int i = 0; i < pmx->m_vcount; i++) {
		auto v = pmx->m_vs[i];
		for (int j = 0; j < 4; j++) {
			skin[i].iBone[j] = v.bone[j];
			skin[i].weight[j] = v.weight[j];
		}
		skin[i].sdef_c = v.sdef_c;
		skin[i].weightType = v.weightType;
		XMFLOAT3 hdR = (v.sdef_r0 - v.sdef_r1) / 2;
		if (v.weightType == 3) {
			skin[i].weight[1] = hdR.x;
			skin[i].weight[2] = hdR.y;
			skin[i].weight[3] = hdR.z;
		}
	}
	Buf skinBuf = dxr->CreateBuf(skin, sizeof(Skinning), pmx->m_vcount, L"SkinningBuffer");

	//ボーン行列(ポーズ)バッファ。アニメーションさせる場合、毎フレーム書き換えてGPUに送る必要アリ
	XMMATRIX* bonedata = new XMMATRIX[solver->bones.size()];
	for (int i = 0; i < solver->bones.size(); i++) {
		bonedata[i] = XMMatrixTranspose(solver->bones[i]->transform);
	}
	Buf boneBuf = dxr->CreateBufCPU(bonedata, sizeof(XMMATRIX), solver->bones.size(), true, false, L"BoneTransforms");

	//モーフ値バッファ... キーフレームの値から各モーフ値を取得した結果を格納する。これもアニメーションさせる場合毎フレーム更新すべし
	float dmymorph = 0;
	Buf morphValuesBuf;
	if (solver->morphValues.size() > 0)
		morphValuesBuf = dxr->CreateBufCPU(solver->morphValues.data(), sizeof(float), solver->morphValues.size(), true, false, L"morphValues");
	else {
		morphValuesBuf = dxr->CreateBufCPU(&dmymorph, sizeof(float), 1, true, false, L"morphValues");	//モーフの個数0のモデル対策
	}

	//どのモーフがどの頂点に影響するか？
	vector<vector<MorphItem>>morphVertexTable;
	morphVertexTable.resize(pmx->m_vcount);
	for (int i = 0; i < pmx->m_mocount; i++) {
		auto mo = pmx->m_mos[i];
		if (mo.kind == 1) {
			//頂点モーフ
			PMXVertexMorphOffset* mof = (PMXVertexMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dPosition = mof->offset;
				morphVertexTable[mof->vertex].push_back(mi);
				mof++;
			}
		} else if (mo.kind == 3) {
			//UVモーフ
			PMXUVMorphOffset* mof = (PMXUVMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dUV = mof->offset;
				morphVertexTable[mof->vertex].push_back(mi);
				mof++;
			}
		}
	}
	//テーブルを平らにする(GPUから並列処理しやすいような格好にまとめる)
	vector<MorphItem>morphTable;			//平らにした後のテーブル
	vector<MorphPointer>morphTablePointer;	//各頂点に対応するモーフについての情報はテーブルのどこに何個書いてあるか？
	morphTablePointer.resize(pmx->m_vcount);
	for (int i = 0; i < pmx->m_vcount; i++) {
		//ポインタ作成
		MorphPointer mp = {};
		int count = morphVertexTable[i].size();
		if (count == 0) {
			mp.where = -1;	//この頂点を動かすようなモーフが無い場合は-1を入れておく
			mp.count = 0;
			morphTablePointer[i] = mp;
		} else {
			mp.where = (int)morphTable.size();	//この頂点を動かすモーフについての情報は、morphTableの何番目から書かれているか？
			mp.count = count;				//この頂点を動かすモーフは何個あるか？
			morphTablePointer[i] = mp;
		}
		//MorphItemのコピー
		for (int j = 0; j < count; j++) {
			//LOG(L"vertex %.4d #%.2d <- morph %.3d(%s)", i, j, morphVertexTable[i][j].iMorph, pmx->m_mos[morphVertexTable[i][j].iMorph].name);
			morphTable.push_back(morphVertexTable[i][j]);
		}
	}
	//以上からGPUにデータを送る。この2つのデータ(モーフ番号と変更される頂点の対応付け)はフレームごとに更新の必要はない
	Buf morphTableBuf;
	if (morphTable.size() == 0)
		morphTable.push_back(MorphItem{ 0, {0,0,0}, {0,0,0,0} });	//モーフが1個もないモデル用ダミーデータ
	morphTableBuf = dxr->CreateBuf(morphTable.data(), sizeof(MorphItem), morphTable.size(), L"morph table buffer");
	Buf morphTablePointerBuf = dxr->CreateBuf(morphTablePointer.data(), sizeof(MorphPointer), morphTablePointer.size(), L"morph table pointer buffer");


	//3.ConstantBufferの中身を定義
	Constantan* cs;
	CB cb = dxr->CreateCB(nullptr, sizeof(Constantan), L"ConstantBuffer");	//中身が空でサイズだけ確保しておく
	cs = (Constantan*)cb.pData;	//cb.pDataにデータを書き込むためのポインタが入るので、それを使ってCBの中身を操作する
	cs->resolution = XMFLOAT2(W, H);	//残りは後で
	cs->nLights = lights.size() - 1;	//ライトポリゴン数。ダミーデータの分1つ引く

	//4. 結果出力用バッファ作成
	RWTex2D dxrOut = dxr->CreateRWTex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, L"DXRout");	//レイトレ結果出力用バッファ
	RWTex2D dxrFlare = dxr->CreateRWTex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, L"DXRflare");	//レンズフレア結果出力用バッファ

	//5.BLASの作成。VertexVufferとIndexBuffer1個ずつ→1個のBLASが作られる
	vector<BLAS>blas;
	blas.push_back(dxr->BuildBLAS(vb, ib));	//MMDモデルのBLAS
	D3D12_RAYTRACING_AABB fogaabb = { -10,-10,-10, 10,10,10 };
	blas.push_back(dxr->BuildBLAS(1, &fogaabb, L"fog blas"));
	blas[1].ID = 9999;	//とりあえず9999番を「フォグのID」という事にする
	blas[1].contributionToHitGroupIndex = 3;

	//6.TLASの作成。BLASの配列からTLASを作成。BLASのメンバをいじると変換行列などを設定できる
	TLAS tlas = dxr->BuildTLAS(blas.size(), blas.data());

	//7.シェーダのコンパイル
	//シェーダのコンパイルはこんな感じでRayGenとかClosestHitとか1つ毎にCompileShader回して各コンパイル済みシェーダのblobにする
	//エントリポイントの大文字小文字はどっちでもいいらしく、[shader("なんとか")]の"なんとか"に当たる部分と合ってれば関数名はどうでもいいっぽい
	wchar_t pmx_shader[] = L"hlsl\\pmx.hlsl";
	auto raygenBlob = dxr->CompileShader(pmx_shader, L"RayGeneration", L"lib_6_6", CompileOption);
	auto missBlob = dxr->CompileShader(pmx_shader, L"Miss", L"lib_6_6", CompileOption);
	auto missSSS = dxr->CompileShader(pmx_shader, L"MissSSS", L"lib_6_6", CompileOption);
	auto missShadow = dxr->CompileShader(pmx_shader, L"MissShadow", L"lib_6_6", CompileOption);
	auto CHSBlob = dxr->CompileShader(pmx_shader, L"ClosestHit", L"lib_6_6", CompileOption);
	auto CHSSS = dxr->CompileShader(pmx_shader, L"ClosestHitSSS", L"lib_6_6", CompileOption);
	auto AnyHitBlob = dxr->CompileShader(pmx_shader, L"AnyHit", L"lib_6_6", CompileOption);
	auto AnyHitSSS = dxr->CompileShader(pmx_shader, L"AnyHitSSS", L"lib_6_6", CompileOption);
	auto AnyHitShadow = dxr->CompileShader(pmx_shader, L"AnyHitShadow", L"lib_6_6", CompileOption);
	auto CHSFog = dxr->CompileShader(pmx_shader, L"ClosestHitFog", L"lib_6_6", CompileOption);
	auto IntersectFog = dxr->CompileShader(pmx_shader, L"IntersectFog", L"lib_6_6", CompileOption);
	vector<Shader> misses(3);
	misses[0] = missBlob;
	misses[1] = missSSS;
	misses[2] = missShadow;
	vector<HItGroup> hg(4);
	hg[0] = { D3D12_HIT_GROUP_TYPE_TRIANGLES, CHSBlob, AnyHitBlob, {} };
	hg[1] = { D3D12_HIT_GROUP_TYPE_TRIANGLES, CHSSS, AnyHitSSS, {} };
	hg[2] = { D3D12_HIT_GROUP_TYPE_TRIANGLES, {}, AnyHitShadow, {} };
	hg[3] = { D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE, CHSFog, {}, IntersectFog };

	//8.サンプラーの作成
	Sampler samp(CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR));

	//9.スキニング用コンピュートパス。vmdのデータを元にポーズ付け
	UINT nVertex = vb.desc().Width / vb.elemSize;
	RWBuf rwvb = dxr->CreateRWBuf(sizeof(Vertex), nVertex, L"altered VB");
	PassMaker* pc = new PassMaker(dxr);
	pc->PushSRV(vb);		//t0 元のVB
	pc->PushSRV(skinBuf);	//t1 ボーン番号とウェイトの配列
	pc->PushSRV(boneBuf);	//t2 ボーンごとの姿勢の配列
	pc->PushSRV(morphValuesBuf); //t3 モーフ値
	pc->PushSRV(morphTableBuf);	 //t4 モーフ番号と位置・UVへの影響を書いたテーブル
	pc->PushSRV(morphTablePointerBuf);	//t5 各頂点が↑のテーブルのどこを読めばいいのか書いた物
	pc->PushUAV(rwvb);		//u0 変形後VB(出力用)
	pc->PushCBV(cb);		//b0 定数バッファ
	auto csBlob = dxr->CompileShader(L"hlsl\\ComputeShader.hlsl", L"CS", L"cs_6_1", CompileOption);
	pc->ComputePass(csBlob);

	//10.レンダリングパスの定義
	//バケツみたいに使いたい物の名札を放り込むとシェーダへの入力とシェーダからの出力先(レイトレーシングの場合UAV)がなんとなく決まる
	PassMaker* pm = new PassMaker(dxr);
	pm->PushSRV(tlas);	//t0 TLAS
	pm->PushSRV(rwvb);	//t1 変形後のVBを参照しないとシェーディングがおかしくなる	
	pm->PushSRV(ib);	//t2 IndexBuffer
	pm->PushSRV(mb);	//t3 マテリアルバッファ
	pm->PushSRV(fb);	//t4 面番号→マテリアル番号テーブル
	pm->PushSRV(lightBuf);	//t5 ライトバッファ
	pm->PushSRV(skybox);//t6 skyboxテクスチャ
	pm->PushSRV(aperture);	//t7 絞りテクスチャ
	pm->PushCBV(cb);	//b0 定数バッファ
	pm->PushSRV(skyboxPDFRow);
	pm->PushSRV(skyboxPDF);
	pm->PushSRV(skyboxCDFRow);
	pm->PushSRV(skyboxCDF);
	pm->PushSRV(skyboxSH);
	pm->PushSampler(samp);	//s0 サンプラー
	for (int i = 0; i < ts.size(); i++)
		pm->PushSRV(ts[i], 1);	//テクスチャ群は t0～n, space1 で使う
	pm->PushUAV(dxrOut);
	pm->PushUAV(dxrFlare);
	//なんとなく決めた所で「レイトレ用です」という進路を決定する
	pm->RaytracingPass(raygenBlob, misses, hg, sizeof(float) * 32, sizeof(XMFLOAT2), 4);	//出力先が具体的にどのUAVに入ってる物なのかはシェーダで決められる
	//マルチパスレンダリングする時はパスの数だけPassMakerインスタンスを作ってね

	//dxrFlareをクリアするパス
	PassMaker* pclear = new PassMaker(dxr);
	pclear->PushUAV(dxrFlare);
	auto clearBlob = dxr->CompileShader(L"hlsl\\clear.hlsl", L"Clear", L"lib_6_6", CompileOption);
	auto clearMiss = dxr->CompileShader(L"hlsl\\clear.hlsl", L"Miss", L"lib_6_6", CompileOption);
	auto clearCHS = dxr->CompileShader(L"hlsl\\clear.hlsl", L"ClosestHit", L"lib_6_6", CompileOption);
	pclear->RaytracingPass(clearBlob, clearMiss, clearCHS, {}, {}, 128, 8, 4);

	//2パス目。アキュムレーション。accRTに現在のフレームと過去のフレームの加重平均を出力する
	//ポストプロセスパスの場合、出力はRTVに放り込まれた物になる。RTVにいくつも放り込むとマルチレンダーターゲットになる
	RT2D accRT = dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT, XMFLOAT4(0, 0, 0, 1), L"AccumulationBuffer");
	RWTex2D historyTex = dxr->CreateRWTex2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT, L"HistoryBuffer");
	auto vsBlob = dxr->CompileShader(L"hlsl\\PostProcess.hlsl", L"VS", L"vs_6_1", CompileOption);
	auto psBlob = dxr->CompileShader(L"hlsl\\PostProcess.hlsl", L"PSAcc", L"ps_6_1", CompileOption);
	PassMaker* pa = new PassMaker(dxr);
	pa->PushSRV(dxrOut);		//レイトレーシングの結果。ここではSRVに突っ込んでテクスチャとして読む
	pa->PushSRV(dxrFlare);
	pa->PushSRV(historyTex);	//RWTex2DはSRVとUAVの両方いける
	pa->PushCBV(cb);
	pa->PushSampler(samp);
	pa->PushRTV(accRT);
	pa->PostProcessPass(vsBlob, psBlob);

	//3パス目。ポストプロセスでhistoryTexの中身をリニア→ガンマ変換およびトーンマッピングして出力
	//「presentするパス」として作る
	//ポストプロセスがpresentパスの場合、レンダーターゲットはバックバッファのみになり、マルチレンダーターゲット不可(PushRTVの結果は無視される)
	RT2D ppOut = dxr->CreateRT2D(W, H, dxr->BackBufferFormat(), XMFLOAT4(0, 0, 0, 1), L"PostProccess out");
	PassMaker* pp = new PassMaker(dxr);
	pp->PushSRV(dxrOut);
	pp->PushSRV(dxrFlare);
	pp->PushSRV(historyTex);
	pp->PushCBV(cb);
	pp->PushSampler(samp);
	pp->PushRTV(ppOut);
	auto psBlob3 = dxr->CompileShader(L"hlsl\\PostProcess.hlsl", L"PSTonemap", L"ps_6_1", CompileOption);
	pp->PostProcessPass(vsBlob, psBlob3);

	//auxパス(OIDNへの入力用にcolor, albedo, normalを出力させる)
	auto oidnBuf = dxr->CreateRWBuf(sizeof(OIDNInput), W * H, L"OIDN input buffer", D3D12_HEAP_FLAG_SHARED);	//OIDNへのcolor,albedo,normal入力用
	auto oidnOutBuf = dxr->CreateRWBuf(12, W * H, L"OIDN output buffer", D3D12_HEAP_FLAG_SHARED);			//ODINからのcolor出力用
	auto auxraygenBlob = dxr->CompileShader(L"hlsl\\aux.hlsl", L"RayGeneration", L"lib_6_3");
	auto auxmissBlob = dxr->CompileShader(L"hlsl\\aux.hlsl", L"Miss", L"lib_6_3");
	auto auxCHSBlob = dxr->CompileShader(L"hlsl\\aux.hlsl", L"ClosestHit", L"lib_6_3");
	auto auxAnyHitBlob = dxr->CompileShader(L"hlsl\\aux.hlsl", L"AnyHit", L"lib_6_3");
	PassMaker* px = new PassMaker(dxr);
	px->PushSRV(tlas);	//t0 TLAS
	px->PushSRV(rwvb);	//t1 変形後のVBを参照しないとシェーディングがおかしくなる	
	px->PushSRV(ib);	//t2 IndexBuffer
	px->PushSRV(mb);	//t3 マテリアルバッファ
	px->PushSRV(fb);	//t4 面番号→マテリアル番号テーブル
	px->PushSRV(skybox);//t5 skyboxテクスチャ
	px->PushCBV(cb);	//b0 定数バッファ
	px->PushSampler(samp);	//s0 サンプラー
	for (int i = 0; i < ts.size(); i++)
		px->PushSRV(ts[i], 1);	//テクスチャ群は t0～n, space1 で使う
	px->PushSRV(historyTex);	//t6 アキュムレーションの結果(OIDNのColor入力としてくっつける)
	px->PushUAV(oidnBuf);
	px->RaytracingPass(auxraygenBlob, auxmissBlob, auxCHSBlob, auxAnyHitBlob, {}, sizeof(float) * 9, sizeof(XMFLOAT2), 1);

	//おいどん
	auto odev = oidnNewDevice(OIDN_DEVICE_TYPE_CUDA);
	oidnCommitDevice(odev);
	auto oidnFlags = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32;
	HANDLE hOidn;
	Validate(dxr->Device()->CreateSharedHandle(oidnBuf.res.Get(), nullptr, GENERIC_ALL, L"yrzmain_cpp_oidnIn", &hOidn), L"CreateSharedHandle failed");
	Validate(dxr->Device()->CreateSharedHandle(oidnOutBuf.res.Get(), nullptr, GENERIC_ALL, L"yrzmain_cpp_oidnOut", &hOidn), L"CreateSharedHandle failed");
	auto oidnIn = oidnNewSharedBufferFromWin32Handle(odev, oidnFlags, 0, L"yrzmain_cpp_oidnIn", W * H * 36);
	auto oidnOut = oidnNewSharedBufferFromWin32Handle(odev, oidnFlags, 0, L"yrzmain_cpp_oidnOut", W * H * 12);
	auto filter = oidnNewFilter(odev, "RT");
	oidnSetFilterImage(filter, "color", oidnIn, OIDN_FORMAT_FLOAT3, W, H, 0, 36, W * 36);
	oidnSetFilterImage(filter, "albedo", oidnIn, OIDN_FORMAT_FLOAT3, W, H, 12, 36, W * 36);	//albedoとnormalはDoFがきつい時はコメントアウトした方がいいかも
	oidnSetFilterImage(filter, "normal", oidnIn, OIDN_FORMAT_FLOAT3, W, H, 24, 36, W * 36);
	oidnSetFilterImage(filter, "output", oidnOut, OIDN_FORMAT_FLOAT3, W, H, 0, 12, W * 12);
	oidnSetFilterBool(filter, "hdr", true);
	oidnCommitFilter(filter);
	const char* errorMessage;
	if (oidnGetDeviceError(odev, &errorMessage) != OIDN_ERROR_NONE)
		LOGA("%s", errorMessage);

	PassMaker* po = new PassMaker(dxr);
	po->PushSRV(oidnOutBuf);
	po->PushCBV(cb);
	po->PushSampler(samp);
	po->PushRTV(ppOut);
	auto psBlobOidn = dxr->CompileShader(L"hlsl\\oidn.hlsl", L"PSTonemap", L"ps_6_1", CompileOption);
	po->PostProcessPass(vsBlob, psBlobOidn);

	//テロップ
	Tex2D creditTex = dxr->CreateTextTexture(L"3D model: よけち様", LogFont(L"刻明朝 Regular", 48), taLeft, L"credit Tex");
	Tex2D telopTex = dxr->CreateTex2D(512, 64, DXGI_FORMAT_R8_UNORM, L"telop Tex");
	Tex2D hankoTex = dxr->CreateTex2D(L"haru.png", L"hankoTex");
	vector<::byte> telopBuf(512 * 64);
	PassMaker* telop = new PassMaker(dxr);
	telop->PushSRV(ppOut);
	telop->PushSRV(creditTex);
	telop->PushSRV(telopTex);
	telop->PushSRV(hankoTex);
	telop->PushSampler(samp);
	Shader psTelop = dxr->CompileShader(L"hlsl\\telop.hlsl", L"PSTelop", L"ps_6_1", CompileOption);
	telop->PostProcessPass(vsBlob, psTelop);

	//8.メインループでレンダリング
	MSG msg;
	POINT prevPos = {};
	GetCursorPos(&prevPos);
	int prevSolvedFrame = iTick;
	DWORD prev_tm = timeGetTime();
	while (dxr->MainLoop(msg)) {
		//カメラむーぶ
		if (GetForegroundWindow() == dxr->hWnd()) {
			if (msg.message == WM_HOTKEY) {
				//スクショ撮る
				time_t t = time(NULL);
				struct tm local;
				localtime_s(&local, &t);
				wchar_t buf[128];
				wcsftime(buf, 128, L"SS_%Y_%m%d_%H%M%S.jpg", &local);

				dxr->Snapshot(buf);
			}
			if (msg.message == WM_MOUSEMOVE) {
				POINT mpos;
				GetCursorPos(&mpos);
				int dx = mpos.x - prevPos.x;
				int dy = mpos.y - prevPos.y;
				if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
					CameraTheta = clamp(CameraTheta - dy * 0.0025, 0.05 * PI, 0.95 * PI);
					CameraPhi -= dx * 0.0025;
					iFrame = 0;
				} else if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
					CameraDX -= 0.01 * dx;
					CameraDY += 0.01 * dy;
					iFrame = 0;
				}
				prevPos = mpos;
			} else if (msg.message == WM_MOUSEWHEEL) {
				CameraDistance -= GET_WHEEL_DELTA_WPARAM(msg.wParam) * 0.01;
				iFrame = 0;
			} else if (msg.message == WM_KEYDOWN) {
				if (msg.wParam == VK_LEFT) {
					iTick--;
					iFrame = 0;
				} else if (msg.wParam == VK_RIGHT) {
					iTick++;
					iFrame = 0;
				}
				if (msg.wParam == 'P') {
					solver->PrewarmPhysics(120);
					iFrame = 0;
				}
				if (msg.wParam == 'R') {
					solver->ResetPhysics();
					iFrame = 0;
				}
				if (msg.wParam == VK_NUMPAD6) {
					skyboxPhi -= PI / 100;
					iFrame = 0;
				}
				if (msg.wParam == VK_NUMPAD4) {
					skyboxPhi += PI / 100;
					iFrame = 0;
				}
			}
		}
		double ct = cos(CameraTheta), st = sin(CameraTheta);
		double cp = cos(CameraPhi), sp = sin(CameraPhi);
		XMFLOAT3 Z = XMFLOAT3(-st * sp, -ct, st * cp);
		XMFLOAT3 Y = XMFLOAT3(0, 1, 0);
		XMFLOAT3 X = Normalize(Cross(Y, Z));
		Y = Cross(Z, X);
		cs->cameraRight = X;
		cs->cameraUp = Y;
		cs->cameraForward = Z;
		cs->cameraPosition = -Z * CameraDistance + X * CameraDX + Y * CameraDY;
		cs->fov = 1 / tanf(30.0 / 2 * PI / 180);
		cs->skyboxPhi = skyboxPhi;

		cs->iTick = iTick;
		cs->iFrame = iFrame;

		//レンダリングに時間が掛かりまくっていると反応が悪くなる事に対策
		auto tm = timeGetTime();
		if (tm - prev_tm > 60) {
			prev_tm = tm;
			continue;
		}

		bool solved = false;


		//描画はじまるよー
		for (int i = 0; i < 1; i++) {
			//現在のフレームのポーズにboneBufを変更する
			//モーションブラー用
			/*
			for (int j = 0; j < 4; j++) {
				solver->Solve(iTick % (vmd->lastFrame + 1) + j / 8.0);//シャッター間隔はフレーム間隔の倍くらいらしいので単純に4分割ではなく8分割して半分を使う
				solver->physics->m_fps = 120;
				solver->UpdatePhysics(1 / 30.0f);
			}
			*/
			if (iTick != prevSolvedFrame) {
				solver->Solve(iTick % (vmd->lastFrame + 1));
				solver->UpdatePhysics(1 / 30.0f);
				prevSolvedFrame = iTick;
			}

			for (int i = 0; i < pmx->m_bcount; i++) {
				bonedata[i] = XMMatrixTranspose(solver->bones[i]->transform);
			}
			void* pData;
			boneBuf.res->Map(0, nullptr, &pData);
			memcpy(pData, bonedata, pmx->m_bcount * sizeof(XMMATRIX));
			boneBuf.res->Unmap(0, nullptr);

			//モーフ値の更新
			morphValuesBuf.res->Map(0, nullptr, &pData);
			memcpy(pData, solver->morphValues.data(), solver->morphValues.size() * sizeof(float));
			morphValuesBuf.res->Unmap(0, nullptr);

			//CSでスキニング・表情モーフなどをやって、BLAS,TLASに反映
			if (!solved || animation) {
				pc->Compute(nVertex, 1, 1);
				dxr->Prepare();
				dxr->UpdateBLAS(blas[0], rwvb, ib);
				dxr->UpdateTLAS(tlas, blas.size(), blas.data());
				dxr->ExecuteCommandList();	//ここまでで一回全部実行する
				dxr->WaitForGPU();
				solved = true;
			}

			int nSpatio = (iFrame < 60 ? 1 : 16);
			for (int j = 0; j < nSpatio; j++) {
				dxr->Prepare();
				pclear->Render();
				pm->Render();	//dxroutへレイトレによるメインのレンダリング
				pa->Render();	//アキュムレーション(dxroutとhistoryをブレンドしてaccRTへ)
				dxr->CopyResource(historyTex, accRT);	//accRTの内容をhistoryTexにコピーする
				dxr->ExecuteCommandList();
				dxr->WaitForGPU();
				//ConstantBufferの更新は描画が完了してからやるべし
				//そうでないと実行中にCBの中身が入れ替わっておかしなことになる
				iFrame++;
				cs->iFrame = iFrame;
			}
		}

		//テロップ
		fill(telopBuf.begin(), telopBuf.end(), 0);
		RenderTextToMemory(telopBuf.data(), 512, 64, Format(L"%d samples", iFrame).c_str(), LogFont(L"刻明朝 Regular", 48), taLeft, taTop);
		dxr->UploadTex2D(telopTex, telopBuf.data());

		// デノイズ
		if (denoise) {
			//OIDNへの入力(color,albedo,normal)をまとめる
			dxr->Prepare();
			px->Render();
			dxr->ExecuteCommandList();
			dxr->WaitForGPU();

			//OIDNでデノイズ
			oidnExecuteFilter(filter);
			const char* errorMessage;
			if (oidnGetDeviceError(odev, &errorMessage) != OIDN_ERROR_NONE)
				LOGA("%s", errorMessage);

			//OIDNからの出力を表示
			dxr->Prepare();
			po->Render();
			telop->Render();
			dxr->Present();
		} else {
			//デノイズなし
			dxr->Prepare();
			pp->Render();
			telop->Render();
			dxr->Present();
		}


		if (animation) {
			iFrame = 0;
			iTick++;
			//dxr->Snapshot(Format(L"out\\%d.png",iTick).c_str());
		}

		SetWindowTextW(dxr->hWnd(), Format(L"samples %d @ frame %d", iFrame, iTick).c_str());
	}

	delete dxr;
}
