#if __INTELLISENSE__
#include "YRZ.ixx"
#include "PMXLoader.ixx"
#else
import YRZ;
import PMXLoader;
#endif

//STL
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

//Windows
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <io.h>
#include <shellapi.h>
#include <timeapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

//DirectX
#include <d2d1_3.h>
#include <d3d11on12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
#include <dwrite.h>

//その他外部ライブラリ
#include <DirectXTex.h>
#include <oidn.hpp>
#include "inicpp.h"

// その他 子ウィンドウなど
#include "AVIFileWriter.h"
#include "GdiCapture.h"
#include "AVISettingWindow.h"
#include "EnvSettingWindow.h"
#include "MotionSettingWindow.h"
#include "WindowSettingWindow.h"
#include "dayoWorkspace.h"

//リンクしてほしいライブラリ
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "OpenImageDenoise.lib")
#pragma comment(lib, "Version.lib")

#pragma warning(disable: 4267 4244 4305 4838)

//グーロバル変数・定数
int iTick = 0;	//MMDモーションのフレーム番号
int iFrame = 0;	//操作されるとリセットされるフレーム番号
bool animation = false;
bool denoise = false;
bool Recompile = false;
int ShaderMode = 0;	//0:プレビュー, 1:片方向パストレ, 2:双方向パストレ
int HelpMode = 0;	//ヘルプ表示の細かさ 0:全部 1:情報のみ 2:表示しない

std::wstring ExePath;	//この実行ファイルのパス(\で終わる)
std::wstring BasePath;	//実行ファイルのパス + ..\..\MikuMikuDayo\ (\で終わる)

const wchar_t* HLSLPath;//HLSLのソースファイルを置いてあるパス
std::vector<const wchar_t*> CompileOption;

DayoWorkspace* dayoWork; // ワークスペースインスタンス

//wstringで書かれたファイル名をフルパスに変換するマクロ
#define BASEDIR(_x)  ((BasePath + (_x)).c_str())

//VertexBufferの中身
struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT2 uv;
};

//変形情報。頂点毎に1つ付く。
struct Skinning {
	int iBone[4];		//ボーン番号
	float weight[4];	//各ボーンのウェイト(SDEFの場合はweight[1-3]に(sdef_r0-sdef_r1)/2.xyzを入れる)
	int weightType;		//変形方式(0-2:BDEF1,2,4 / 3:SDEF)
	DirectX::XMFLOAT3 sdef_c;	//SDEFパラメータ
};

//頂点UVモーフ情報
struct MorphItem {
	int iMorph = 0;			//対応するモーフ番号
	DirectX::XMFLOAT3 dPosition = DirectX::XMFLOAT3(0,0,0);	//モーフ値1につき頂点座標はどれだけ動くか
	DirectX::XMFLOAT4 dUV = DirectX::XMFLOAT4(0,0,0,0);		//モーフ値1につきUV座標はどれだけ動くか
};

//各頂点に割り当てられる、頂点モーフテーブルの参照情報
struct MorphPointer {
	int where;	//テーブルのどこから自分に割り当てられたモーフ情報が始まるか？ -1で割り当て無し
	int count;	//いくつアイテムがあるか
};

//ライトポリゴン情報
struct Light {
	DirectX::XMFLOAT3 Le;	//輝度
	int iFace;		//面番号
	float pdf;		//このライトの放射束/全ライトの放射束
	float cdf;		//0番から↑を累積していったもの
};

//面情報
struct Face {	
	int iMaterial;	//マテリアル番号
	int iLight;		//ライトリストに加えられている場合、その番号
};

//OIDNへの入力
struct OIDNInput {
	DirectX::XMFLOAT3 color;
	DirectX::XMFLOAT3 albedo;
	DirectX::XMFLOAT3 normal;
};

//定数バッファ。float4を跨ぐような変数を置かないようにすべし
struct Constantan {
	UINT iFrame;					//収束開始からのフレーム番号、0スタート						4
	UINT iTick;						//MMDアニメーションのフレーム番号							8
	DirectX::XMFLOAT2 resolution;	//解像度													16
	DirectX::XMFLOAT3 cameraRight;	//ワールド座標系でのカメラの右方向							28
	float fov;						//cot(垂直画角/2)											32
	DirectX::XMFLOAT3 cameraUp;		//カメラの上方向											44
	float skyboxPhi;				//skyboxの回転角											48
	DirectX::XMFLOAT3 cameraForward;//カメラの前方向											60
	int nLights;					//ライトポリゴンの数(キャラクター①)						64
	DirectX::XMFLOAT3 cameraPosition;//カメラの位置												76
	int spectralRendering;			//分光を考慮してレンダリングする?(bool)						80
	float sceneRadius;				//シーン全体の半径(BDPTでのみ使用)							84
	float lensR, pint;				//レンズの半径(DoF用)										88, 92
	//追加したCB要素//
	float brigtnessGain;			//明るさゲイン												96
	float saturationGain;			//彩度ゲイン												100
	int nLights_st;					//ライトポリゴンの数(ステージ)								104
	int DofEnable;					//DOFを有効にするか(bool)									108
	int FogEnable;					//FOGを有効にするか(bool)									112
	int ShadowEnable;				//照明からのシャドウを有効にするか(bool)					116
	DirectX::XMFLOAT3 lightPosition;//照明(≒太陽)の位置										128
	DirectX::XMFLOAT4 fogColor;		//フォグカラー												144
};

//ユーザーコンピュートシェーダー用 バッファ構造体
//MikuMikuDayoのカメラ・照明情報
struct CameraAndLight {
	DirectX::XMFLOAT3 camera_up;		//カメラの上方向
	DirectX::XMFLOAT3 camera_right;	//ワールド座標系でのカメラの右方向
	DirectX::XMFLOAT3 camera_forward;	//カメラの前方向
	DirectX::XMFLOAT3 camera_position;	//カメラの位置
	float  camera_pint;		//カメラのピント位置
	DirectX::XMFLOAT3 light_position;	//光源(≒太陽)の位置
};

//MikuMikuDayoのワールド情報
struct WorldInfomation {
	unsigned int iFrame;			//収束開始からのフレーム番号、0スタート
	unsigned int iTick;				//MMDアニメーションのフレーム番号
};

//線形にする？
DirectX::XMFLOAT3 ToLinear(DirectX::XMFLOAT3 c)
{
	DirectX::XMFLOAT3 r;
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

//小文字にする
std::wstring wtolower(const std::wstring& str)
{
	std::wstring result = str;
	std::locale loc;
	for (auto& ch : result) {
		ch = std::tolower(ch, loc);
	}
	return result;
}

//キャッシュがあればそこから読み、なかったらコンパイルする
YRZ::Shader LoadShader(bool recompile, YRZ::DXR* dxr, const std::wstring& cachefile,
	const std::wstring& filename, const std::wstring& entrypoint, const std::wstring& target, const std::vector<const wchar_t*>& option)
{
	YRZ::LOG(L"LoadShader {}@{}", entrypoint, filename);
	namespace fs = std::filesystem;

	bool matched = false;
	std::wstring cache = BasePath + L"shadercache\\" + cachefile;

	auto srctime = std::filesystem::last_write_time(BasePath + filename);
	auto cachetime = srctime;

	if (std::filesystem::exists(cache))
		cachetime = std::filesystem::last_write_time(cache);
	else
		cachetime = srctime + std::chrono::hours(1);

	//シェーダーキャッシュ内のファイルを探す
	if (!recompile && (cachetime >= srctime))
		//強制再コンパイルモードがオフ、かつ キャッシュがソースより新しい場合はキャッシュを探す(ソースの依存関係は調べないので注意)
		for (const fs::directory_entry& dir : fs::directory_iterator(BASEDIR(L"shadercache")))
			if (dir.path().wstring() == cache)
				return YRZ::Shader(cache.c_str(), entrypoint.c_str());

	//再コンパイルする
	YRZ::Shader shader = dxr->CompileShader((BasePath + filename).c_str(), entrypoint.c_str(), target.c_str(), option);
	shader.SaveToFile(cache.c_str());

	return shader;
}


//skyboxの読み込み
std::tuple<std::vector<YRZ::Tex2D>, std::vector<YRZ::Buf>> LoadSkybox(const std::wstring& filename, YRZ::DXR* dxr)
{
	//skybox
	auto skybox = dxr->CreateTex2D(filename.c_str());

	//ComputeShaderでskyboxのpdf,cdf,SHを作る。全部で11パス構成
	struct SHCoeff {
		DirectX::XMFLOAT4 c[9];
	};	//skyboxを2次までのSHをした結果格納用coeff[0]がl=0, coeff[1-3]がl=1,m=-1,0,+1、coeff[4-8]がl=2,m=-2～+2
	UINT SW, SH;	//skyboxの画像のサイズ
	SW = skybox.desc().Width;
	SH = skybox.desc().Height;
	YRZ::Tex2D skyboxAvgRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxAvgAll = dxr->CreateRWTex2D(1, 1, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxPDFRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxPDF = dxr->CreateRWTex2D(SW, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxCDFRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxCDF = dxr->CreateRWTex2D(SW, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxPDFEx = dxr->CreateRWTex2D(SW, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxPDFExRow = dxr->CreateRWTex2D(1, SH, DXGI_FORMAT_R32_FLOAT);
	YRZ::Tex2D skyboxPDFExAvg = dxr->CreateRWTex2D(1, 1, DXGI_FORMAT_R32_FLOAT);
	YRZ::Buf skyboxSHX = dxr->CreateRWBuf(sizeof(SHCoeff), SH);
	YRZ::Buf skyboxSH = dxr->CreateRWBuf(sizeof(SHCoeff), 1);
	skyboxAvgRow.SetName(L"skyboxAvgRow");
	skyboxAvgAll.SetName(L"skyboxAvgAll");
	skyboxPDFRow.SetName(L"skyboxPDFRow");
	skyboxPDF.SetName(L"skyboxPDF");
	skyboxCDFRow.SetName(L"skyboxCDFRow");
	skyboxCDF.SetName(L"skyboxCDF");
	skyboxPDFEx.SetName(L"skyboxPDFEx");
	skyboxPDFExRow.SetName(L"skyboxPDFExRow");
	skyboxPDFExAvg.SetName(L"skyboxPDFExAvg");
	skyboxSHX.SetName(L"skyboxSHX");
	skyboxSH.SetName(L"skyboxSH");


	bool MISCompensation = true;
	if (MISCompensation) {
		const std::vector<std::wstring> skyEntries = { L"AvgRowCS", L"AvgAllCS", L"PDFExCS", L"PDFExRowCS",L"PDFExAvgCS", L"PDF2CS", L"PDF2RowCS", L"CDFRowCS", L"CDFCS", L"SHComboX", L"SHComboY" };
		const std::vector<DirectX::XMUINT2> skyThreads = { {SH,1}, {1,1}, {SW,SH}, {SH,1}, {1,1}, {SW,SH}, {SH,1}, {1,1}, {SH,1}, {SH,1}, {1,1} };
		for (int i = 0; i < skyEntries.size(); i++) {
			YRZ::Pass* skyPass = new YRZ::Pass(dxr);
			skyPass->SRV[0].push_back(&skybox);
			skyPass->UAV.push_back({ &skyboxAvgRow,0 });
			skyPass->UAV.push_back({ &skyboxAvgAll,0 });
			skyPass->UAV.push_back({ &skyboxPDFRow,0 });
			skyPass->UAV.push_back({ &skyboxPDF,0 });
			skyPass->UAV.push_back({ &skyboxCDFRow,0 });
			skyPass->UAV.push_back({ &skyboxCDF,0 });
			skyPass->UAV.push_back({ &skyboxPDFEx,0 });		//MIS compensation用3つ
			skyPass->UAV.push_back({ &skyboxPDFExRow,0 });
			skyPass->UAV.push_back({ &skyboxPDFExAvg,0 });
			skyPass->UAV.push_back({ &skyboxSHX,0 });		//SH計算用2つ
			skyPass->UAV.push_back({ &skyboxSH,0 });
			YRZ::Shader skyCS1 = LoadShader(Recompile, dxr, L"skyboxPDF_"+skyEntries[i], L"hlsl\\skyboxPDF.hlsl", skyEntries[i].c_str(), L"cs_6_1", CompileOption);
			skyPass->ComputePass(skyCS1);
			dxr->OpenCommandListCS();
			skyPass->Compute(skyThreads[i].x, skyThreads[i].y, 1);
			dxr->ExecuteCommandListCS();
			delete skyPass;
			YRZ::DEB(L"sky CS {}/{} : {}", i + 1, skyEntries.size(), skyEntries[i].c_str());
		}
	} else {
		const std::vector<std::wstring> skyEntries = { L"AvgRowCS", L"AvgAllCS", L"PDFRowCS", L"PDFCS", L"CDFRowCS", L"CDFCS" };
		const std::vector<DirectX::XMUINT2> skyThreads = { {SH,1}, {1,1}, {SH,1}, {SW,SH}, {1,1}, {SH,1} };
		for (int i = 0; i < 6; i++) {
			YRZ::Pass* skyPass = new YRZ::Pass(dxr);
			skyPass->SRV[0].push_back(&skybox);
			skyPass->UAV.push_back({ &skyboxAvgRow,0 });
			skyPass->UAV.push_back({ &skyboxAvgAll,0 });
			skyPass->UAV.push_back({ &skyboxPDFRow,0 });
			skyPass->UAV.push_back({ &skyboxPDF,0 });
			skyPass->UAV.push_back({ &skyboxCDFRow,0 });
			skyPass->UAV.push_back({ &skyboxCDF,0 });
			YRZ::Shader skyCS1 = LoadShader(Recompile, dxr, L"skyboxPDF_" + skyEntries[i], L"hlsl\\skyboxPDF.hlsl", skyEntries[i].c_str(), L"cs_6_1", CompileOption);
			skyPass->ComputePass(skyCS1);
			dxr->OpenCommandListCS();
			skyPass->Compute(skyThreads[i].x, skyThreads[i].y, 1);
			dxr->ExecuteCommandListCS();
			delete skyPass;
			YRZ::DEB(L"sky CS {}/6", i + 1);
		}
	}

	//計算が終わったらvectorに突っ込んで返す
	std::vector<YRZ::Tex2D> t;
	t.emplace_back(skybox);
	t.emplace_back(skyboxPDFRow);
	t.emplace_back(skyboxPDF);
	t.emplace_back(skyboxCDFRow);
	t.emplace_back(skyboxCDF);

	std::vector<YRZ::Buf> b;
	b.emplace_back(skyboxSH);

	return { t,b };
}


using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Data::Json;

//モデルの読み込み(モデル個々で読込み用. 今はステージとキャラクターセットで読み込むしか対応していないので, 本関数は使用するにはメンテが必要. なので使用禁止)
#if FALSE
std::tuple<PMX::PMXModel*, YRZ::TLAS, std::vector<YRZ::BLAS>, std::vector<YRZ::Tex2D>, std::vector<YRZ::Buf>, float> LoadModel(const std::wstring& filename, YRZ::DXR* dxr)
{
	using namespace YRZ::PM::Math;

	size_t idx = filename.rfind(L"\\");
	std::wstring modelpath = idx < filename.size() ? filename.substr(0,idx+1) : L"";

	auto pmx = new PMX::PMXModel(filename.c_str());

	std::vector<YRZ::Tex2D> t;
	std::vector<YRZ::Buf> b;

	//頂点データ
	float sceneR = 5000;	//シーンの半径
	//シーンのどこかにくまなく着弾するような最低限の範囲にすれば良いと言えば良いが
	//ピッタリにしすぎると遮蔽される部分へのskyboxから生成されるライトサブパスの
	//確率密度を無視できないので物体が暗く見えがち
	std::vector<Vertex>vs(pmx->vertices.size());
	int i = 0;
	for (const auto& v : pmx->vertices) {
		vs[i].position = v.position;
		sceneR = max(sceneR, Length(v.position));
		vs[i].normal = v.normal;
		vs[i].uv = v.uv;
		i++;
	}
	YRZ::Buf vb = dxr->CreateBuf(vs.data(), sizeof(Vertex), pmx->vertices.size());
	vb.SetName((L"VertexBuffer@" + filename).c_str());

	//インデクスデータからIBを直接作る
	YRZ::Buf ib = dxr->CreateBuf(pmx->indices.data(), sizeof(UINT), pmx->indices.size());
	ib.SetName((L"IndexBuffer@" + filename).c_str());

	//設定JSON読み込み
	JsonObject jo;
	try {
		std::ifstream jsonfile(BasePath + L"config.json");
		std::wstring jsonstr((std::istreambuf_iterator<char>(jsonfile)), std::istreambuf_iterator<char>());
		jo = JsonObject::Parse(winrt::param::hstring(jsonstr));
	} catch (...) {
		MessageBox(0, L"can't open config.json", nullptr, MB_OK);
	}
	
	//デフォルトマテリアル
	JsonObject jo_defmat;
	Material defmat;
	try {
		jo_defmat = jo.GetNamedObject(L"default_material");
		defmat = GetMaterialFromJSON(jo_defmat, {});
	} catch (...) {
		MessageBox(0, L"invalid default_material in config.json", nullptr, MB_OK);
	}

	//マテリアルリストの読み込み
	std::vector<Material>preset;
	std::vector<std::wstring>presetName;
	JsonArray jo_mat;
	try {
		jo_mat = jo.GetNamedArray(L"material");
	} catch (...) {
		MessageBox(0, L"array \"material\" is not found in config.json", nullptr, MB_OK);
	}
	int nPreset = jo_mat.Size();
	preset.resize(nPreset);
	presetName.resize(nPreset);
	winrt::hstring debstr;
	for (int i = 0; i < nPreset; i++) {
		try {
			auto obj = jo_mat.GetObjectAt(i);
			debstr = obj.ToString();
			preset[i] = GetMaterialFromJSON(obj, defmat);
			presetName[i] = CorrectJsonStr(obj.GetNamedString(L"name").c_str());
		} catch (...) {
			MessageBox(0, std::format(L"invalid material {}", debstr).c_str(), nullptr, MB_OK);
		}
	}

	//変換規則の読み込み
	JsonArray jo_rule;
	try {
		jo_rule = jo.GetNamedArray(L"rule");
	} catch (...) {
		MessageBox(0, L"\"rule\" is not found in config.json", nullptr, MB_OK);
	}
	int nRule = jo_rule.Size();
	std::vector<std::vector<std::wstring>>keyword(nRule);	//キーワードリスト
	std::vector<std::wstring>keyword2preset(nRule);		//キーワードに対応するプリセット名
	for (int i = 0; i < nRule; i++) {
		try {
			auto obj = jo_rule.GetObjectAt(i);
			debstr = obj.ToString();
			auto keys = obj.GetNamedArray(L"keyword");
			for (size_t j = 0; j < keys.Size(); j++) {
				auto w = CorrectJsonStr((wchar_t*)keys.GetStringAt(j).c_str());
				keyword[i].push_back(wtolower(w));
			}
			keyword2preset[i] = CorrectJsonStr(obj.GetNamedString(L"material").c_str());
		} catch (...) {
			MessageBox(0, std::format(L"invalid rule {}", debstr).c_str(), nullptr, MB_OK);
		}
	}

	//変換規則に則ってMMDマテリアル→PBRマテリアルへ変換
	std::vector<Material> ms(pmx->materials.size());
	for (size_t i = 0; i < ms.size(); i++) {
		ms[i] = defmat;
		for (int j = 0; j < nRule; j++) {
			if (MatchName(pmx->materials[i].name, keyword[j])) {
				auto idx = std::find(presetName.begin(), presetName.end(), keyword2preset[j]);
				if (idx != presetName.end()) {
					ms[i] = preset[idx - presetName.begin()];
					YRZ::DEB(L"{}:ルール{}={}にマッチしました", pmx->materials[i].name, j, keyword2preset[j]);
					break;
				}
			}
		}

		//jsonに書けないマテリアル情報をフォロー
		auto& m = pmx->materials[i];
		ms[i].albedo = ToLinear(YRZ::PM::Math::xyz(m.diffuse));
		ms[i].alpha = m.diffuse.w;
		ms[i].texture = m.tex;
		ms[i].twosided = m.drawFlag & 1;
		//emission.xにマイナスの値が入ってる場合は「拡散色の-emission.x倍の値をemissionに入れ直す」というフラグ
		if (ms[i].emission.x < 0) {
			ms[i].emission = -ms[i].emission.x * ms[i].albedo;
		}
	}

	YRZ::Buf mb = dxr->CreateBuf(ms.data(), sizeof(Material), pmx->materials.size());
	mb.SetName((L"MaterialBuffer@" + filename).c_str());

	//面番号→マテリアル番号バッファ
	UINT faceCount = pmx->indices.size() / 3;
	std::vector<Face> fs(faceCount);
	UINT faceIndex = 0;
	i = 0;
	for (const auto& m : pmx->materials) {
		for (int j = 0; j < m.vertexCount / 3; j++) {
			fs[faceIndex].iMaterial = (int)i;
			fs[faceIndex].iLight = -1;
			faceIndex++;
		}
		i++;
	}
	YRZ::Buf fb = dxr->CreateBuf(fs.data(), sizeof(Face), faceCount);
	fb.SetName((L"FaceBuffer@" + filename).c_str());

	//ライトポリゴンバッファ
	std::vector<Light> lights;
	faceIndex = 0;
	float totalLightW = 0;
	for (int i = 0; i < pmx->materials.size(); i++) {
		if (Dot(ms[i].emission, YRZ::PM::Math::vec3(1)) > 1) {	//RGB足して輝度1より大きい発光体はNEEの対象にする
			for (int j = 0; j < pmx->materials[i].vertexCount / 3; j++) {
				//面積の計算
				DirectX::XMFLOAT3 p[3];
				for (int k = 0; k < 3; k++)
					p[k] = vs[pmx->indices[faceIndex * 3 + k]].position;
				float area = Length(Cross(p[0] - p[1], p[0] - p[2])) / 2;
				//面積*明度で全光束に比例したウェイトとする
				float weight = area * Dot(ms[i].emission, DirectX::XMFLOAT3(0.2126729, 0.7151522, 0.0721750));
				totalLightW += weight;

				fs[faceIndex].iLight = lights.size();
				lights.push_back({ ms[i].emission, (int)faceIndex, weight, 0 });	//pdfには仮に現在のウェイトを入れておく
				faceIndex++;
			}
		} else {
			faceIndex += pmx->materials[i].vertexCount / 3;
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
	YRZ::Buf lightBuf = dxr->CreateBuf(lights.data(), sizeof(Light), lights.size());
	lightBuf.SetName((L"LightBuffer@" + filename).c_str());


	//ComputeShaderで各ボーンのtransformから頂点の移動をさせる
	//頂点に対する各ボーンの影響度マップを作る
	auto skin = std::vector<Skinning>(pmx->vertices.size());
	i = 0;
	for (const auto& v : pmx->vertices) {
		for (int j = 0; j < 4; j++) {
			skin[i].iBone[j] = v.bone[j];
			skin[i].weight[j] = v.weight[j];
		}
		skin[i].sdef_c = v.sdef_c;
		skin[i].weightType = v.weightType;
		DirectX::XMFLOAT3 hdR = (v.sdef_r0 - v.sdef_r1) / 2;
		if (v.weightType == 3) {
			skin[i].weight[1] = hdR.x;
			skin[i].weight[2] = hdR.y;
			skin[i].weight[3] = hdR.z;
		}
		i++;
	}
	YRZ::Buf skinBuf = dxr->CreateBuf(skin.data(), sizeof(Skinning), pmx->vertices.size());
	skinBuf.SetName((L"SkinBuffer@" + filename).c_str());

	//ボーン行列(ポーズ)バッファ。アニメーションさせる場合、毎フレーム書き換えてGPUに送る必要アリ
	YRZ::Buf boneBuf = dxr->CreateBufCPU(nullptr, sizeof(DirectX::XMMATRIX), pmx->bones.size(), true, false);
	boneBuf.SetName((L"BoneBuffer@" + filename).c_str());

	//モーフ値バッファ... キーフレームの値から各モーフ値を取得した結果を格納する。これもアニメーションさせる場合毎フレーム更新すべし
	float dmymorph = 0;
	YRZ::Buf morphValuesBuf;
	if (pmx->morphs.size() > 0)
		morphValuesBuf = dxr->CreateBufCPU(nullptr, sizeof(float), pmx->morphs.size(), true, false);
	else {
		morphValuesBuf = dxr->CreateBufCPU(&dmymorph, sizeof(float), 1, true, false);	//モーフの個数0のモデル対策
	}
	morphValuesBuf.SetName((L"MorphValuesBuffer@" + filename).c_str());

	//どのモーフがどの頂点に影響するか？
	std::vector<std::vector<MorphItem>>morphVertexTable;
	morphVertexTable.resize(pmx->vertices.size());
	i = 0;
	for (const auto& mo : pmx->morphs) {
		if (mo.kind == 1) {
			//頂点モーフ
			PMX::PMXVertexMorphOffset* mof = (PMX::PMXVertexMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dPosition = mof->offset;
				morphVertexTable[mof->vertex].push_back(mi);
				mof++;
			}
		} else if (mo.kind == 3) {
			//UVモーフ
			PMX::PMXUVMorphOffset* mof = (PMX::PMXUVMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dUV = mof->offset;
				morphVertexTable[mof->vertex].push_back(mi);
				mof++;
			}
		}
		i++;
	}
	//テーブルを平らにする(GPUから並列処理しやすいような格好にまとめる)
	std::vector<MorphItem>morphTable;			//平らにした後のテーブル
	std::vector<MorphPointer>morphTablePointer;	//各頂点に対応するモーフについての情報はテーブルのどこに何個書いてあるか？
	morphTablePointer.resize(pmx->vertices.size());
	i = 0;
	for (const auto& v : pmx->vertices) {
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
		i++;
	}
	//以上からGPUにデータを送る。この2つのデータ(モーフ番号と変更される頂点の対応付け)はフレームごとに更新の必要はない
	YRZ::Buf morphTableBuf;
	if (morphTable.size() == 0)
		morphTable.push_back(MorphItem{ 0, {0,0,0}, {0,0,0,0} });	//モーフが1個もないモデル用ダミーデータ
	morphTableBuf = dxr->CreateBuf(morphTable.data(), sizeof(MorphItem), morphTable.size());
	morphTableBuf.SetName((L"MorphTableBuffer@" + filename).c_str());
	YRZ::Buf morphTablePointerBuf = dxr->CreateBuf(morphTablePointer.data(), sizeof(MorphPointer), morphTablePointer.size());
	morphTablePointerBuf.SetName((L"MorphTablePointerBuffer@" + filename).c_str());

	
	//スキニング結果格納用バッファ
	UINT nVertex = vb.desc().Width / vb.elemSize;
	YRZ::Buf rwvb = dxr->CreateRWBuf(sizeof(Vertex), nVertex);
	rwvb.SetName((L"SkinnedVertexBufferRW@" + filename).c_str());

		
	//BLASの作成。VertexBufferとIndexBuffer1個ずつ→1個のBLASが作られる
	std::vector<YRZ::BLAS>blas;
	blas.push_back(dxr->BuildBLAS(vb, ib));	//MMDモデルのBLAS
	D3D12_RAYTRACING_AABB fogaabb = { -10,-10,-10, 10,10,10 };
	blas.push_back(dxr->BuildBLAS(1, &fogaabb));
	blas[1].ID = 9999;	//とりあえず9999番を「フォグのID」という事にする
	blas[1].contributionToHitGroupIndex = 3;

	//TLASの作成。BLASの配列からTLASを作成。BLASのメンバをいじると変換行列などを設定できる
	YRZ::TLAS tlas = dxr->BuildTLAS(blas.size(), blas.data());

	//シェーダから使われるバッファ群を入れる
	b.emplace_back(vb);	//0
	b.emplace_back(ib);	//1
	b.emplace_back(mb);	//2
	b.emplace_back(fb);	//3
	b.emplace_back(lightBuf);	//4

	//コンピュートシェーダから使われるバッファ群を入れる
	b.emplace_back(rwvb);		//5
	b.emplace_back(skinBuf);	//6
	b.emplace_back(boneBuf);	//7
	b.emplace_back(morphValuesBuf);	//8
	b.emplace_back(morphTableBuf);	//9
	b.emplace_back(morphTablePointerBuf);	//10
	b.emplace_back(rwvb);		//11

	//テクスチャ
	for (const auto& tex : pmx->textures) {
		t.push_back(dxr->CreateTex2D((modelpath + tex).c_str()));
	}
	t.push_back(dxr->CreateTex2D(32,32,DXGI_FORMAT_R8G8B8A8_UNORM));	//テクスチャ数が0だとルートシグネチャでエラーが出るので最低保証として入れとく
	t.back().SetName((L"BlankTexture for " + filename).c_str());

	return { pmx, tlas, blas, t,b, sceneR};
}
#endif	// LoadModel 単品Version 使用禁止

//モデルの読み込み(ステージとキャラクター同時読み込み)
std::tuple<PMX::PMXModel*, PMX::PMXModel*, YRZ::TLAS, std::vector<YRZ::BLAS>,
	std::vector<YRZ::Tex2D>, std::vector<YRZ::Buf>, std::vector<YRZ::Tex2D>, std::vector<YRZ::Buf>, float>
	LoadModel(const std::wstring& filename_c, const std::wstring& filename_s, YRZ::DXR* dxr,
		const std::wstring json_filename = BasePath + L"config.json")
{
	using namespace YRZ::PM::Math;

	size_t idx = filename_c.rfind(L"\\");
	std::wstring modelpath_c = idx < filename_c.size() ? filename_c.substr(0, idx + 1) : L"";

	idx = filename_s.rfind(L"\\");
	std::wstring modelpath_s = idx < filename_s.size() ? filename_s.substr(0, idx + 1) : L"";

	auto pmx_c = new PMX::PMXModel(filename_c.c_str());
	auto pmx_s = new PMX::PMXModel(filename_s.c_str());

	std::vector<YRZ::Tex2D> tx_c;
	std::vector<YRZ::Buf> buf_c;
	std::vector<YRZ::Tex2D> tx_s;
	std::vector<YRZ::Buf> buf_s;

	//頂点データ
	float sceneR = 5000;	//シーンの半径
	//シーンのどこかにくまなく着弾するような最低限の範囲にすれば良いと言えば良いが
	//ピッタリにしすぎると遮蔽される部分へのskyboxから生成されるライトサブパスの
	//確率密度を無視できないので物体が暗く見えがち
	std::vector<Vertex>vs_c(pmx_c->vertices.size());
	int i = 0;
	//(キャラクター)
	for (const auto& v : pmx_c->vertices) {
		vs_c[i].position = v.position;
		sceneR = max(sceneR, Length(v.position));
		vs_c[i].normal = v.normal;
		vs_c[i].uv = v.uv;
		i++;
	}
	YRZ::Buf vb_c = dxr->CreateBuf(vs_c.data(), sizeof(Vertex), pmx_c->vertices.size());
	vb_c.SetName((L"VertexBuffer@" + filename_c).c_str());
	//(ステージ)
	std::vector<Vertex>vs_s(pmx_s->vertices.size());
	i = 0;
	for (const auto& v : pmx_s->vertices) {
		vs_s[i].position = v.position;
		sceneR = max(sceneR, Length(v.position));
		vs_s[i].normal = v.normal;
		vs_s[i].uv = v.uv;
		i++;
	}
	YRZ::Buf vb_s = dxr->CreateBuf(vs_s.data(), sizeof(Vertex), pmx_s->vertices.size());
	vb_s.SetName((L"VertexBuffer@" + filename_s).c_str());

	//インデクスデータからIBを直接作る
	//(キャラクター)
	YRZ::Buf ib_c = dxr->CreateBuf(pmx_c->indices.data(), sizeof(UINT), pmx_c->indices.size());
	ib_c.SetName((L"IndexBuffer@" + filename_c).c_str());
	//(ステージ)
	YRZ::Buf ib_s = dxr->CreateBuf(pmx_s->indices.data(), sizeof(UINT), pmx_s->indices.size());
	ib_s.SetName((L"IndexBuffer@" + filename_s).c_str());

	//設定JSON読み込み
	JsonObject jo;
	//デフォルトマテリアル(キャラクター)
	JsonObject jo_defmat;
	Material defmat;
	//デフォルトマテリアル(ステージ)
	JsonObject jo_defmat_st;
	Material defmat_st ;
	//マテリアルリスト
	std::vector<Material>preset;
	std::vector<std::wstring>presetName;
	JsonArray jo_mat;
	//変換規則
	JsonArray jo_rule;
	int nRule;
	std::vector<std::vector<std::wstring>>keyword;	//キーワードリスト
	std::vector<std::wstring>keyword2preset;		//キーワードに対応するプリセット名

	//設定JSON読み込み、キーワードリストを作成
	bool bret = LoadConfigJSonAndMakeKeyword(json_filename, jo,
		jo_defmat, defmat, jo_defmat_st, defmat_st, preset, presetName, jo_mat,
		jo_rule, nRule, keyword, keyword2preset);
	if (bret == false) { throw("can't load config.json!"); }

	//変換規則に則ってMMDマテリアル→PBRマテリアルへ変換
	//(キャラクター)
	std::vector<Material> ms_c(pmx_c->materials.size());
	dayoWork->m_DayoWorkData.matchPreset[chModelIdx].clear();
	dayoWork->m_DayoWorkData.matchPreset[chModelIdx].resize(pmx_c->materials.size());
	for (size_t i = 0; i < ms_c.size(); i++) {
		ms_c[i] = defmat;
		dayoWork->m_DayoWorkData.matchPreset[chModelIdx][i] = L"(default)";
		for (int j = 0; j < nRule; j++) {
			if (MatchName(pmx_c->materials[i].name, keyword[j])) {
				auto idx = std::find(presetName.begin(), presetName.end(), keyword2preset[j]);
				if (idx != presetName.end()) {
					ms_c[i] = preset[idx - presetName.begin()];
					YRZ::DEB(L"{}:ルール{}={}にマッチしました", pmx_c->materials[i].name, j, keyword2preset[j]);
					dayoWork->m_DayoWorkData.matchPreset[chModelIdx][i]=(keyword2preset[j]);
					break;
				}
			}
		}
		//jsonに書けないマテリアル情報をフォロー
		auto& m = pmx_c->materials[i];
		ms_c[i].albedo = ToLinear(YRZ::PM::Math::xyz(m.diffuse));
		ms_c[i].alpha = m.diffuse.w;
		ms_c[i].texture = m.tex;
		ms_c[i].twosided = m.drawFlag & 1;
		//emission.xにマイナスの値が入ってる場合は「拡散色の-emission.x倍の値をemissionに入れ直す」というフラグ
		if (ms_c[i].emission.x < 0) {
			ms_c[i].emission = -ms_c[i].emission.x * ms_c[i].albedo;
		}
	}
	//(ステージ)
	std::vector<Material> ms_s(pmx_s->materials.size());
	dayoWork->m_DayoWorkData.matchPreset[stModelIdx].clear();
	dayoWork->m_DayoWorkData.matchPreset[stModelIdx].resize(pmx_s->materials.size());
	for (size_t i = 0; i < ms_s.size(); i++) {
		ms_s[i] = defmat_st;
		dayoWork->m_DayoWorkData.matchPreset[stModelIdx][i] = L"(default)";
		for (int j = 0; j < nRule; j++) {
			if (MatchName(pmx_s->materials[i].name, keyword[j])) {
				auto idx = std::find(presetName.begin(), presetName.end(), keyword2preset[j]);
				if (idx != presetName.end()) {
					ms_s[i] = preset[idx - presetName.begin()];
					YRZ::DEB(L"{}:ルール{}={}にマッチしました", pmx_s->materials[i].name, j, keyword2preset[j]);
					dayoWork->m_DayoWorkData.matchPreset[stModelIdx][i] = (keyword2preset[j]);
					break;
				}
			}
		}
		//jsonに書けないマテリアル情報をフォロー
		auto& m = pmx_s->materials[i];
		ms_s[i].albedo = ToLinear(YRZ::PM::Math::xyz(m.diffuse));
		ms_s[i].alpha = m.diffuse.w;
		ms_s[i].texture = m.tex;
		ms_s[i].twosided = m.drawFlag & 1;
		//emission.xにマイナスの値が入ってる場合は「拡散色の-emission.x倍の値をemissionに入れ直す」というフラグ
		if (ms_s[i].emission.x < 0) {
			ms_s[i].emission = -ms_s[i].emission.x * ms_s[i].albedo;
		}
	}

	YRZ::Buf mb_c = dxr->CreateBuf(ms_c.data(), sizeof(Material), pmx_c->materials.size());
	mb_c.SetName((L"MaterialBuffer@" + filename_c).c_str());
	YRZ::Buf mb_s = dxr->CreateBuf(ms_s.data(), sizeof(Material), pmx_s->materials.size());
	mb_s.SetName((L"MaterialBuffer@" + filename_s).c_str());

	//面番号→マテリアル番号バッファ
	//(キャラクター)
	UINT faceCount_c = pmx_c->indices.size() / 3;
	std::vector<Face> fs_c(faceCount_c);
	UINT faceIndex_c = 0;
	i = 0;
	for (const auto& m : pmx_c->materials) {
		for (int j = 0; j < m.vertexCount / 3; j++) {
			fs_c[faceIndex_c].iMaterial = (int)i;
			fs_c[faceIndex_c].iLight = -1;
			faceIndex_c++;
		}
		i++;
	}
	YRZ::Buf fb_c = dxr->CreateBuf(fs_c.data(), sizeof(Face), faceCount_c);
	fb_c.SetName((L"FaceBuffer@" + filename_c).c_str());
	//(ステージ)
	UINT faceCount_s = pmx_s->indices.size() / 3;
	std::vector<Face> fs_s(faceCount_s);
	UINT faceIndex_s = 0;
	i = 0;
	for (const auto& m : pmx_s->materials) {
		for (int j = 0; j < m.vertexCount / 3; j++) {
			fs_s[faceIndex_s].iMaterial = (int)i;
			fs_s[faceIndex_s].iLight = -1;
			faceIndex_s++;
		}
		i++;
	}
	YRZ::Buf fb_s = dxr->CreateBuf(fs_s.data(), sizeof(Face), faceCount_s);
	fb_s.SetName((L"FaceBuffer@" + filename_s).c_str());

	//ライトポリゴンバッファ
	//(キャラクター)
	std::vector<Light> lights_c;
	faceIndex_c = 0;
	float totalLightW_c = 0;
	for (int i = 0; i < pmx_c->materials.size(); i++) {
		if (Dot(ms_c[i].emission, YRZ::PM::Math::vec3(1)) > 1) {	//RGB足して輝度1より大きい発光体はNEEの対象にする
			for (int j = 0; j < pmx_c->materials[i].vertexCount / 3; j++) {
				//面積の計算
				DirectX::XMFLOAT3 p[3];
				for (int k = 0; k < 3; k++)
					p[k] = vs_c[pmx_c->indices[faceIndex_c * 3 + k]].position;
				float area = Length(Cross(p[0] - p[1], p[0] - p[2])) / 2;
				//面積*明度で全光束に比例したウェイトとする
				float weight = area * Dot(ms_c[i].emission, DirectX::XMFLOAT3(0.2126729, 0.7151522, 0.0721750));
				totalLightW_c += weight;

				fs_c[faceIndex_c].iLight = lights_c.size();
				lights_c.push_back({ ms_c[i].emission, (int)faceIndex_c, weight, 0 });	//pdfには仮に現在のウェイトを入れておく
				faceIndex_c++;
			}
		}
		else {
			faceIndex_c += pmx_c->materials[i].vertexCount / 3;
		}
	}
	//各ライトのpdfとcdfを決定
	float lightCDF_c = 0;
	for (int i = 0; i < lights_c.size(); i++) {
		lights_c[i].pdf /= totalLightW_c;
		lightCDF_c += lights_c[i].pdf;
		lights_c[i].cdf = lightCDF_c;
	}
	lights_c.push_back({ {0,0,0}, 0, 0, 0 });	//要素数0のバッファは作れないのでダミーデータを最後に足しとく
	YRZ::Buf lightBuf_c = dxr->CreateBuf(lights_c.data(), sizeof(Light), lights_c.size());
	lightBuf_c.SetName((L"LightBuffer@" + filename_c).c_str());
	//(ステージ)
	std::vector<Light> lights_s;
	faceIndex_s = 0;
	float totalLightW_s = 0;
	for (int i = 0; i < pmx_s->materials.size(); i++) {
		if (Dot(ms_s[i].emission, YRZ::PM::Math::vec3(1)) > 1) {	//RGB足して輝度1より大きい発光体はNEEの対象にする
			for (int j = 0; j < pmx_s->materials[i].vertexCount / 3; j++) {
				//面積の計算
				DirectX::XMFLOAT3 p[3];
				for (int k = 0; k < 3; k++)
					p[k] = vs_s[pmx_s->indices[faceIndex_s * 3 + k]].position;
				float area = Length(Cross(p[0] - p[1], p[0] - p[2])) / 2;
				//面積*明度で全光束に比例したウェイトとする
				float weight = area * Dot(ms_s[i].emission, DirectX::XMFLOAT3(0.2126729, 0.7151522, 0.0721750));
				totalLightW_s += weight;

				fs_s[faceIndex_s].iLight = lights_s.size();
				lights_s.push_back({ ms_s[i].emission, (int)faceIndex_s, weight, 0 });	//pdfには仮に現在のウェイトを入れておく
				faceIndex_s++;
			}
		}
		else {
			faceIndex_s += pmx_s->materials[i].vertexCount / 3;
		}
	}
	//各ライトのpdfとcdfを決定
	float lightCDF_s = 0;
	for (int i = 0; i < lights_s.size(); i++) {
		lights_s[i].pdf /= totalLightW_s;
		lightCDF_s += lights_s[i].pdf;
		lights_s[i].cdf = lightCDF_s;
	}
	lights_s.push_back({ {0,0,0}, 0, 0, 0 });	//要素数0のバッファは作れないのでダミーデータを最後に足しとく
	YRZ::Buf lightBuf_s = dxr->CreateBuf(lights_s.data(), sizeof(Light), lights_s.size());
	lightBuf_s.SetName((L"LightBuffer@" + filename_s).c_str());

	//ComputeShaderで各ボーンのtransformから頂点の移動をさせる
	//頂点に対する各ボーンの影響度マップを作る
	//(キャラクター)
	auto skin_c = std::vector<Skinning>(pmx_c->vertices.size());
	i = 0;
	for (const auto& v : pmx_c->vertices) {
		for (int j = 0; j < 4; j++) {
			skin_c[i].iBone[j] = v.bone[j];
			skin_c[i].weight[j] = v.weight[j];
		}
		skin_c[i].sdef_c = v.sdef_c;
		skin_c[i].weightType = v.weightType;
		DirectX::XMFLOAT3 hdR = (v.sdef_r0 - v.sdef_r1) / 2;
		if (v.weightType == 3) {
			skin_c[i].weight[1] = hdR.x;
			skin_c[i].weight[2] = hdR.y;
			skin_c[i].weight[3] = hdR.z;
		}
		i++;
	}
	YRZ::Buf skinBuf_c= dxr->CreateBuf(skin_c.data(), sizeof(Skinning), pmx_c->vertices.size());
	skinBuf_c.SetName((L"SkinBuffer@" + filename_c).c_str());
	//(ステージ)
	auto skin_s = std::vector<Skinning>(pmx_s->vertices.size());
	i = 0;
	for (const auto& v : pmx_s->vertices) {
		for (int j = 0; j < 4; j++) {
			skin_s[i].iBone[j] = v.bone[j];
			skin_s[i].weight[j] = v.weight[j];
		}
		skin_s[i].sdef_c = v.sdef_c;
		skin_s[i].weightType = v.weightType;
		DirectX::XMFLOAT3 hdR = (v.sdef_r0 - v.sdef_r1) / 2;
		if (v.weightType == 3) {
			skin_s[i].weight[1] = hdR.x;
			skin_s[i].weight[2] = hdR.y;
			skin_s[i].weight[3] = hdR.z;
		}
		i++;
	}
	YRZ::Buf skinBuf_s = dxr->CreateBuf(skin_s.data(), sizeof(Skinning), pmx_s->vertices.size());
	skinBuf_s.SetName((L"SkinBuffer@" + filename_s).c_str());

	//ボーン行列(ポーズ)バッファ。アニメーションさせる場合、毎フレーム書き換えてGPUに送る必要アリ
	//(キャラクター)
	YRZ::Buf boneBuf_c = dxr->CreateBufCPU(nullptr, sizeof(DirectX::XMMATRIX), pmx_c->bones.size(), true, false);
	boneBuf_c.SetName((L"BoneBuffer@" + filename_c).c_str());
	//(ステージ)
	YRZ::Buf boneBuf_s = dxr->CreateBufCPU(nullptr, sizeof(DirectX::XMMATRIX), pmx_s->bones.size(), true, false);
	boneBuf_s.SetName((L"BoneBuffer@" + filename_s).c_str());

	//モーフ値バッファ... キーフレームの値から各モーフ値を取得した結果を格納する。これもアニメーションさせる場合毎フレーム更新すべし
	//(キャラクター)
	float dmymorph_c = 0;
	YRZ::Buf morphValuesBuf_c;
	if (pmx_c->morphs.size() > 0)
		morphValuesBuf_c = dxr->CreateBufCPU(nullptr, sizeof(float), pmx_c->morphs.size(), true, false);
	else {
		morphValuesBuf_c = dxr->CreateBufCPU(&dmymorph_c, sizeof(float), 1, true, false);	//モーフの個数0のモデル対策
	}
	morphValuesBuf_c.SetName((L"MorphValuesBuffer@" + filename_c).c_str());
	//(ステージ)
	float dmymorph_s = 0;
	YRZ::Buf morphValuesBuf_s;
	if (pmx_s->morphs.size() > 0)
		morphValuesBuf_s = dxr->CreateBufCPU(nullptr, sizeof(float), pmx_s->morphs.size(), true, false);
	else {
		morphValuesBuf_s = dxr->CreateBufCPU(&dmymorph_s, sizeof(float), 1, true, false);	//モーフの個数0のモデル対策
	}
	morphValuesBuf_s.SetName((L"MorphValuesBuffer@" + filename_s).c_str());
	
	//どのモーフがどの頂点に影響するか？
	//(キャラクター)
	std::vector<std::vector<MorphItem>>morphVertexTable_c;
	morphVertexTable_c.resize(pmx_c->vertices.size());
	i = 0;
	for (const auto& mo : pmx_c->morphs) {
		if (mo.kind == 1) {
			//頂点モーフ
			PMX::PMXVertexMorphOffset* mof = (PMX::PMXVertexMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dPosition = mof->offset;
				morphVertexTable_c[mof->vertex].push_back(mi);
				mof++;
			}
		}
		else if (mo.kind == 3) {
			//UVモーフ
			PMX::PMXUVMorphOffset* mof = (PMX::PMXUVMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dUV = mof->offset;
				morphVertexTable_c[mof->vertex].push_back(mi);
				mof++;
			}
		}
		i++;
	}
	//(ステージ)
	std::vector<std::vector<MorphItem>>morphVertexTable_s;
	morphVertexTable_s.resize(pmx_s->vertices.size());
	i = 0;
	for (const auto& mo : pmx_s->morphs) {
		if (mo.kind == 1) {
			//頂点モーフ
			PMX::PMXVertexMorphOffset* mof = (PMX::PMXVertexMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dPosition = mof->offset;
				morphVertexTable_s[mof->vertex].push_back(mi);
				mof++;
			}
		}
		else if (mo.kind == 3) {
			//UVモーフ
			PMX::PMXUVMorphOffset* mof = (PMX::PMXUVMorphOffset*)mo.offsets;
			for (int j = 0; j < mo.offsetCount; j++) {
				MorphItem mi = {};
				mi.iMorph = i;
				mi.dUV = mof->offset;
				morphVertexTable_s[mof->vertex].push_back(mi);
				mof++;
			}
		}
		i++;
	}

	//テーブルを平らにする(GPUから並列処理しやすいような格好にまとめる)
	//(キャラクター)
	std::vector<MorphItem>morphTable_c;				//平らにした後のテーブル
	std::vector<MorphPointer>morphTablePointer_c;	//各頂点に対応するモーフについての情報はテーブルのどこに何個書いてあるか？
	morphTablePointer_c.resize(pmx_c->vertices.size());
	i = 0;
	for (const auto& v : pmx_c->vertices) {
		//ポインタ作成
		MorphPointer mp = {};
		int count = morphVertexTable_c[i].size();
		if (count == 0) {
			mp.where = -1;	//この頂点を動かすようなモーフが無い場合は-1を入れておく
			mp.count = 0;
			morphTablePointer_c[i] = mp;
		}
		else {
			mp.where = (int)morphTable_c.size();	//この頂点を動かすモーフについての情報は、morphTableの何番目から書かれているか？
			mp.count = count;				//この頂点を動かすモーフは何個あるか？
			morphTablePointer_c[i] = mp;
		}
		//MorphItemのコピー
		for (int j = 0; j < count; j++) {
			//LOG(L"vertex %.4d #%.2d <- morph %.3d(%s)", i, j, morphVertexTable[i][j].iMorph, pmx->m_mos[morphVertexTable[i][j].iMorph].name);
			morphTable_c.push_back(morphVertexTable_c[i][j]);
		}
		i++;
	}
	//(ステージ)
	std::vector<MorphItem>morphTable_s;				//平らにした後のテーブル
	std::vector<MorphPointer>morphTablePointer_s;	//各頂点に対応するモーフについての情報はテーブルのどこに何個書いてあるか？
	morphTablePointer_s.resize(pmx_s->vertices.size());
	i = 0;
	for (const auto& v : pmx_s->vertices) {
		//ポインタ作成
		MorphPointer mp = {};
		int count = morphVertexTable_s[i].size();
		if (count == 0) {
			mp.where = -1;	//この頂点を動かすようなモーフが無い場合は-1を入れておく
			mp.count = 0;
			morphTablePointer_s[i] = mp;
		}
		else {
			mp.where = (int)morphTable_s.size();	//この頂点を動かすモーフについての情報は、morphTableの何番目から書かれているか？
			mp.count = count;				//この頂点を動かすモーフは何個あるか？
			morphTablePointer_s[i] = mp;
		}
		//MorphItemのコピー
		for (int j = 0; j < count; j++) {
			//LOG(L"vertex %.4d #%.2d <- morph %.3d(%s)", i, j, morphVertexTable[i][j].iMorph, pmx->m_mos[morphVertexTable[i][j].iMorph].name);
			morphTable_s.push_back(morphVertexTable_s[i][j]);
		}
		i++;
	}

	//以上からGPUにデータを送る。この2つのデータ(モーフ番号と変更される頂点の対応付け)はフレームごとに更新の必要はない
	//(キャラクター)
	YRZ::Buf morphTableBuf_c;
	if (morphTable_c.size() == 0)
		morphTable_c.push_back(MorphItem{ 0, {0,0,0}, {0,0,0,0} });	//モーフが1個もないモデル用ダミーデータ
	morphTableBuf_c = dxr->CreateBuf(morphTable_c.data(), sizeof(MorphItem), morphTable_c.size());
	morphTableBuf_c.SetName((L"MorphTableBuffer@" + filename_c).c_str());
	YRZ::Buf morphTablePointerBuf_c = dxr->CreateBuf(morphTablePointer_c.data(), sizeof(MorphPointer), morphTablePointer_c.size());
	morphTablePointerBuf_c.SetName((L"MorphTablePointerBuffer@" + filename_c).c_str());
	//(ステージ)
	YRZ::Buf morphTableBuf_s;
	if (morphTable_s.size() == 0)
		morphTable_s.push_back(MorphItem{ 0, {0,0,0}, {0,0,0,0} });	//モーフが1個もないモデル用ダミーデータ
	morphTableBuf_s = dxr->CreateBuf(morphTable_s.data(), sizeof(MorphItem), morphTable_s.size());
	morphTableBuf_s.SetName((L"MorphTableBuffer@" + filename_s).c_str());
	YRZ::Buf morphTablePointerBuf_s = dxr->CreateBuf(morphTablePointer_s.data(), sizeof(MorphPointer), morphTablePointer_s.size());
	morphTablePointerBuf_s.SetName((L"MorphTablePointerBuffer@" + filename_s).c_str());

	//スキニング結果格納用バッファ
	//(キャラクター)
	UINT nVertex_c = vb_c.desc().Width / vb_c.elemSize;
	YRZ::Buf rwvb_c = dxr->CreateRWBuf(sizeof(Vertex), nVertex_c);
	rwvb_c.SetName((L"SkinnedVertexBufferRW@" + filename_c).c_str());
	//(ステージ)
	UINT nVertex_s = vb_s.desc().Width / vb_s.elemSize;
	YRZ::Buf rwvb_s = dxr->CreateRWBuf(sizeof(Vertex), nVertex_s);
	rwvb_s.SetName((L"SkinnedVertexBufferRW@" + filename_s).c_str());

	//BLASの作成。VertexBufferとIndexBuffer1個ずつ→1個のBLASが作られる
	std::vector<YRZ::BLAS>blas;
	blas.push_back(dxr->BuildBLAS(vb_c, ib_c));	//MMDキャラクターモデルのBLAS
	blas.push_back(dxr->BuildBLAS(vb_s, ib_s));	//MMDステージモデルのBLAS
	D3D12_RAYTRACING_AABB fogaabb = { -10,-10,-10, 10,10,10 };
	blas.push_back(dxr->BuildBLAS(1, &fogaabb));

	blas[0].ID = 100;	//MMDキャラクターモデルのBLASを100番する
	blas[1].ID = 200;	//MMDステージモデルのBLASを200番する
	blas[2].ID = 9999;	//とりあえず9999番を「フォグのID」という事にする
	blas[2].contributionToHitGroupIndex = 3;

	//TLASの作成。BLASの配列からTLASを作成。BLASのメンバをいじると変換行列などを設定できる
	YRZ::TLAS tlas = dxr->BuildTLAS(blas.size(), blas.data());

	// キャラクター バッファ
	//シェーダから使われるバッファ群を入れる
	buf_c.emplace_back(vb_c);		//0	頂点バッファ
	buf_c.emplace_back(ib_c);		//1	頂点インデックスバッファ
	buf_c.emplace_back(mb_c);		//2	マテリアルバッファ
	buf_c.emplace_back(fb_c);		//3 面番号⇒マテリアル番号バッファ
	buf_c.emplace_back(lightBuf_c);	//4 ライトポリゴンバッファ
	//コンピュートシェーダから使われるバッファ群を入れる
	buf_c.emplace_back(rwvb_c);		//5 スキニング結果格納用バッファ
	buf_c.emplace_back(skinBuf_c);	//6 頂点に対する各ボーンの影響度マップ
	buf_c.emplace_back(boneBuf_c);	//7 ボーン行列(ポーズ)バッファ
	buf_c.emplace_back(morphValuesBuf_c);	//8 モーフ値バッファ
	buf_c.emplace_back(morphTableBuf_c);	//9 モーフテーブル
	buf_c.emplace_back(morphTablePointerBuf_c);	//10 モーフテーブルポインタ
	buf_c.emplace_back(rwvb_c);		//11 

	// ステージ バッファ
	//シェーダから使われるバッファ群を入れる
	buf_s.emplace_back(vb_s);		//0	頂点バッファ
	buf_s.emplace_back(ib_s);		//1	頂点インデックスバッファ
	buf_s.emplace_back(mb_s);		//2	マテリアルバッファ
	buf_s.emplace_back(fb_s);		//3 面番号⇒マテリアル番号バッファ
	buf_s.emplace_back(lightBuf_s);	//4 ライトポリゴンバッファ
	//コンピュートシェーダから使われるバッファ群を入れる
	buf_s.emplace_back(rwvb_s);		//5 スキニング結果格納用バッファ
	buf_s.emplace_back(skinBuf_s);	//6 頂点に対する各ボーンの影響度マップ
	buf_s.emplace_back(boneBuf_s);	//7 ボーン行列(ポーズ)バッファ
	buf_s.emplace_back(morphValuesBuf_s);	//8 モーフ値バッファ
	buf_s.emplace_back(morphTableBuf_s);	//9 モーフテーブル
	buf_s.emplace_back(morphTablePointerBuf_s);	//10 モーフテーブルポインタ
	buf_s.emplace_back(rwvb_s);		//11 

	//キャラクター テクスチャ
	for (const auto& tex : pmx_c->textures) {
		tx_c.push_back(dxr->CreateTex2D((modelpath_c + tex).c_str()));
	}
	tx_c.push_back(dxr->CreateTex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM));	//テクスチャ数が0だとルートシグネチャでエラーが出るので最低保証として入れとく
	tx_c.back().SetName((L"BlankTexture for " + filename_c).c_str());

	//ステージ テクスチャ
	for (const auto& tex : pmx_s->textures) {
		tx_s.push_back(dxr->CreateTex2D((modelpath_s + tex).c_str()));
	}
	tx_s.push_back(dxr->CreateTex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM));	//テクスチャ数が0だとルートシグネチャでエラーが出るので最低保証として入れとく
	tx_s.back().SetName((L"BlankTexture for " + filename_s).c_str());

	return { pmx_c, pmx_s, tlas, blas, tx_c, buf_c, tx_s, buf_s, sceneR };
}


//シェーダのコンパイルとパスの作成(モデル個々で読込み用. 今はステージとキャラクターセットで読み込むしか対応していないので, 本関数は使用するにはメンテが必要. なので使用禁止)
#if FALSE
std::vector<YRZ::Pass*> CreatePasses(YRZ::DXR* dxr,
	YRZ::TLAS& tlas, YRZ::CB& cb, YRZ::Tex2D& aperture, D3D12_STATIC_SAMPLER_DESC& samp, YRZ::Tex2D& dxrOut, YRZ::Tex2D& dxrFlare, YRZ::Tex2D& historyTex,
	std::vector<YRZ::Tex2D>& modelTex, std::vector<YRZ::Buf>& modelBuf, std::vector<YRZ::Tex2D>& skyboxTex, std::vector<YRZ::Buf>& skyboxBuf,
	YRZ::Buf& oidnBuf)
{
	int W = dxr->Width(), H = dxr->Height();

	//シェーダのコンパイルは一度でいいけど、パスはリソースオブジェクト再作成の度に作りなおす必要がある
	//シェーダのコンパイルはこんな感じでRayGenとかClosestHitとか1つ毎にCompileShader回して各コンパイル済みシェーダのblobにする

	const wchar_t* shadernames[] = { L"hlsl\\pmx_simple.hlsl", L"hlsl\\pmx_pt.hlsl", L"hlsl\\pmx.hlsl" };
	const wchar_t* pmx_shader = shadernames[ShaderMode];

	const wchar_t* cachenames[] = { L"pmx_simple_", L"pmx_pt_", L"pmx_bdpt_" };
	std::wstring cachename = cachenames[ShaderMode];

	auto raygenBlob = LoadShader(Recompile, dxr, cachename + L"RTMain", pmx_shader, L"RayGeneration", L"lib_6_3", CompileOption);

	//↑のシェーダとリソースでレイトレーシングレンダリングパスを定義
	auto pm = new YRZ::Pass(dxr);
	pm->SRV[0].push_back(&tlas);			//t0 TLAS
	pm->SRV[0].push_back(&modelBuf[11]);	//t1 変形後のVB
	pm->SRV[0].push_back(&modelBuf[1]);	//t2 IndexBuffer
	pm->SRV[0].push_back(&modelBuf[2]);	//t3 マテリアルバッファ
	pm->SRV[0].push_back(&modelBuf[3]);	//t4 面番号→マテリアル番号テーブル
	pm->SRV[0].push_back(&modelBuf[4]);	//t5 ライトバッファ
	pm->SRV[0].push_back(&skyboxTex[0]);	//t6 skyboxテクスチャ
	pm->SRV[0].push_back(&aperture);		//t7 絞りテクスチャ
	pm->SRV[0].push_back(&skyboxTex[1]);	//t8 skyboxのpdf(行方向)
	pm->SRV[0].push_back(&skyboxTex[2]);	//t9 skyboxのpdf(列方向)
	pm->SRV[0].push_back(&skyboxTex[3]);	//t10 skyboxのcdf(行方向)
	pm->SRV[0].push_back(&skyboxTex[4]);	//t11 skyboxのcdf(列方向)
	pm->SRV[0].push_back(&skyboxBuf[0]);	//t12 skyboxのSH
	pm->CBV.push_back(&cb);			//b0 定数バッファ
	pm->Samplers.push_back(samp);		//s0 サンプラー
	for (int i = 0; i < modelTex.size(); i++)
		pm->SRV[1].push_back(&modelTex[i]);	//モデル用テクスチャ群は t0～n, space1 で使う
	pm->UAV.push_back({ &dxrOut,0 });		//u0 基本出力用
	pm->UAV.push_back({ &dxrFlare,0 });		//u1 レンズフレア出力用
	pm->RaytracingPass(raygenBlob,
		{ L"Miss", L"MissSSS", L"MissShadow" },
		{
			YRZ::HitGroup(L"ClosestHit", L"AnyHit"),
			YRZ::HitGroup(L"ClosestHitSSS",L"AnyHitSSS"),
			YRZ::HitGroup(L"ClosestHitShadow",L"AnyHitShadow"),
			YRZ::HitGroup(L"ClosestHitFog", L"", L"IntersectFog")
		},
		{}, sizeof(float) * 32, sizeof(DirectX::XMFLOAT2), 4);


	//スキニング用コンピュートパス。vmdのデータを元にポーズ付け
	auto pc = new YRZ::Pass(dxr);
	pc->SRV[0].push_back(&modelBuf[0]);	//t0 元のVB
	pc->SRV[0].push_back(&modelBuf[6]);	//t1 ボーン番号とウェイトの配列
	pc->SRV[0].push_back(&modelBuf[7]);	//t2 ボーンごとの姿勢の配列
	pc->SRV[0].push_back(&modelBuf[8]);	//t3 モーフ値
	pc->SRV[0].push_back(&modelBuf[9]);	//t4 モーフ番号と位置・UVへの影響を書いたテーブル
	pc->SRV[0].push_back(&modelBuf[10]);	//t5 各頂点が↑のテーブルのどこを読めばいいのか書いた物
	pc->UAV.push_back({ &modelBuf[11],0 });	//u0 変形後VB(出力用)
	auto csBlob = LoadShader(Recompile, dxr, L"ComputeShader", L"hlsl\\ComputeShader.hlsl", L"CS", L"cs_6_1", CompileOption);
	pc->ComputePass(csBlob);


	//auxパス(OIDNへの入力用にcolor, albedo, normalを出力させる)
	auto auxraygenBlob = LoadShader(Recompile, dxr, L"oidnRT", L"hlsl\\oidnRT.hlsl", L"RayGeneration", L"lib_6_3", CompileOption);
	auto px = new YRZ::Pass(dxr);
	px->SRV[0].push_back(&tlas);			//t0 TLAS
	px->SRV[0].push_back(&modelBuf[11]);	//t1 変形後のVB
	px->SRV[0].push_back(&modelBuf[1]);	//t2 IndexBuffer
	px->SRV[0].push_back(&modelBuf[2]);	//t3 マテリアルバッファ
	px->SRV[0].push_back(&modelBuf[3]);	//t4 面番号→マテリアル番号テーブル
	px->SRV[0].push_back(&skyboxTex[0]);	//t5 skyboxテクスチャ
	px->SRV[0].push_back(&historyTex);	//t6 アキュムレーションの結果(OIDNのColor入力としてくっつける)
	px->CBV.push_back(&cb);			//b0 定数バッファ
	px->Samplers.push_back(samp);		//s0 サンプラー
	for (int i = 0; i < modelTex.size(); i++)
		px->SRV[1].push_back(&modelTex[i]);	//テクスチャ群は t0～n, space1 で使う
	px->UAV.push_back({ &oidnBuf,0 });
	px->RaytracingPass(auxraygenBlob, { L"Miss" }, { YRZ::HitGroup(L"ClosestHit", L"AnyHit")}, {}, sizeof(float) * 9, sizeof(DirectX::XMFLOAT2), 1);

	return {pm, pc, px};
}
#endif	// CreatePasses 単品Version　使用禁止

//シェーダのコンパイルとパスの作成(ステージとキャラクター同時）
std::vector<YRZ::Pass*> CreatePasses(YRZ::DXR* dxr,
	YRZ::TLAS& tlas, YRZ::CB& cb, YRZ::Tex2D& aperture, D3D12_STATIC_SAMPLER_DESC& samp,
	YRZ::Tex2D& dxrOut, YRZ::Tex2D& dxrFlare, YRZ::Tex2D& hdrRT,
	std::vector<YRZ::Tex2D>& modelTex_c, std::vector<YRZ::Buf>& modelBuf_c,
	std::vector<YRZ::Tex2D>& modelTex_s, std::vector<YRZ::Buf>& modelBuf_s,
	std::vector<YRZ::Tex2D>& skyboxTex, std::vector<YRZ::Buf>& skyboxBuf,
	YRZ::Buf& oidnBuf)
{
	int W = dxr->Width(), H = dxr->Height();

	//シェーダのコンパイルは一度でいいけど、パスはリソースオブジェクト再作成の度に作りなおす必要がある
	//シェーダのコンパイルはこんな感じでRayGenとかClosestHitとか1つ毎にCompileShader回して各コンパイル済みシェーダのblobにする

	const wchar_t* shadernames[] = { L"hlsl\\pmx_simple.hlsl", L"hlsl\\pmx_pt.hlsl", L"hlsl\\pmx.hlsl" };
	const wchar_t* pmx_shader = shadernames[ShaderMode];

	const wchar_t* cachenames[] = { L"pmx_simple_", L"pmx_pt_", L"pmx_bdpt_" };
	std::wstring cachename = cachenames[ShaderMode];

	auto raygenBlob = LoadShader(Recompile, dxr, cachename + L"RTMain", pmx_shader, L"RayGeneration", L"lib_6_3", CompileOption);

	//↑のシェーダとリソースでレイトレーシングレンダリングパスを定義
	auto pm = new YRZ::Pass(dxr);
	//キャラクター
	pm->SRV[0].push_back(&tlas);			//t0 TLAS
	pm->SRV[0].push_back(&modelBuf_c[11]);	//t1 変形後のVB
	pm->SRV[0].push_back(&modelBuf_c[1]);	//t2 IndexBuffer
	pm->SRV[0].push_back(&modelBuf_c[2]);	//t3 マテリアルバッファ
	pm->SRV[0].push_back(&modelBuf_c[3]);	//t4 面番号→マテリアル番号テーブル
	pm->SRV[0].push_back(&modelBuf_c[4]);	//t5 ライトバッファ
	pm->SRV[0].push_back(&skyboxTex[0]);	//t6 skyboxテクスチャ
	pm->SRV[0].push_back(&aperture);		//t7 絞りテクスチャ
	pm->SRV[0].push_back(&skyboxTex[1]);	//t8 skyboxのpdf(行方向)
	pm->SRV[0].push_back(&skyboxTex[2]);	//t9 skyboxのpdf(列方向)
	pm->SRV[0].push_back(&skyboxTex[3]);	//t10 skyboxのcdf(行方向)
	pm->SRV[0].push_back(&skyboxTex[4]);	//t11 skyboxのcdf(列方向)
	pm->SRV[0].push_back(&skyboxBuf[0]);	//t12 skyboxのSH
	for (int i = 0; i < modelTex_c.size(); i++)
		pm->SRV[1].push_back(&modelTex_c[i]);	//モデル用テクスチャ群は t0～n, space1 で使う
	//ステージ
	pm->SRV[2].push_back(&tlas);			//t0 TLAS
	pm->SRV[2].push_back(&modelBuf_s[11]);	//t1 変形後のVB
	pm->SRV[2].push_back(&modelBuf_s[1]);	//t2 IndexBuffer
	pm->SRV[2].push_back(&modelBuf_s[2]);	//t3 マテリアルバッファ
	pm->SRV[2].push_back(&modelBuf_s[3]);	//t4 面番号→マテリアル番号テーブル
	pm->SRV[2].push_back(&modelBuf_s[4]);	//t5 ライトバッファ
	pm->SRV[2].push_back(&skyboxTex[0]);	//t6 skyboxテクスチャ
	pm->SRV[2].push_back(&aperture);		//t7 絞りテクスチャ
	pm->SRV[2].push_back(&skyboxTex[1]);	//t8 skyboxのpdf(行方向)
	pm->SRV[2].push_back(&skyboxTex[2]);	//t9 skyboxのpdf(列方向)
	pm->SRV[2].push_back(&skyboxTex[3]);	//t10 skyboxのcdf(行方向)
	pm->SRV[2].push_back(&skyboxTex[4]);	//t11 skyboxのcdf(列方向)
	pm->SRV[2].push_back(&skyboxBuf[0]);	//t12 skyboxのSH
	for (int i = 0; i < modelTex_s.size(); i++)
		pm->SRV[3].push_back(&modelTex_s[i]);	//モデル用テクスチャ群は t0～n, space3 で使う
	pm->CBV.push_back(&cb);					//b0 定数バッファ
	pm->Samplers.push_back(samp);			//s0 サンプラー
	pm->UAV.push_back({ &dxrOut,0 });		//u0 基本出力用
	pm->UAV.push_back({ &dxrFlare,0 });		//u1 レンズフレア出力用
	pm->RaytracingPass(raygenBlob,
		{ L"Miss", L"MissSSS", L"MissShadow" },
		{
			YRZ::HitGroup(L"ClosestHit", L"AnyHit"),
			YRZ::HitGroup(L"ClosestHitSSS",L"AnyHitSSS"),
			YRZ::HitGroup(L"ClosestHitShadow",L"AnyHitShadow"),
			YRZ::HitGroup(L"ClosestHitFog", L"", L"IntersectFog")
		},
		{}, sizeof(float) * 32, sizeof(DirectX::XMFLOAT2), 4);


	//スキニング用コンピュートパス。vmdのデータを元にポーズ付け
	//キャラクター
	auto pc_c = new YRZ::Pass(dxr);
	pc_c->SRV[0].push_back(&modelBuf_c[0]);	//t0 元のVB
	pc_c->SRV[0].push_back(&modelBuf_c[6]);	//t1 ボーン番号とウェイトの配列
	pc_c->SRV[0].push_back(&modelBuf_c[7]);	//t2 ボーンごとの姿勢の配列
	pc_c->SRV[0].push_back(&modelBuf_c[8]);	//t3 モーフ値
	pc_c->SRV[0].push_back(&modelBuf_c[9]);	//t4 モーフ番号と位置・UVへの影響を書いたテーブル
	pc_c->SRV[0].push_back(&modelBuf_c[10]);	//t5 各頂点が↑のテーブルのどこを読めばいいのか書いた物
	pc_c->UAV.push_back({ &modelBuf_c[11],0 });	//u0 変形後VB(出力用)
	auto csBlob_c = LoadShader(Recompile, dxr, L"ComputeShader", L"hlsl\\ComputeShader.hlsl", L"CS", L"cs_6_1", CompileOption);
	pc_c->ComputePass(csBlob_c);
	//ステージ
	auto pc_s = new YRZ::Pass(dxr);
	pc_s->SRV[0].push_back(&modelBuf_s[0]);	//t0 元のVB
	pc_s->SRV[0].push_back(&modelBuf_s[6]);	//t1 ボーン番号とウェイトの配列
	pc_s->SRV[0].push_back(&modelBuf_s[7]);	//t2 ボーンごとの姿勢の配列
	pc_s->SRV[0].push_back(&modelBuf_s[8]);	//t3 モーフ値
	pc_s->SRV[0].push_back(&modelBuf_s[9]);	//t4 モーフ番号と位置・UVへの影響を書いたテーブル
	pc_s->SRV[0].push_back(&modelBuf_s[10]);	//t5 各頂点が↑のテーブルのどこを読めばいいのか書いた物
	pc_s->UAV.push_back({ &modelBuf_s[11],0 });	//u0 変形後VB(出力用)
	auto csBlob_s = LoadShader(Recompile, dxr, L"ComputeShader", L"hlsl\\ComputeShader.hlsl", L"CS", L"cs_6_1", CompileOption);
	pc_s->ComputePass(csBlob_s);

	//auxパス(OIDNへの入力用にcolor, albedo, normalを出力させる)
	auto auxraygenBlob = LoadShader(Recompile, dxr, L"oidnRT", L"hlsl\\oidnRT.hlsl", L"RayGeneration", L"lib_6_3", CompileOption);
	auto px = new YRZ::Pass(dxr);
	//キャラクター
	px->SRV[0].push_back(&tlas);			//t0 TLAS
	px->SRV[0].push_back(&modelBuf_c[11]);	//t1 変形後のVB
	px->SRV[0].push_back(&modelBuf_c[1]);	//t2 IndexBuffer
	px->SRV[0].push_back(&modelBuf_c[2]);	//t3 マテリアルバッファ
	px->SRV[0].push_back(&modelBuf_c[3]);	//t4 面番号→マテリアル番号テーブル
	px->SRV[0].push_back(&skyboxTex[0]);	//t5 skyboxテクスチャ
	px->SRV[0].push_back(&hdrRT);			//t6 アキュムレーションの結果(OIDNのColor入力としてくっつける)
	for (int i = 0; i < modelTex_c.size(); i++)
		px->SRV[1].push_back(&modelTex_c[i]);	//テクスチャ群は t0～n, space1 で使う
	//ステージ
	px->SRV[2].push_back(&tlas);			//t0 TLAS
	px->SRV[2].push_back(&modelBuf_s[11]);	//t1 変形後のVB
	px->SRV[2].push_back(&modelBuf_s[1]);	//t2 IndexBuffer
	px->SRV[2].push_back(&modelBuf_s[2]);	//t3 マテリアルバッファ
	px->SRV[2].push_back(&modelBuf_s[3]);	//t4 面番号→マテリアル番号テーブル
	px->SRV[2].push_back(&skyboxTex[0]);	//t5 skyboxテクスチャ
	px->SRV[2].push_back(&hdrRT);			//t6 アキュムレーションの結果(OIDNのColor入力としてくっつける)
	for (int i = 0; i < modelTex_s.size(); i++)
		px->SRV[3].push_back(&modelTex_s[i]);	//テクスチャ群は t0～n, space1 で使う
	px->CBV.push_back(&cb);					//b0 定数バッファ
	px->Samplers.push_back(samp);			//s0 サンプラー
	px->UAV.push_back({ &oidnBuf,0 });
	px->RaytracingPass(auxraygenBlob, { L"Miss" }, { YRZ::HitGroup(L"ClosestHit", L"AnyHit") }, {}, sizeof(float) * 9, sizeof(DirectX::XMFLOAT2), 1);

	return { pm, pc_c, pc_s, px };
}

//シェーダのパスの再作成(モデル個々で読込み用. 今はステージとキャラクターセットで読み込むしか対応していないので, 本関数は使用するにはメンテが必要. なので使用禁止)
#if FALSE
void ReCreatePasses(std::vector<YRZ::Pass*>& passes, YRZ::DXR* dxr,
	YRZ::TLAS& tlas, YRZ::CB& cb, YRZ::Tex2D& aperture, D3D12_STATIC_SAMPLER_DESC& samp, YRZ::Tex2D& dxrOut, YRZ::Tex2D& dxrFlare, YRZ::Tex2D& historyTex,
	std::vector<YRZ::Tex2D>& modelTex, std::vector<YRZ::Buf>& modelBuf, std::vector<YRZ::Tex2D>& skyboxTex, std::vector<YRZ::Buf>& skyboxBuf,
	YRZ::Buf& oidnBuf)
{
	for (auto& p : passes)
		delete p;

	passes = CreatePasses(dxr, tlas, cb, aperture, samp, dxrOut, dxrFlare,
		historyTex, modelTex, modelBuf, skyboxTex, skyboxBuf,
		oidnBuf);
}
#endif	// ReCreatePasses 単品Version　使用禁止

//シェーダのパスの再作成(ステージとキャラクター同時）
void ReCreatePasses(std::vector<YRZ::Pass*>& passes, YRZ::DXR* dxr,
	YRZ::TLAS& tlas, YRZ::CB& cb, YRZ::Tex2D& aperture, D3D12_STATIC_SAMPLER_DESC& samp, 
	YRZ::Tex2D& dxrOut, YRZ::Tex2D& dxrFlare, YRZ::Tex2D& hdrRT,
	std::vector<YRZ::Tex2D>& modelTex_c, std::vector<YRZ::Buf>& modelBuf_c,
	std::vector<YRZ::Tex2D>& modelTex_s, std::vector<YRZ::Buf>& modelBuf_s,
	std::vector<YRZ::Tex2D>& skyboxTex, std::vector<YRZ::Buf>& skyboxBuf,
	YRZ::Buf& oidnBuf)
{
	for (auto& p : passes)
		delete p;
	
	passes = CreatePasses(dxr, tlas, cb, aperture, samp, dxrOut, dxrFlare, hdrRT,
		modelTex_c, modelBuf_c, modelTex_s, modelBuf_s,
		skyboxTex, skyboxBuf, oidnBuf);
}


//リサイズされたら作り直さないといけないリソース(=ウィンドウの大きさと一緒である事が前提のリソース)を作成する
std::tuple<std::vector<YRZ::Buf>, std::vector<YRZ::Tex2D>> CreateSizeDependentResource(YRZ::DXR *dxr,
	OIDNDevice& odev, OIDNBuffer& oidnIn, OIDNBuffer& oidnOut, OIDNFilter& filter,OIDNQuality qualiy, 
	HANDLE& hOidn1, HANDLE& hOidn2)
{
	int W = dxr->Width();
	int H = dxr->Height();

	std::vector<YRZ::Buf> b;
	std::vector<YRZ::Tex2D> t;

	//レンダリング結果等を格納するテクスチャの再作成
	// Raytracingステージ用テクスチャ
	t.push_back(dxr->CreateRWTex2D(W,H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[0].SetName(L"dxrOut");				// Raytracingメインレンダリング結果格納用テクスチャ
	t.push_back(dxr->CreateRWTex2D(W,H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[1].SetName(L"dxrFlare");				// レンズフレア？？？
	// Post pixel shaderステージ用テクスチャ
	t.push_back(dxr->CreateRT2D(W, H, dxr->BackBufferFormat()));
	t[2].SetName(L"ppOut");					// ポストプロセス結果格納用テクスチャ
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[3].SetName(L"accRT");					// アキュムレーション(累積)結果格納用テクスチャ. historyTexとセットで使用する
	t.push_back(dxr->CreateRWTex2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[4].SetName(L"historyTex");			// アキュムレーション(累積)結果格納用テクスチャ. accRTとセットで使用する
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[5].SetName(L"hdrRT");					// HDR最終結果格納用テクスチャ
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[6].SetName(L"bloom_firstPassRT");		// Bloom用 firstPass格納テクスチャ
	t.push_back(dxr->CreateRT2D(W >> 1,H >> 1, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[7].SetName(L"bloom_ScnSampXRT");		// Bloom用 X方向操作納テクスチャ
	t.push_back(dxr->CreateRT2D(W >> 1,H >> 1, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[8].SetName(L"bloom_ScnSampYRT");		// Bloom用 Y方向操作格納テクスチャ
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[9].SetName(L"hdr_Post_RT");			// HDR ポストエフェクト適用後最終結果格納テクスチャ
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[10].SetName(L"ppOut_Fxaa");			// ポストプロセス FXAA結果格納用テクスチャ
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[11].SetName(L"hdrFullSizeRT");		// HDRポストプロセス用 カラー 中間RTバッファテクスチャ
	t.push_back(dxr->CreateRT2D(W, H, DXGI_FORMAT_R32G32B32A32_FLOAT));
	t[12].SetName(L"hdrFullSizeTex");		// HDRポストプロセス用 カラー 中間RWバッファテクスチャ
	t.push_back(dxr->CreateRWTex2D(W, H, DXGI_FORMAT_R32_FLOAT));
	t[13].SetName(L"hdrFullSizeDepth");		// HDRポストプロセス用 深度 中間バッファテクスチャ

	// <<テクスチャバッファのデータフロー>>
	// (dxrOut,dxrFlare)->(accRT)->(historyTex)-┳-------------->┳(hdrRT)->┳>(bloom_ScnSampXRT)->(bloom_ScnSampYRT)->┳>①
	//											┗>[oidnOutBuf]->┛			┗>(bloom_firstPassRT)-------------------->┛
	//
	// 	①->(hdr_Post_RT)->②->(ppOut_Fxaa)->(ppOut)->③->[backBuffer]
	//

	// <<パスの接続フロー>>
	// (pm.Render)-->(pa.Render)->Copy(historyTex,accRT)->┳>Copy(hdrRT, historyTex)-------------┳>①
	//													  ┗>[(px.Render)->(oidnExecuteFilter)]->┛
	//
	//	①->(pbloom_fist.Render)->(pbloom_passSx.Render)->(pbloom_passSy.Render)->(pbloom_passX.Render)->(pbloom_passY_Mix.Render)->②
	//
	//	②->(pfxaa.Render)->(pp.Render)->③->(telop.Render)
	//

	//OIDNバッファの再作成
	if (hOidn1 != 0)
		CloseHandle(hOidn1);
	if (hOidn2 != 0)
		CloseHandle(hOidn2);
	std::wstring shareNameIn = std::format(L"YRZIn{}", timeGetTime());	//なんかCloseHandleしても共有リソース名を毎回変えないと怒られるので
	std::wstring shareNameOut = std::format(L"YRZOut{}", timeGetTime());
	b.push_back(dxr->CreateRWBuf(sizeof(OIDNInput), W * H, D3D12_HEAP_FLAG_SHARED));
	b[0].SetName(L"oidnInputBuf");
	b.push_back(dxr->CreateRWBuf(12, W * H, D3D12_HEAP_FLAG_SHARED));
	b[1].SetName(L"oidnOutputBuf");
	YRZ::ThrowIfFailed(dxr->Device()->CreateSharedHandle(b[0].res.Get(), nullptr, GENERIC_ALL, shareNameIn.c_str(), &hOidn1), L"CreateSharedHandle failed", __FILEW__, __LINE__);
	YRZ::ThrowIfFailed(dxr->Device()->CreateSharedHandle(b[1].res.Get(), nullptr, GENERIC_ALL, shareNameOut.c_str(), &hOidn2), L"CreateSharedHandle failed", __FILEW__, __LINE__);

	auto oidnFlags = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32;

	if (oidnIn != nullptr)
		oidnReleaseBuffer(oidnIn);
	if (oidnOut != nullptr)
		oidnReleaseBuffer(oidnOut);
	oidnIn = oidnNewSharedBufferFromWin32Handle(odev, oidnFlags, 0, shareNameIn.c_str(), W * H * 36);
	oidnOut = oidnNewSharedBufferFromWin32Handle(odev, oidnFlags, 0, shareNameOut.c_str(), W * H * 12);
	
	if (filter != nullptr)
		oidnReleaseFilter(filter);
	filter = oidnNewFilter(odev, "RT");
	oidnSetFilterImage(filter, "color", oidnIn, OIDN_FORMAT_FLOAT3, W, H, 0, 36, W * 36);
	oidnSetFilterImage(filter, "albedo", oidnIn, OIDN_FORMAT_FLOAT3, W, H, 12, 36, W * 36);	//albedoとnormalはDoFがきつい時はコメントアウトした方がいいかも
	oidnSetFilterImage(filter, "normal", oidnIn, OIDN_FORMAT_FLOAT3, W, H, 24, 36, W * 36);
	oidnSetFilterImage(filter, "output", oidnOut, OIDN_FORMAT_FLOAT3, W, H, 0, 12, W * 12);
	oidnSetFilterBool(filter, "hdr", true); 
	oidnSetFilterInt(filter, "quality", qualiy);
	//oidnSetFilterInt(filter, "quality", OIDN_QUALITY_BALANCED);	// バランスクォリティ(OIDN_QUALITY_BALANCED)
	//oidnSetFilterInt(filter, "quality", OIDN_QUALITY_HIGH);		// 最高クォリティ(OIDN_QUALITY_HIGH)
	oidnCommitFilter(filter);

	const char* errorMessage;
	if (oidnGetDeviceError(odev, &errorMessage) != OIDN_ERROR_NONE)
		YRZ::DEBA("{}", errorMessage);

	return { b,t };
}


//ファイルダイアログの表示
bool FileDialog(std::wstring& filename, const std::vector<std::wstring>& filter, const std::wstring& initialDir, bool isSaveMode = false)
{
	OPENFILENAME ofn = {};
	wchar_t buf[MAX_PATH+8] = {};	//念のため+8
	
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = buf;

	if (isSaveMode == false) {
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
	}
	else {
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_EXPLORER | OFN_NOCHANGEDIR;
	}
	ofn.lpstrInitialDir = initialDir.c_str();

	int len = 0;
	for (const auto& f : filter)
		len += f.size()+1;
	len++;

	wchar_t* fb = new wchar_t[len];
	int idx = 0;
	for (const auto& f : filter) {
		memcpy(&fb[idx], f.data(), f.size() * sizeof(wchar_t));
		idx += f.size();
		fb[idx] = L'\0';
		idx++;
	}
	fb[idx] = L'\0';

	ofn.lpstrFilter = fb;	//= L"全てのファイル (*.*)\0*.*\0"; //拡張子フィルター
	ofn.nFilterIndex = 0;	//フィルターの初期値
	ofn.nMaxFile = MAX_PATH;

	bool ret;
	if (isSaveMode == false) {
		ret = GetOpenFileName(&ofn);
	}
	else {
		ret = GetSaveFileName(&ofn);
	}
	delete[] fb;
	filename = buf;
	return ret;
}


//パス名をフルパスにする
std::wstring MakeFullPath(const std::wstring& filename)
{
	//ドライブ名が入ってる場合はフルパスか来てる
	if ( filename.size() >= 2  &&  filename[1] == L':' )
		return filename;

	//\から始まってる場合はフルパスが来てる
	if (filename[0] == L'\\')
		return filename;

	//それ以外の場合はBaseDirからの相対パス
	auto o = std::filesystem::canonical(std::filesystem::path(BasePath + filename));
	return o.wstring();
}

// 拡張子を付与する関数
std::wstring AddExtensionIfNeeded(const std::wstring& filePath, const std::wstring& extension) {
	// ファイル名を取得
	std::wstring fileName = std::filesystem::path(filePath).filename();
	std::wstring fileFullPathName = filePath;

	// ファイル名に拡張子が含まれていない場合、拡張子を付与
	if (fileName.find(L'.') == std::wstring::npos) {
		fileFullPathName += extension;
	}

	return fileFullPathName;
}


// プロダクトバージョンを文字列にする関数
std::wstring GetProductVersionString() {
	
	// 自身のEXEファイル名を取得
	wchar_t filePath[MAX_PATH];
	DWORD length = GetModuleFileName(nullptr, filePath, MAX_PATH);
	if (length == 0) {
		// Error handling
		return L"";
	}

	DWORD dummy;
	DWORD versionInfoSize = GetFileVersionInfoSize(filePath, &dummy);
	if (versionInfoSize == 0) {
		// Error handling
		return L"";
	}

	std::vector<BYTE> versionInfoBuffer(versionInfoSize);
	if (!GetFileVersionInfo(filePath, 0, versionInfoSize, versionInfoBuffer.data())) {
		// Error handling
		return L"";
	}

	VS_FIXEDFILEINFO* fileInfo;
	UINT fileInfoSize;
	if (!VerQueryValue(versionInfoBuffer.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &fileInfoSize)) {
		// Error handling
		return L"";
	}

	WORD majorVersion = HIWORD(fileInfo->dwProductVersionMS);
	WORD minorVersion = LOWORD(fileInfo->dwProductVersionMS);
	WORD buildNumber = HIWORD(fileInfo->dwProductVersionLS);
	WORD revisionNumber = LOWORD(fileInfo->dwProductVersionLS);

	// Construct the version string
	std::wstring versionString = std::to_wstring(majorVersion) + L"." +
		std::to_wstring(minorVersion) + L"." +
		std::to_wstring(buildNumber) + L"." +
		std::to_wstring(revisionNumber);

	return versionString;
}

/* グローバル変数(static) */
// 各種フラグ
static BOOL bMotionLoop = TRUE;						   // モーションループフラグ
static BOOL bAutoFocus = TRUE;						   // オートフォーカス有効フラグ
static BOOL bBloomEnable = TRUE;					   // Bloom有効フラグ
static BOOL bFxaaEnable = TRUE;						   // FXAA有効フラグ
static BOOL bUserComSEffect = FALSE;				   // ユーザーComputeシェーダー有効フラグ
static BOOL bUserPostEffect = FALSE;				   // ユーザーポストエフェクト有効フラグ
static BOOL bDrawDepthEnable = FALSE;				   // 深度バッファ表示有効フラグ
// 動画書き出し関連変数 グローバル変数
static BOOL bActive_GDI_MovCap = FALSE;                // GDIでの動画キャプチャ 開始フラグ
static BOOL bResizeOK = FALSE;						   // ウィンドウリサイズ完了フラグ
static LONG accumCounter = 0;						   // 累積カウンタ
static LONG frameCounter = 0;                          // 動画フレームNo
static LONG endframeCounter = 0;                       // 動画終了フレームNo
static GdiCapture gdiCapture;                          // GDIキャプチャクラス
static AVIFileWriter aviFileWriter;                    // AVIファイル書き出しクラス
static BITMAPINFOHEADER bitMapInfoHeader;              // AVIファイル書き出し用BitMapInfoHeader
// サブウィンドウ
static AVISettingWindow aviSettingWindow;			   // AVIファイル書き出し設定ウィンドウ
static EnvSettingWindow envSettingWindow;			   // 色調調整ウィンドウ
static MotionSettingWindow motionSettingWindow;		   // モーション設定ウィンドウ
static WindowSettingWindow windowSettingWindow;		   // ウィンドウサイズ設定ウィンドウ

void Dayo(HINSTANCE hInstance)
{
	using namespace YRZ::PM::Math;
	using YRZ::PM::Math::PI;

	//vector<const wchar_t*> CompileOption = { L"-I", srcpath, L"-HV", L"2021" };

	//ウィンドウのクライアント領域の幅と高さ
	int W = 1600, H = 900;

	//カメラなどの変数
	float CameraDistance = 50;						// 目標点とカメラの距離(目標点がカメラ前面でマイナス)
	float CameraTheta = PI * 0.5;					// Rx回転角度;
	float CameraPhi = PI * 0.5;						// Ry回転角度
	float CameraPsi = PI * 0.25;					// Rz回転角度
	float CameraDX = 0, CameraDY = 0;				// カメラ平行移動(マウス操作)
	float CameraDTheta = 0, CameraDPhi = 0;			// カメラ回転角度(マウス操作)
	float CameraDistanceDz = 0;						// 目標点とカメラの距離(マウス操作)
	float FovD = 0;									// 視野角(キーボード操作)
	DirectX::XMFLOAT3 CameraTarget = { 0,10,0 };	// カメラの注視点
	float skyboxPhi = 0;							// skyboxの回転角

	//よろずアプリオブジェクトの作成(メインウィンドウの作成)
	std::wstring app_title = std::format(L"{}{}", L"MikuMikuDayo V", GetProductVersionString());
	YRZ::App* app = new YRZ::App(app_title.c_str(), W, H, hInstance);
	DragAcceptFiles(app->hWnd(), TRUE);

	// メニューID
	constexpr DWORD ID_MENU_FILE = 103;
	constexpr DWORD ID_MENU_FILE_LOADPRJ = 104;
	constexpr DWORD ID_MENU_FILE_SAVEPRJ = 105;
	constexpr DWORD ID_MENU_FILE_SEP0 = 106;
	constexpr DWORD ID_MENU_FILE_SKYBOX = 107;
	constexpr DWORD ID_MENU_FILE_CHARACTOR = 108;
	constexpr DWORD ID_MENU_FILE_CH_MODEL = 109;
	constexpr DWORD ID_MENU_FILE_CH_VMD = 110;
	constexpr DWORD ID_MENU_FILE_STAGE = 111;
	constexpr DWORD ID_MENU_FILE_ST_MODEL = 112;
	constexpr DWORD ID_MENU_FILE_ST_VMD = 113;
	constexpr DWORD ID_MENU_FILE_CAM = 114;
	constexpr DWORD ID_MENU_FILE_SEP1 = 115;
	constexpr DWORD ID_MENU_FILE_EXIT = 116;

	constexpr DWORD ID_MENU_VIEW = 200;
	constexpr DWORD ID_MENU_VIEW_DENOISE = 201;
	constexpr DWORD ID_MENU_VIEW_SPECTRAL = 202;
	constexpr DWORD ID_MENU_VIEW_SHADOW = 203;
	constexpr DWORD ID_MENU_VIEW_SEP1 = 204;
	constexpr DWORD ID_MENU_VIEW_SHADER_0 = 210;
	constexpr DWORD ID_MENU_VIEW_SHADER_1 = 211;
	constexpr DWORD ID_MENU_VIEW_SHADER_2 = 212;
	constexpr DWORD ID_MENU_VIEW_SEP2 = 213;
	constexpr DWORD ID_MENU_VIEW_ENV = 220;
	constexpr DWORD ID_MENU_VIEW_WORK = 221;

	constexpr DWORD ID_MENU_MOTION = 300;
	constexpr DWORD ID_MENU_MOTION_START = 301;
	constexpr DWORD ID_MENU_MOTION_STOP = 302;
	constexpr DWORD ID_MENU_MOTION_JUMP = 303;
	constexpr DWORD ID_MENU_MOTION_SEP1 = 304;
	constexpr DWORD ID_MENU_MOTION_LOOP = 305;
	constexpr DWORD ID_MENU_MOTION_SEP2 = 306;
	constexpr DWORD ID_MENU_MOTION_RESET = 307;

	constexpr DWORD ID_MENU_EFFECT = 400;
	constexpr DWORD ID_MENU_EFFECT_DOF = 401;
	constexpr DWORD ID_MENU_EFFECT_AUTOFOCUS = 402;
	constexpr DWORD ID_MENU_EFFECT_BLOOM = 403;
	constexpr DWORD ID_MENU_EFFECT_FXAA = 404;
	constexpr DWORD ID_MENU_EFFECT_FOG = 405;
	constexpr DWORD ID_MENU_EFFECT_SEP1 = 406;
	constexpr DWORD ID_MENU_EFFECT_USERCOM = 407;
	constexpr DWORD ID_MENU_EFFECT_USERPS = 408;
	constexpr DWORD ID_MENU_EFFECT_SEP2 = 409;
	constexpr DWORD ID_MENU_EFFECT_DRAWDEPTH = 410;

	constexpr DWORD ID_MENU_CAPTURE = 500;
	constexpr DWORD ID_MENU_CAPTURE_PNG = 501;
	constexpr DWORD ID_MENU_CAPTURE_AVI = 502;
	constexpr DWORD ID_MENU_CAPTURE_SEP1 = 503;
	constexpr DWORD ID_MENU_CAPTURE_WINDOWSIZE = 504;

	//メニュー追加
	HMENU hMenuTop = CreateMenu();
	HMENU hMenuFile = CreatePopupMenu();
	HMENU hMenuFileCharactor = CreatePopupMenu();
	HMENU hMenuFileStage = CreatePopupMenu();
	HMENU hMenuView = CreatePopupMenu();
	HMENU hMenuMotion = CreatePopupMenu();
	HMENU hMenuEffect = CreatePopupMenu();
	HMENU hMenuCapture = CreatePopupMenu();

	MENUITEMINFO mii = {};
	mii.cbSize = sizeof(mii);

	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_FILE;
	mii.hSubMenu = hMenuFile;
	WCHAR M1[] = L"File(&F)";
	mii.dwTypeData = M1;
	InsertMenuItem(hMenuTop, ID_MENU_FILE, false, &mii);

	mii.wID = ID_MENU_FILE_LOADPRJ;
	mii.fMask = MIIM_ID | MIIM_STRING;
	WCHAR M1_1[] = L"Load Project...(&L)";
	mii.dwTypeData = M1_1;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_LOADPRJ, false, &mii);

	mii.wID = ID_MENU_FILE_SAVEPRJ;
	mii.fMask = MIIM_ID | MIIM_STRING;
	WCHAR M1_2[] = L"Save Project(&W)";
	mii.dwTypeData = M1_2;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_SAVEPRJ, false, &mii);

	mii.wID = ID_MENU_FILE_SEP0;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_SEP0, false, &mii);

	mii.wID = ID_MENU_FILE_SKYBOX;
	mii.fMask = MIIM_ID | MIIM_STRING;
	WCHAR M2[] = L"Load skybox HDRI...(&B)";
	mii.dwTypeData = M2;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_SKYBOX, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_FILE_CHARACTOR;
	mii.hSubMenu = hMenuFileCharactor;
	WCHAR M3[] = L"Load Charactor(&C)";
	mii.dwTypeData = M3;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_CHARACTOR, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_FILE_CH_MODEL;
	WCHAR M3_1[] = L"Load PMX model...(&P)";
	mii.dwTypeData = M3_1;
	InsertMenuItem(hMenuFileCharactor, ID_MENU_FILE_CH_MODEL, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_FILE_CH_VMD;
	WCHAR M3_2[] = L"Load VMD motion...(&V)";
	mii.dwTypeData = M3_2;
	InsertMenuItem(hMenuFileCharactor, ID_MENU_FILE_CH_VMD, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_FILE_STAGE;
	mii.hSubMenu = hMenuFileStage;
	WCHAR M4[] = L"Load Stage(&S)";
	mii.dwTypeData = M4;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_STAGE, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_FILE_ST_MODEL;
	WCHAR M4_1[] = L"Load PMX model...(&P)";
	mii.dwTypeData = M4_1;
	InsertMenuItem(hMenuFileStage, ID_MENU_FILE_ST_MODEL, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_FILE_ST_VMD;
	WCHAR M4_2[] = L"Load VMD motion...(&V)";
	mii.dwTypeData = M4_2;
	InsertMenuItem(hMenuFileStage, ID_MENU_FILE_ST_VMD, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_FILE_CAM;
	WCHAR M5[] = L"Load VMD camera...(&V)";
	mii.dwTypeData = M5;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_CAM, false, &mii);

	mii.wID = ID_MENU_FILE_SEP1;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_SEP1, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_FILE_EXIT;
	WCHAR M6[] = L"Exit(&X)";
	mii.dwTypeData = M6;
	InsertMenuItem(hMenuFile, ID_MENU_FILE_EXIT, false, &mii);

	//表示メニュー
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_VIEW;
	mii.hSubMenu = hMenuView;
	WCHAR M10[] = L"View(&V)";
	mii.dwTypeData = M10;
	InsertMenuItem(hMenuTop, ID_MENU_VIEW, false, &mii);

	mii.wID = ID_MENU_VIEW_DENOISE;
	WCHAR M11[] = L"denoise(&D)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M11;
	mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_DENOISE, false, &mii);

	mii.wID = ID_MENU_VIEW_SPECTRAL;
	WCHAR M12[] = L"spectral rendering(&S)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M12;
	mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SPECTRAL, false, &mii);

	mii.wID = ID_MENU_VIEW_SHADOW;
	WCHAR M13[] = L"shadow rendering(&H)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M13;
	mii.fState = MFS_CHECKED;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SHADOW, false, &mii);

	mii.wID = ID_MENU_VIEW_SEP1;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SEP1, false, &mii);

	mii.wID = ID_MENU_VIEW_SHADER_0;
	WCHAR M14[] = L"preview shader(&0)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
	mii.dwTypeData = M14;
	mii.fState = MFS_CHECKED;
	mii.fType = MFT_RADIOCHECK;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SHADER_0, false, &mii);

	mii.wID = ID_MENU_VIEW_SHADER_1;
	WCHAR M15[] = L"pathtracing shader(&1)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
	mii.dwTypeData = M15;
	mii.fState = MFS_UNCHECKED;
	mii.fType = MFT_RADIOCHECK;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SHADER_1, false, &mii);

	mii.wID = ID_MENU_VIEW_SHADER_2;
	WCHAR M16[] = L"bidirectional pathtracing shader(&2)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
	mii.dwTypeData = M16;
	mii.fState = MFS_UNCHECKED;
	mii.fType = MFT_RADIOCHECK;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SHADER_2, false, &mii);

	mii.wID = ID_MENU_VIEW_SEP2;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_SEP2, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_VIEW_WORK;
	WCHAR M17[] = L"view workspace...(&W)";
	mii.dwTypeData = M17;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_WORK, false, &mii);

	mii.fMask = MIIM_ID | MIIM_STRING;
	mii.wID = ID_MENU_VIEW_ENV;
	WCHAR M18[] = L"environment setting...(&E)";
	mii.dwTypeData = M18;
	InsertMenuItem(hMenuView, ID_MENU_VIEW_ENV, false, &mii);

	//モーションメニュー
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_MOTION;
	mii.hSubMenu = hMenuMotion;
	WCHAR M20[] = L"Motion(&M)";
	mii.dwTypeData = M20;
	InsertMenuItem(hMenuTop, ID_MENU_MOTION, false, &mii);

	mii.wID = ID_MENU_MOTION_START;
	WCHAR M21[] = L"Start(&B)";
	mii.fMask = MIIM_ID | MIIM_STRING /* | MIIM_STATE*/;
	mii.dwTypeData = M21;
	//mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_START, false, &mii);

	mii.wID = ID_MENU_MOTION_STOP;
	WCHAR M22[] = L"Stop(&E)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
	mii.dwTypeData = M22;
	mii.fState = MFS_CHECKED;
	mii.fType = MFT_RADIOCHECK;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_STOP, false, &mii);

	mii.wID = ID_MENU_MOTION_JUMP;
	WCHAR MJUMP[] = L"Jump to...(&J)";
	mii.fMask = MIIM_ID | MIIM_STRING /* | MIIM_STATE*/;
	mii.dwTypeData = MJUMP;
	//mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_JUMP, false, &mii);

	mii.wID = ID_MENU_MOTION_SEP1;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_SEP1, false, &mii);

	mii.wID = ID_MENU_MOTION_LOOP;
	WCHAR M23[] = L"motion loop(&L)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M23;
	mii.fState = MFS_CHECKED;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_LOOP, false, &mii);

	mii.wID = ID_MENU_MOTION_SEP2;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_SEP2, false, &mii);

	mii.wID = ID_MENU_MOTION_RESET;
	WCHAR M24[] = L"Reset(&R)";
	mii.fMask = MIIM_ID | MIIM_STRING /* | MIIM_STATE*/;
	mii.dwTypeData = M24;
	//mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuMotion, ID_MENU_MOTION_RESET, false, &mii);

	//エフェクトメニュー
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_EFFECT;
	mii.hSubMenu = hMenuEffect;
	WCHAR M30[] = L"Effect(&E)";
	mii.dwTypeData = M30;
	InsertMenuItem(hMenuTop, ID_MENU_EFFECT, false, &mii);
	
	mii.wID = ID_MENU_EFFECT_DOF;
	WCHAR M31[] = L"dof(&D)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M31;
	mii.fState = MFS_CHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_DOF, false, &mii);

	mii.wID = ID_MENU_EFFECT_AUTOFOCUS;
	WCHAR M32[] = L"autofocus(&A)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M32;
	mii.fState = MFS_CHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_AUTOFOCUS, false, &mii);

	mii.wID = ID_MENU_EFFECT_BLOOM;
	WCHAR M33[] = L"bloom(&B)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M33;
	mii.fState = MFS_CHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_BLOOM, false, &mii);

	mii.wID = ID_MENU_EFFECT_FXAA;
	WCHAR M34[] = L"fxaa(&X)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M34;
	mii.fState = MFS_CHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_FXAA, false, &mii);

	mii.wID = ID_MENU_EFFECT_FOG;
	WCHAR M35[] = L"fog(&F)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M35;
	mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_FOG, false, &mii);

	mii.wID = ID_MENU_EFFECT_SEP1;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_SEP1, false, &mii);

	mii.wID = ID_MENU_EFFECT_USERCOM;
	WCHAR M36[] = L"user compute shader(&C)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M36;
	mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_USERCOM, false, &mii);

	mii.wID = ID_MENU_EFFECT_USERPS;
	WCHAR M37[] = L"user pixel shader(&P)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M37;
	mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_USERPS, false, &mii);

	mii.wID = ID_MENU_EFFECT_SEP2;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_SEP2, false, &mii);

	mii.wID = ID_MENU_EFFECT_DRAWDEPTH;
	WCHAR M38[] = L"view depth(&V)";
	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
	mii.dwTypeData = M38;
	mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuEffect, ID_MENU_EFFECT_DRAWDEPTH, false, &mii);
	

	mii.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
	mii.wID = ID_MENU_CAPTURE;
	mii.hSubMenu = hMenuCapture;
	WCHAR M40[] = L"Capture(&C)";
	mii.dwTypeData = M40;
	InsertMenuItem(hMenuTop, ID_MENU_CAPTURE, false, &mii);

	mii.wID = ID_MENU_CAPTURE_PNG;
	WCHAR M41[] = L"Save as PNG(&P)";
	mii.fMask = MIIM_ID | MIIM_STRING /* | MIIM_STATE*/;
	mii.dwTypeData = M41;
	//mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuCapture, ID_MENU_CAPTURE_PNG, false, &mii);

	mii.wID = ID_MENU_CAPTURE_AVI;
	WCHAR M42[] = L"Save as AVI(&A)";
	mii.fMask = MIIM_ID | MIIM_STRING /* | MIIM_STATE*/;
	mii.dwTypeData = M42;
	//mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuCapture, ID_MENU_CAPTURE_AVI, false, &mii);

	mii.wID = ID_MENU_CAPTURE_SEP1;
	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	InsertMenuItem(hMenuCapture, ID_MENU_CAPTURE_SEP1, false, &mii);

	mii.wID = ID_MENU_CAPTURE_WINDOWSIZE;
	WCHAR M43[] = L"Set window size(&W)";
	mii.fMask = MIIM_ID | MIIM_STRING /* | MIIM_STATE*/;
	mii.dwTypeData = M43;
	//mii.fState = MFS_UNCHECKED;
	InsertMenuItem(hMenuCapture, ID_MENU_CAPTURE_WINDOWSIZE, false, &mii);

	SetMenu(app->hWnd(), hMenuTop);

	//メニュー付きのウィンドウとしてサイズを再計算する
	RECT winR = { 0,0,W,H };
	//AdjustWindowRect(&winR, WS_OVERLAPPEDWINDOW, true);	// ボーダレスウィンドウにするので調整しない
	SetWindowPos(app->hWnd(), 0, 0, 0, winR.right - winR.left, winR.bottom - winR.top, SWP_NOMOVE);

	//ホットキーの設定(なんかPrintScreenキーが押されたことを知るためのメッセージが無いので)
	RegisterHotKey(app->hWnd(), 0, 0, VK_SNAPSHOT);

	//メニューを付けてクライアント領域のサイズが決まったところでよろずDXRオブジェクトの作成
	YRZ::DXR* dxr = new YRZ::DXR(app->hWnd());
	// ワークスペースの作成
	dayoWork = new DayoWorkspace();
	
	//マテリアル以外のJSONに格納されている設定
	UINT vmdCodepage;
	std::wstring modelFilename;
	std::wstring vmdFilename;
	std::wstring stgmodelFilename;
	std::wstring stgvmdFilename;
	std::wstring vmdcamFilename;
	std::wstring skyboxFilename;
	std::wstring apertureFilename;
	{
		JsonObject jo;
		try {
			std::ifstream jsonfile(BasePath + L"config.json");
			std::wstring jsonstr((std::istreambuf_iterator<char>(jsonfile)), std::istreambuf_iterator<char>());
			jo = JsonObject::Parse(winrt::param::hstring(jsonstr));
		} catch (...) {
			MessageBox(0, L"can't open config.json", nullptr, MB_OK);
		}

		vmdCodepage = GetJson1(jo, L"vmd_codepage", 932);	//日本語環境がデフォルト
		modelFilename = MakeFullPath(GetJsonStr(jo, L"charactor_model", L""));
		vmdFilename = MakeFullPath(GetJsonStr(jo, L"charactor_vmd", L""));
		stgmodelFilename = MakeFullPath(GetJsonStr(jo, L"stage_model", L""));
		stgvmdFilename = MakeFullPath(GetJsonStr(jo, L"stage_vmd", L""));
		vmdcamFilename = MakeFullPath(GetJsonStr(jo, L"camera_vmd", L""));
		skyboxFilename = MakeFullPath(GetJsonStr(jo, L"skybox", L""));
		apertureFilename = MakeFullPath(GetJsonStr(jo, L"aperture", L""));
		dayoWork->m_DayoWorkData.m_materialJsonFileName = BasePath + L"config.json";
	}

	/*** まず、不変なオブジェクト(モデル・モーション・skyboxが変わっても変更の必要が無い)を作る ***/

	//コンスタントバッファ、中身は後で書き換える
	YRZ::CB cb = dxr->CreateCB(nullptr, sizeof(Constantan));

	//絞りテクスチャ(DoF用)
	auto aperture = dxr->CreateTex2D(apertureFilename.c_str());
	YRZ::LOG(L"Loaded aperture.png");

	//サンプラー
	auto samp = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

	//OIDNデバイス
	OIDNDevice odev = oidnNewDevice(OIDN_DEVICE_TYPE_CUDA);
	oidnCommitDevice(odev);

	//ウィンドウのリサイズの度に作り直す必要のあるリソース
	//各出力用バッファとOIDN関係
	OIDNBuffer oidnIn = nullptr, oidnOut = nullptr;
	OIDNFilter filter = nullptr;
	HANDLE hOidn1 = 0, hOidn2 = 0;
	OIDNQuality oidnQuality = OIDN_QUALITY_BALANCED;
	auto [fullBuf, fullTex] = CreateSizeDependentResource(dxr, odev, oidnIn, oidnOut, filter, oidnQuality, hOidn1, hOidn2);
	auto dxrOut = fullTex[0];
	auto dxrFlare = fullTex[1];
	auto ppOut = fullTex[2];
	auto accRT = fullTex[3];
	auto historyTex = fullTex[4];
	auto hdrRT = fullTex[5];
	auto bloom_firstPassRT = fullTex[6];
	auto bloom_ScnSampXRT = fullTex[7];
	auto bloom_ScnSampYRT = fullTex[8];
	auto hdr_Post_RT = fullTex[9];
	auto ppOut_Fxaa = fullTex[10];
	auto hdrFullSizeRT = fullTex[11];
	auto hdrFullSizeTex = fullTex[12];
	auto hdrFullSizeDepth = fullTex[13];
	auto oidnBuf = fullBuf[0];
	auto oidnOutBuf = fullBuf[1];
	fullBuf.clear();	//ここでクリアしとかないとdxrOutとかが上書きされてもメモリに残り続ける
	fullTex.clear();

	//ポストプロセス枠用VertexShader
	//auto vsBlob = dxr->CompileShader(L"hlsl\\PostProcess.hlsl", L"VS", L"vs_6_1", CompileOption);
	auto vsBlob = LoadShader(Recompile, dxr, L"PostProcessVS", L"hlsl\\PostProcess.hlsl", L"VS", L"vs_6_1", CompileOption);

	//OIDNによるデノイズ結果をhdrRTにいれるパス
	auto po = new YRZ::Pass(dxr);
	po->SRV[0].push_back(&oidnOutBuf);
	po->CBV.push_back(&cb);
	po->Samplers.push_back(samp);
	po->RTV.push_back({ &hdrRT, YRZ::CV(0,0,0,1) });
	auto psBlobOidn = LoadShader(Recompile, dxr, L"PSconvOIDN", L"hlsl\\oidn.hlsl", L"PSConvOIDN", L"ps_6_1", CompileOption);
	po->PostProcessPass(vsBlob, psBlobOidn);

	//dxrFlare,Outをクリアするパス
	auto pclear = new YRZ::Pass(dxr);
	pclear->UAV.push_back({ &dxrFlare });
	pclear->UAV.push_back({ &dxrOut });
	auto clearBlob = LoadShader(Recompile, dxr, L"RTClear", L"hlsl\\clear.hlsl", L"Clear", L"lib_6_3", CompileOption);
	pclear->RaytracingPass(clearBlob, {L"Miss"}, { YRZ::HitGroup(L"ClosestHit") }, {}, 128, 8, 4);

	//アキュムレーション。accRTに現在のフレームと過去のフレームの加重平均を出力する
	//ポストプロセスパスの場合、出力はRTVに放り込まれた物になる。RTVにいくつも放り込むとマルチレンダーターゲットになる
	//PushRTVをしなかった場合、バックバッファへ出力する
	auto pa = new YRZ::Pass(dxr);
	pa->SRV[0].push_back(&dxrOut);		//レイトレーシングの結果。ここではSRVに突っ込んでテクスチャとして読む
	pa->SRV[0].push_back(&dxrFlare);
	pa->SRV[0].push_back(&historyTex);	//RWTex2DはSRVとUAVの両方いける
	pa->CBV.push_back(&cb);
	pa->Samplers.push_back(samp);
	pa->RTV.push_back({ &accRT, YRZ::CV(0,0,0,1) });
	auto psBlob = LoadShader(Recompile, dxr, L"PSAcc", L"hlsl\\PostProcess.hlsl", L"PSAcc", L"ps_6_1", CompileOption);
	pa->PostProcessPass(vsBlob, psBlob);

	//ポストプロセスで、hdrRTに対してBloomをかけるパス
	// Firstパス
	auto pbloom_fist = new YRZ::Pass(dxr);
	pbloom_fist->SRV[0].push_back(&hdrRT);
	pbloom_fist->SRV[0].push_back(&historyTex);	//Dummy
	pbloom_fist->SRV[0].push_back(&hdrFullSizeDepth);
	pbloom_fist->CBV.push_back(&cb);
	pbloom_fist->Samplers.push_back(samp);
	pbloom_fist->RTV.push_back({ &bloom_ScnSampYRT, YRZ::CV(0,0,0,1) });
	auto psBlobBloomFirst = LoadShader(Recompile, dxr, L"PS_first", L"hlsl\\Bloom.hlsl", L"PS_first", L"ps_6_1", CompileOption);
	pbloom_fist->PostProcessPass(vsBlob, psBlobBloomFirst);
	// X方向にじみ
	auto pbloom_passSx = new YRZ::Pass(dxr);
	pbloom_passSx->SRV[0].push_back(&bloom_ScnSampYRT);
	pbloom_passSx->SRV[0].push_back(&hdrRT);	//Dummy
	pbloom_passSx->SRV[0].push_back(&hdrFullSizeDepth);
	pbloom_passSx->CBV.push_back(&cb);
	pbloom_passSx->Samplers.push_back(samp);
	pbloom_passSx->RTV.push_back({ &bloom_ScnSampXRT, YRZ::CV(0,0,0,1) });
	auto psBlobBloomSx = LoadShader(Recompile, dxr, L"PS_passSX", L"hlsl\\Bloom.hlsl", L"PS_passSX", L"ps_6_1", CompileOption);
	pbloom_passSx->PostProcessPass(vsBlob, psBlobBloomSx);
	// Y方向にじみ
	auto pbloom_passSy = new YRZ::Pass(dxr);
	pbloom_passSy->SRV[0].push_back(&bloom_ScnSampXRT);
	pbloom_passSy->SRV[0].push_back(&hdrRT);	//Dummy
	pbloom_passSy->SRV[0].push_back(&hdrFullSizeDepth);
	pbloom_passSy->CBV.push_back(&cb);
	pbloom_passSy->Samplers.push_back(samp);
	pbloom_passSy->RTV.push_back({ &bloom_ScnSampYRT, YRZ::CV(0,0,0,1) });
	auto psBlobBloomSy = LoadShader(Recompile, dxr, L"PS_passSY", L"hlsl\\Bloom.hlsl", L"PS_passSY", L"ps_6_1", CompileOption);
	pbloom_passSy->PostProcessPass(vsBlob, psBlobBloomSy);
	// X方向ぼかし
	auto pbloom_passX = new YRZ::Pass(dxr);
	pbloom_passX->SRV[0].push_back(&bloom_ScnSampYRT);
	pbloom_passX->SRV[0].push_back(&hdrRT);		//Dummy
	pbloom_passX->SRV[0].push_back(&hdrFullSizeDepth);
	pbloom_passX->CBV.push_back(&cb);
	pbloom_passX->Samplers.push_back(samp);
	pbloom_passX->RTV.push_back({ &bloom_ScnSampXRT, YRZ::CV(0,0,0,1) });
	auto psBlobBloomX = LoadShader(Recompile, dxr, L"PS_passX", L"hlsl\\Bloom.hlsl", L"PS_passX", L"ps_6_1", CompileOption);
	pbloom_passX->PostProcessPass(vsBlob, psBlobBloomX);
	// Y方向ぼかし & Mix
	auto pbloom_passY_Mix = new YRZ::Pass(dxr);
	pbloom_passY_Mix->SRV[0].push_back(&bloom_ScnSampXRT);
	pbloom_passY_Mix->SRV[0].push_back(&hdrRT);
	pbloom_passY_Mix->SRV[0].push_back(&hdrFullSizeDepth);
	pbloom_passY_Mix->CBV.push_back(&cb);
	pbloom_passY_Mix->Samplers.push_back(samp);
	pbloom_passY_Mix->RTV.push_back({ &hdr_Post_RT, YRZ::CV(0,0,0,1) });
	auto psBlobBloomY = LoadShader(Recompile, dxr, L"PS_passY", L"hlsl\\Bloom.hlsl", L"PS_passY", L"ps_6_1", CompileOption);
	pbloom_passY_Mix->PostProcessPass(vsBlob, psBlobBloomY, false);

	// ポストプロセスで、hdrRTに対してフォグをかけるパス
	auto ppFog = new YRZ::Pass(dxr);
	ppFog->SRV[0].push_back(&hdrFullSizeTex);
	ppFog->SRV[0].push_back(&hdrFullSizeDepth);
	ppFog->CBV.push_back(&cb);
	ppFog->Samplers.push_back(samp);
	ppFog->RTV.push_back({ &hdr_Post_RT });			//αブレンディングの為, レンダーターゲットをクリアしない
	auto psFog = LoadShader(Recompile, dxr, L"PSFog", L"hlsl\\Fog.hlsl", L"PSFog", L"ps_6_1", CompileOption);
	ppFog->PostProcessPass(vsBlob, psFog, true);	//αブレンディング有効(Out = Src.rgb * Src.a + Dest.rgb * (1 - Dest.a))

	// ユーザー定義ポストエフェクト
	auto ppUser = new YRZ::Pass(dxr);
	ppUser->SRV[0].push_back(&hdrFullSizeTex);
	ppUser->SRV[0].push_back(&hdrRT);
	ppUser->SRV[0].push_back(&hdrFullSizeDepth);
	ppUser->CBV.push_back(&cb);
	ppUser->Samplers.push_back(samp);
	ppUser->RTV.push_back({ &hdrFullSizeRT, YRZ::CV(0,0,0,1) });
	auto vsBlobUser = LoadShader(Recompile, dxr, L"UserPostProcessVS", L"hlsl\\Plugin02_PS.hlsl", L"VS", L"vs_6_1", CompileOption);
	auto psUser = LoadShader(Recompile, dxr, L"PSUSer", L"hlsl\\Plugin02_PS.hlsl", L"PSUSer", L"ps_6_1", CompileOption);
	ppUser->PostProcessPass(vsBlobUser, psUser);

	// ポストプロセスで、FXAAをかけるパス
	auto pfxaa = new YRZ::Pass(dxr);
	pfxaa->SRV[0].push_back(&hdr_Post_RT);
	pfxaa->CBV.push_back(&cb);
	pfxaa->Samplers.push_back(samp);
	pfxaa->RTV.push_back({ &ppOut_Fxaa, YRZ::CV(0,0,0,1) });
	auto psFxaa = LoadShader(Recompile, dxr, L"PS_Fxaa", L"hlsl\\FXAA.hlsl", L"PS_Fxaa", L"ps_6_1", CompileOption);
	pfxaa->PostProcessPass(vsBlob, psFxaa);

	//ポストプロセスで、hdr_Post_RTの中身をトーンマッピング+ガンマ変換してppOutに出力するパス
	auto pp = new YRZ::Pass(dxr);
	pp->SRV[0].push_back(&dxrOut);
	pp->SRV[0].push_back(&dxrFlare);
	pp->SRV[0].push_back(&ppOut_Fxaa);
	pp->CBV.push_back(&cb);
	pp->Samplers.push_back(samp);
	pp->RTV.push_back({ &ppOut, YRZ::CV(0,0,0,1) });
	auto psBlob3 = LoadShader(Recompile, dxr, L"PSTonemap", L"hlsl\\PostProcess.hlsl", L"PSTonemap", L"ps_6_1", CompileOption);
	pp->PostProcessPass(vsBlob, psBlob3);

	//テロップ。ポストプロセスが終わったppOutにテロップを入れてバックバッファに出力
	//現在はテロップ入れはDirect2Dで行っているのでppOut→バックバッファへ形式を変えつつコピーする役割のみ
	YRZ::Pass* telop = new YRZ::Pass(dxr);
	telop->SRV[0].push_back(&ppOut);
	telop->Samplers.push_back(samp);
	auto psTelop = LoadShader(Recompile, dxr, L"PSTelop", L"hlsl\\telop.hlsl", L"PSTelop", L"ps_6_1", CompileOption);
	telop->PostProcessPass(vsBlob, psTelop);

	// Ray深度バッファモニタ用
	YRZ::Pass* pDepthView =  new YRZ::Pass(dxr);
	pDepthView->SRV[0].push_back(&hdrFullSizeDepth);
	pDepthView->Samplers.push_back(samp);
	pDepthView->RTV.push_back({ &hdrFullSizeRT, YRZ::CV(0,0,0,1) });
	auto psDepth = LoadShader(Recompile, dxr, L"PSDepthmap", L"hlsl\\depth_view.hlsl", L"PSDepthmap", L"ps_6_1", CompileOption);
	pDepthView->PostProcessPass(vsBlob, psDepth);

	//テロップ用Direct2D
	auto d2d = YRZ::D2D(dxr);
	d2d.WrapBackBuffers();
	auto help = d2d.CreateBitmap(BASEDIR(L"help.png"));
	YRZ::LOG(L"Loaded help.png");
	auto hanko = d2d.CreateBitmap(BASEDIR(L"haru.png"));
	YRZ::LOG(L"Loaded haru.png");
	auto brush = d2d.SolidColorBrush({ 1,0.5,0.3,1 });
	auto shadowbrush = d2d.SolidColorBrush({ 0,0,0,0.5 });
	auto textformat = d2d.TextFormat(L"Meiryo", 32);
	auto textformat2 = d2d.TextFormat(L"Meiryo", 24);

	// ユーザーコンピュートシェーダー
	YRZ::Buf cs_CameraAndLight_in = dxr->CreateBufCPU(nullptr, sizeof(CameraAndLight), 1);
	YRZ::Buf cs_CameraAndLight_out = dxr->CreateRWBuf( sizeof(CameraAndLight), 1);
	YRZ::Buf cs_WorldInfomation = dxr->CreateBufCPU(nullptr, sizeof(WorldInfomation), 1);
	auto pcUser = new YRZ::Pass(dxr);
	pcUser->SRV[0].push_back(&cs_CameraAndLight_in);		//t0 元のCAL
	pcUser->SRV[0].push_back(&cs_WorldInfomation);			//t1 WI
	pcUser->UAV.push_back({ &cs_CameraAndLight_out, 0 });	//u0 更新後CAL(出力用)
	auto csCSUser = LoadShader(Recompile, dxr, L"CSUser", L"hlsl\\Plugin01_CS.hlsl", L"CS_User", L"cs_6_1", CompileOption);
	pcUser->ComputePass(csCSUser);

	// imgui ワークスペース作成
	dayoWork->SetupWorkspace(app->hWnd(), dxr->Device().Get(), dxr->BackBufferCount());

	/*** 後から違う物に替えられるオブジェクトを作る ***/

	//モデルを読み込む
	YRZ::TLAS tlas;
	std::vector<YRZ::BLAS> blas;
	std::vector<YRZ::Tex2D> modelTex[maxModelNum];
	std::vector<YRZ::Buf> modelBuf[maxModelNum];
	float sceneR;
	
	// キャラクター＆ステージモデル読み込み
	auto [pmxtmpc, pmxtmps, tlastmp, blastmp, modelTextmpc, modelBuftmpc, modelTextmps, modelBuftmps, sceneRtmps] =
	LoadModel(modelFilename.c_str(), stgmodelFilename.c_str(), dxr);
	{
		dayoWork->m_DayoWorkData.pmx[chModelIdx] = pmxtmpc;
		dayoWork->m_DayoWorkData.pmx[stModelIdx] = pmxtmps;
		tlas = tlastmp;
		blas = blastmp;
		modelTex[chModelIdx] = modelTextmpc;
		modelBuf[chModelIdx] = modelBuftmpc;
		modelTex[stModelIdx] = modelTextmps;
		modelBuf[stModelIdx] = modelBuftmps;
		sceneR = sceneRtmps;
	}
			
	//skybox読み込み
	auto [skyboxTex, skyboxBuf] = LoadSkybox(skyboxFilename.c_str(), dxr);

	// Camera VMDの定義
	PMX::VMDCam* vmd_camera = new PMX::VMDCam(vmdcamFilename.c_str(), vmdCodepage);
	PMX::CameraSolver* cam_solver = new PMX::CameraSolver(vmd_camera);
	PMX::VMDCamera camera = {};
	cam_solver->Solve(iTick, 0, &camera);

	//VMDの読み込み
	PMX::Physics* physics[maxModelNum] = { NULL,NULL };
	physics[chModelIdx] = new PMX::Physics();	//キャラクターモデル物理エンジン
	physics[stModelIdx] = new PMX::Physics();	//ステージモデル物理エンジン
	PMX::VMD* vmd[maxModelNum] = { NULL,NULL };
	vmd[chModelIdx] = new PMX::VMD(vmdFilename.c_str(), vmdCodepage);		//キャラクターモデルモーション
	vmd[stModelIdx] = new PMX::VMD(stgvmdFilename.c_str(), vmdCodepage);	//ステージモデルモーション
	PMX::PoseSolver* solver[maxModelNum] = { NULL,NULL };
	//キャラクターモデルソルバー
	solver[chModelIdx] = new PMX::PoseSolver(physics[chModelIdx], dayoWork->m_DayoWorkData.pmx[chModelIdx], vmd[chModelIdx]);
	solver[chModelIdx]->Solve(iTick);	//VMDからiTickフレーム目のポーズ(各ボーンのtransform)の取得
	physics[chModelIdx]->Prewarm(30);
	//ステージモデルソルバー
	solver[stModelIdx] = new PMX::PoseSolver(physics[stModelIdx], dayoWork->m_DayoWorkData.pmx[stModelIdx], vmd[stModelIdx]);
	solver[stModelIdx]->Solve(iTick);	//VMDからiTickフレーム目のポーズ(各ボーンのtransform)の取得
	physics[stModelIdx]->Prewarm(30);

	//ConstantBufferの中身を定義
	YRZ::Buf &vb_c = modelBuf[chModelIdx][0];
	YRZ::Buf &vb_s = modelBuf[stModelIdx][0];
	YRZ::Buf &ib_c = modelBuf[chModelIdx][1];
	YRZ::Buf &ib_s = modelBuf[stModelIdx][1];
	YRZ::Buf &lights_c = modelBuf[chModelIdx][4];
	YRZ::Buf &lights_s = modelBuf[stModelIdx][4];
	Constantan* cs;
	cs = (Constantan*)cb.pData;	//cb.pDataにデータを書き込むためのポインタが入るので、それを使ってCBの中身を操作する
	cs->resolution = DirectX::XMFLOAT2(W, H);
	cs->nLights = lights_c.desc().Width / lights_c.elemSize - 1;	//ライトポリゴン数。ダミーデータの分1つ引く
	cs->nLights_st = lights_s.desc().Width / lights_s.elemSize - 1;	//ライトポリゴン数。ダミーデータの分1つ引く
	cs->sceneRadius = sceneR;
	cs->lensR = 0.25;
	cs->pint = 20;
	cs->DofEnable = 1;
	cs->FogEnable = 0;
	cs->ShadowEnable = 1;
	cs->fov = 1 / tanf(30.0 / 2 * PI / 180);

	//モデル・skyboxに依存するパスの作成
	YRZ::Pass* pm;
	YRZ::Pass* pc[maxModelNum];
	YRZ::Pass* px;
	//キャラクター, ステージモデルパス
	auto Passes = CreatePasses(dxr,
	tlas, cb, aperture, samp, dxrOut, dxrFlare, hdrRT,
		modelTex[chModelIdx], modelBuf[chModelIdx],
		modelTex[stModelIdx], modelBuf[stModelIdx],
		skyboxTex, skyboxBuf, oidnBuf);

		pm = Passes[0];
		pc[chModelIdx] = Passes[1];
		pc[stModelIdx] = Passes[2];
		px = Passes[3];

	//オートフォーカス(焦点位置導出)用Rayシェーダー
	auto pAF = new YRZ::Pass(dxr);
	YRZ::Tex2D AFtex = dxr->CreateRWTex2D(1, 1, DXGI_FORMAT_R32_FLOAT);
	AFtex.SetName(L"AutoFocusRWTex");
	pAF->CBV.push_back(&cb);
	pAF->UAV.push_back({ &AFtex });					// 焦点位置用
	pAF->SRV[0].push_back(&tlas);
	YRZ::Shader AFRayGen = LoadShader(Recompile, dxr, L"AutoFocus", L"hlsl\\autofocus.hlsl", L"RayGeneration", L"lib_6_3", CompileOption);
	pAF->RaytracingPass(AFRayGen, { L"Miss" }, { YRZ::HitGroup(L"ClosestHit") }, {}, 32, 16, 1);

	// ポストプロセス用深度バッファ生成用Rayシェーダー
	auto pDepth = new YRZ::Pass(dxr);
	pDepth->CBV.push_back(&cb);
	pDepth->UAV.push_back({ &hdrFullSizeDepth });	// ポストプロセス用深度バッファ
	pDepth->SRV[0].push_back(&tlas);
	YRZ::Shader DepthRayGen = LoadShader(Recompile, dxr, L"DepthBuf", L"hlsl\\depth.hlsl", L"RayGeneration", L"lib_6_3", CompileOption);
	pDepth->RaytracingPass(DepthRayGen, { L"Miss" }, { YRZ::HitGroup(L"ClosestHit") }, {}, 32, 16, 1);

	//ファイル読み込みとかのラムダ式
	//ファイルドロップ時とメニューからの選択両方から使えるようにしとく

	//skybox読み込み
	auto loadSkyboxLambda = [&](const std::wstring& filename) {
		HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
		try {
			auto [_skyboxTex, _skyboxBuf] = LoadSkybox(filename, dxr);
			int idx = 0;
			skyboxTex = _skyboxTex;
			skyboxBuf = _skyboxBuf;
			pm->Update();	//使用するリソースの数・種類に変更が無く、内容だけ書き換わっている場合はこれでOK
			px->Update();
			iFrame = 0;
		} catch (std::exception err) {
			MessageBoxA(dxr->hWnd(), err.what(), nullptr, 0);
		}
		SetCursor(hcurPrev);
	};

	//モデル読み込み
	auto loadPMXLambda = [&](const std::wstring& filename, int modelIdx) {
		// 現状, 引数のfilenameは使っていない. modelFilenameとstgmodelFilenameの変数であらかじめ指定する. 要メンテ.
		HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
		try {
			//キャラクターモデルとステージモデル両方読み込み
			auto [_pmx_c, _pmx_s, _tlas, _blas, _modelTex_c, _modelBuf_c, _modelTex_s, _modelBuf_s,
				_sceneR] = LoadModel(modelFilename, stgmodelFilename, dxr,
					dayoWork->m_DayoWorkData.m_materialJsonFileName);
			delete dayoWork->m_DayoWorkData.pmx[chModelIdx];
			delete dayoWork->m_DayoWorkData.pmx[stModelIdx];
			dayoWork->m_DayoWorkData.pmx[chModelIdx] = _pmx_c;
			dayoWork->m_DayoWorkData.pmx[stModelIdx] = _pmx_s;
			tlas = _tlas;
			blas = _blas;
			modelTex[chModelIdx] = _modelTex_c;
			modelTex[stModelIdx] = _modelTex_s;
			modelBuf[chModelIdx] = _modelBuf_c;
			modelBuf[stModelIdx] = _modelBuf_s;
			sceneR = _sceneR;
			//モデルが変わると使用するテクスチャの数が変わるのでパスを作り直す
			ReCreatePasses(Passes, dxr, tlas, cb, aperture, samp, dxrOut, dxrFlare, hdrRT,
				modelTex[chModelIdx], modelBuf[chModelIdx], modelTex[stModelIdx], modelBuf[stModelIdx],
				skyboxTex, skyboxBuf, oidnBuf);

			pm = Passes[0];
			pc[chModelIdx] = Passes[1];
			pc[stModelIdx] = Passes[2];
			px = Passes[3];
			pAF->Update();		//TLASが更新されたので
			pDepth->Update();	//TLASが更新されたので
			//ライトポリゴンの数が変わるのでCBを更新する
			cs->nLights = lights_c.desc().Width / lights_c.elemSize - 1;
			cs->nLights_st = lights_s.desc().Width / lights_s.elemSize - 1;
			cs->sceneRadius = sceneR;
			//モーションの読み直し(ボーン番号との対応が変わるので)
			delete solver[chModelIdx];
			solver[chModelIdx] = new PMX::PoseSolver(physics[chModelIdx], dayoWork->m_DayoWorkData.pmx[chModelIdx], vmd[chModelIdx]);
			solver[chModelIdx]->Solve(iTick);
			physics[chModelIdx]->Reset();
			delete solver[stModelIdx];
			solver[stModelIdx] = new PMX::PoseSolver(physics[stModelIdx], dayoWork->m_DayoWorkData.pmx[stModelIdx], vmd[stModelIdx]);
			solver[stModelIdx]->Solve(iTick);
			physics[stModelIdx]->Reset();
			iFrame = 0;
		} catch (std::exception err) {
			MessageBoxA(dxr->hWnd(), err.what(), nullptr, 0);
		}
		SetCursor(hcurPrev);
	};

	//モーション読み込み
	auto loadVMDLambda = [&](const std::wstring& filename, int modelIdx) {
		HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
		try {
			iTick = 0;
			iFrame = 0;
			delete solver[modelIdx];
			delete vmd[modelIdx];
			vmd[modelIdx] = new PMX::VMD(filename.c_str(), vmdCodepage, dayoWork->m_DayoWorkData.m_fps60enable);
			solver[modelIdx] = new PMX::PoseSolver(physics[modelIdx], dayoWork->m_DayoWorkData.pmx[modelIdx], vmd[modelIdx]);
			solver[modelIdx]->Solve(iTick);
			physics[modelIdx]->Reset();
		} catch (std::exception err) {
			MessageBoxA(dxr->hWnd(), err.what(), nullptr, 0);
		}
		SetCursor(hcurPrev);
	};
	
	//カメラモーション読み込み
	auto loadVMDCameraLambda = [&](const std::wstring& filename) {
		HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
		try {
			iTick = 0;
			iFrame = 0;
			delete cam_solver;
			delete vmd_camera;
			vmd_camera = new PMX::VMDCam(filename.c_str(), vmdCodepage, dayoWork->m_DayoWorkData.m_fps60enable);
			cam_solver = new PMX::CameraSolver(vmd_camera);
			cam_solver->Solve(iTick, 0 , &camera);
		} catch (std::exception err) {
			MessageBoxA(dxr->hWnd(), err.what(), nullptr, 0);
		}
		SetCursor(hcurPrev);
	};

	//デノイズ Quality更新, リサイズされたら作り直さないといけないリソース(=ウィンドウの大きさと一緒である事が前提のリソース)の作成
	auto updateDenoiseQuality_And_CreateSizeDependentResource = [&](OIDNQuality _quality) {
		oidnQuality = _quality;
		{
			auto [b, t] = CreateSizeDependentResource(dxr, odev, oidnIn, oidnOut, filter, oidnQuality, hOidn1, hOidn2);
			dxrOut = t[0];	dxrFlare = t[1];	ppOut = t[2];	accRT = t[3];	historyTex = t[4];	hdrRT = t[5];
			bloom_firstPassRT = t[6];	bloom_ScnSampXRT = t[7];	bloom_ScnSampYRT = t[8];	hdr_Post_RT = t[9];
			ppOut_Fxaa = t[10];		hdrFullSizeRT = t[11];	hdrFullSizeTex = t[12];	hdrFullSizeDepth = t[13];
			oidnBuf = b[0];	oidnOutBuf = b[1];

			pm->Update();
			pp->Update();
			po->Update();
			pa->Update();
			px->Update();
			pbloom_fist->Update();
			pbloom_passSx->Update();
			pbloom_passSy->Update();
			pbloom_passX->Update();
			pbloom_passY_Mix->Update(false);
			pDepth->Update();
			pDepthView->Update();
			ppFog->Update(true);
			ppUser->Update();
			pclear->Update();
			pfxaa->Update();
			telop->Update();
			
			d2d.WrapBackBuffers();
		}
	};

	//色調調整, Infomationウィンドウ表示
	envSettingWindow.ShowDialog(hInstance, app->hWnd());

	// ウィンドウサイズ設定 
	SetActiveWindow(app->hWnd());
	RECT nowRect = { 0 };	GetWindowRect(app->hWnd(), &nowRect);
	MoveWindow(app->hWnd(), nowRect.left, nowRect.top, W, H, TRUE);
	
	// デノイズクォリティの設定(バランス)
	updateDenoiseQuality_And_CreateSizeDependentResource(OIDN_QUALITY_BALANCED);

	//メインループでレンダリング
	MSG msg;
	POINT prevPos = {};
	GetCursorPos(&prevPos);
	int prevSolvedFrame = iTick;
	DWORD prev_tm = timeGetTime();
	while (app->MainLoop(msg, envSettingWindow.hDlg())) {
		//移動係数、shiftで加速、ctrlで減速
		float mk = 1;
		if (GetKeyState(VK_SHIFT) & 0x8000)
			mk = 10;
		else if (GetKeyState(VK_LCONTROL) & 0x8000)
			mk = 0.1;

		//カメラむーぶとか
		if (GetForegroundWindow() == dxr->hWnd()) {
			if (msg.message == WM_HOTKEY) {
				//スクショ撮る
				time_t t = time(NULL);
				struct tm local;
				localtime_s(&local, &t);
				wchar_t buf[MAX_PATH];	//24昼修正,invalid string positionが出る人がいるとのことで128→MAX_PATHへ増やした
				wcsftime(buf, MAX_PATH, BASEDIR(L"screenshots\\SS_%Y_%m%d_%H%M%S.jpg"), &local);

				dxr->Snapshot(buf);
			}
			if (msg.message == WM_MOUSEMOVE) {
				POINT mpos;
				GetCursorPos(&mpos);
				int dx = (mpos.x - prevPos.x) * mk;
				int dy = (mpos.y - prevPos.y) * mk;
				if (app->borderless == false  && dayoWork->GetIO().WantCaptureMouse == false)
				{
					// マウスによるパン・チルト・移動は、ボーダレスウィンドウではないとき, かつimguiのウィンドウをつまんでいないときのみ
					if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
						CameraDTheta = clamp(CameraDTheta - dy * 0.0025, -0.95 * PI, 0.95 * PI);
						CameraDPhi -= dx * 0.0025;
						iFrame = 0;
					}
					else if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
						CameraDX -= 0.01 * dx;
						CameraDY += 0.01 * dy;
						iFrame = 0;
					}
				}
				prevPos = mpos;
			} else if (msg.message == WM_MOUSEWHEEL) {
				if (app->borderless == false && dayoWork->GetIO().WantCaptureMouse == false)
				{
					CameraDistanceDz -= GET_WHEEL_DELTA_WPARAM(msg.wParam) * 0.03 * mk;
					iFrame = 0;
				}
			}
			else if (msg.message == WM_KEYDOWN) {
				if (msg.wParam == VK_LEFT) {
					//前のフレーム
					if (GetKeyState(VK_SHIFT) & 0x8000)
						iTick -= 30;
					else
						iTick--;
					if (vmd[chModelIdx]->lastFrame == 0)
						iTick = 0;
					else if (iTick < 0) {
						iTick = vmd[chModelIdx]->lastFrame - ((-iTick) % vmd[chModelIdx]->lastFrame);
					}
					iFrame = 0;
				}
				else if (msg.wParam == VK_RIGHT) {
					//次のフレーム
					if (GetKeyState(VK_SHIFT) & 0x8000)
						iTick += 30;
					else
						iTick++;
					if (vmd[chModelIdx]->lastFrame == 0)
						iTick = 0;
					//ループモーションを前提にする場合、最後まで再生したら次は0Fからではなく1Fから再生するのが良いと思うので
					if (iTick > vmd[chModelIdx]->lastFrame)
						iTick = iTick % vmd[chModelIdx]->lastFrame;
					iFrame = 0;
				}
				if (msg.wParam == VK_ESCAPE)
				{
					// ESCキーで、動画書き出しを中断する
					if (bActive_GDI_MovCap)
					{
						bActive_GDI_MovCap = FALSE;
						animation = FALSE;
						iFrame = 0;
						aviFileWriter.CloseAVIFile();
						app->set_borderless(false);
						// デノイズクォリティの変更(最高->バランス)
						oidnQuality = OIDN_QUALITY_BALANCED;
						OIDNQuality nowQuality = oidnQuality;
						updateDenoiseQuality_And_CreateSizeDependentResource(nowQuality);
					}
				}
				if (msg.wParam == 'P') {
					//Aスタンスから最後にSolveした時のキーフレームの状態にジワジワと近づける事で剛体が貫通状態からスタートするのをある程度防ぐ
					//ポーズによっては却って物理がおかしくなるので、framesをなるべく増やすのが良いと思われる
					physics[chModelIdx]->Prewarm(120);
					physics[stModelIdx]->Prewarm(120);
					iFrame = 0;
				}
				if (msg.wParam == 'R') {
					//物理リセット…剛体の位置・回転をボーンアニメーションを適用しただけの状態に戻す
					solver[chModelIdx]->Solve(prevSolvedFrame);
					physics[chModelIdx]->Reset();
					solver[stModelIdx]->Solve(prevSolvedFrame);
					physics[stModelIdx]->Reset();
					iFrame = 0;
				}
				//テンキー4,6でskyboxの回転
				if (msg.wParam == VK_NUMPAD6) {
					skyboxPhi -= PI / 250 * mk;
					iFrame = 0;
				}
				if (msg.wParam == VK_NUMPAD4) {
					skyboxPhi += PI / 250 * mk;
					iFrame = 0;
				}
				//テンキー3,9でDoFの強弱
				if (msg.wParam == VK_NUMPAD3) {
					cs->lensR *= pow(0.8, mk);
					cs->lensR = max(cs->lensR, 0.001);
					iFrame = 0;
				}
				if (msg.wParam == VK_NUMPAD9) {
					cs->lensR *= pow(1.25, mk);
					cs->lensR = min(cs->lensR, 10);
					iFrame = 0;
				}
				//テンキー2,8でピント合わせ
				if (msg.wParam == VK_NUMPAD2) {
					cs->pint -= 5 * mk;
					cs->pint = max(1, cs->pint);
					iFrame = 0;
				}
				if (msg.wParam == VK_NUMPAD8) {
					cs->pint += 5 * mk;
					iFrame = 0;
				}
				//テンキー1,7で画角の変更
				if (msg.wParam == VK_NUMPAD7) {
					//float a = atanf(1.0 / FovD) / PI * 180.0;
					//a = min(a + 0.5 * mk, 89);
					//cs->fov = 1 / tanf(a * PI / 180);
					FovD = min(FovD + 0.5 * mk, 89);
					iFrame = 0;
				}
				if (msg.wParam == VK_NUMPAD1) {
					//float a = atanf(1.0 / cs->fov) / PI * 180.0;
					//a = max(a - 0.5 * mk, 1);
					//cs->fov = 1 / tanf(a * PI / 180);
					FovD = min(FovD - 0.5 * mk, 1);
					iFrame = 0;
				}
				//テンキー*でDoF,ピント,画角のリセット
				if (msg.wParam == VK_MULTIPLY) {
					cs->pint = 20;
					cs->lensR = 0.025;
					cs->fov = 1 / tanf(30.0 / 2 * PI / 180);
					iFrame = 0;
				}
				//テンキー/でオートフォーカス
				if (msg.wParam == VK_DIVIDE) {
					float z;
					dxr->Download(&z, AFtex);
					cs->pint = z;
					iFrame = 0;
				}
				//F1でヘルプ切り替え
				if (msg.wParam == VK_F1) {
					HelpMode++;
					if (HelpMode > 2)
						HelpMode = 0;
				}
			} else if (dayoWork->m_DayoWorkData.m_resetPhysics == true) {
				// Workspace imguiからの物理リセット要求
				dayoWork->m_DayoWorkData.m_resetPhysics = false;
				//物理リセット…剛体の位置・回転をボーンアニメーションを適用しただけの状態に戻す
				solver[chModelIdx]->Solve(prevSolvedFrame);
				physics[chModelIdx]->Reset();
				solver[stModelIdx]->Solve(prevSolvedFrame);
				physics[stModelIdx]->Reset();
				iFrame = 0;
			} else if (dayoWork->m_DayoWorkData.m_loadMaterialJsonReq == true) {
				// マテリアルJsonファイル読み込み
				std::wstring filename;
				dayoWork->m_DayoWorkData.m_loadMaterialJsonReq = false;
				if (FileDialog(filename, { L"MikuMikuDayo material file(*.json)",L"*.json" }, BasePath))
				{
					dayoWork->m_DayoWorkData.m_materialJsonFileName = filename;

					// マテリアルを反映させるために、モデルファイルを読み込みなおす
					loadPMXLambda(modelFilename, chModelIdx);
					loadPMXLambda(stgmodelFilename, stModelIdx);
				}
			} else if (msg.message == WM_COMMAND) {
				std::wstring filename;
				if (msg.wParam == ID_MENU_FILE_LOADPRJ) {
					//プロジェクトファイルの読み込み
					if (FileDialog(filename, { L"MikuMikuDayo Prj(*.pdayo)",L"*.pdayo" }, BasePath))
					{
						ini::IniFile dayoprj_ini;

						// 読み込み
						dayoprj_ini.load(wstring_to_utf8(filename));

						// 60FPS 設定
						dayoWork->m_DayoWorkData.m_fps60enable = dayoprj_ini["AviSetting"]["60FPSEnable"].as<bool>();

						// モデル・カメラ・スカイボックス
						modelFilename = std::wstring(utf8_to_wstring(dayoprj_ini["Charactor"]["ModelFile"].as<std::string>()));
						vmdFilename = std::wstring(utf8_to_wstring(dayoprj_ini["Charactor"]["ModelMotionFile"].as<std::string>()));
						stgmodelFilename = std::wstring(utf8_to_wstring(dayoprj_ini["Stage"]["ModelFile"].as<std::string>()));
						stgvmdFilename = std::wstring(utf8_to_wstring(dayoprj_ini["Stage"]["ModelMotionFile"].as<std::string>()));
						vmdcamFilename = std::wstring(utf8_to_wstring(dayoprj_ini["Camera"]["MotionFile"].as<std::string>()));
						skyboxFilename = std::wstring(utf8_to_wstring(dayoprj_ini["Skybox"]["TextureFile"].as<std::string>()));
						dayoWork->m_DayoWorkData.m_materialJsonFileName = std::wstring(utf8_to_wstring(dayoprj_ini["Environment"]["MaterialJson"].as<std::string>()));

						loadPMXLambda(modelFilename, chModelIdx);
						loadVMDLambda(vmdFilename, chModelIdx);
						loadPMXLambda(stgmodelFilename, stModelIdx);
						loadVMDLambda(stgvmdFilename, stModelIdx);
						loadVMDCameraLambda(vmdcamFilename);
						loadSkyboxLambda(skyboxFilename);

						skyboxPhi = dayoprj_ini["Skybox"]["skyboxPhi"].as<float>();

						// 環境設定
						EnvSetting env_setting = {};
						env_setting.m_V_Gain = dayoprj_ini["Environment"]["Brightness"].as<float>();
						env_setting.m_S_Gain = dayoprj_ini["Environment"]["Saturation"].as<float>();
						dayoWork->m_DayoWorkData.fog_color.x = dayoprj_ini["Environment"]["FogColor_RED"].as<float>();
						dayoWork->m_DayoWorkData.fog_color.y = dayoprj_ini["Environment"]["FogColor_GREEN"].as<float>();
						dayoWork->m_DayoWorkData.fog_color.z = dayoprj_ini["Environment"]["FogColor_BLUE"].as<float>();

						envSettingWindow.SetEnvEnvSetting(env_setting);

						// Aviファイル設定
						AVISetting aviSetting = {};
						aviSetting.m_Avi_Width = dayoprj_ini["AviSetting"]["Width"].as<int>();
						aviSetting.m_Avi_height = dayoprj_ini["AviSetting"]["Height"].as<int>();
						aviSetting.m_StartFrame = dayoprj_ini["AviSetting"]["StartFrame"].as<int>();
						aviSetting.m_EndFrame = dayoprj_ini["AviSetting"]["EndFrame"].as<int>();
						aviSetting.m_AccumFrame = dayoprj_ini["AviSetting"]["AccumCount"].as<int>();
						if (dayoWork->m_DayoWorkData.m_fps60enable) {
							aviSetting.m_FPS = 60;
						}
						else {
							aviSetting.m_FPS = 30;
						}
						aviSettingWindow.SetAviParameters(aviSetting);

						// レンダリング設定
						cs->spectralRendering = dayoprj_ini["Rendering"]["SPECTRAL"].as<int>();
						if (cs->spectralRendering == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SPECTRAL, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_VIEW_SPECTRAL, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SPECTRAL, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_VIEW_SPECTRAL, MFS_CHECKED);
							cs->spectralRendering = 1;
						}
						cs->ShadowEnable = dayoprj_ini["Rendering"]["SHADOW"].as<int>();
						if (cs->ShadowEnable == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SHADOW, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_VIEW_SHADOW, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SHADOW, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_VIEW_SHADOW, MFS_CHECKED);
							cs->ShadowEnable = 1;
						}
						cs->lensR = dayoprj_ini["Rendering"]["LensR"].as<float>();
						cs->pint = dayoprj_ini["Rendering"]["Pint"].as<float>();
						bMotionLoop = dayoprj_ini["Rendering"]["MotionLoop"].as<int>();
						if (bMotionLoop == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_MOTION_LOOP, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_MOTION_LOOP, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_MOTION_LOOP, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_MOTION_LOOP, MFS_CHECKED);
							bMotionLoop = TRUE;
						}

						// エフェクト
						cs->DofEnable = dayoprj_ini["Effect"]["Dof"].as<int>();
						if (cs->DofEnable == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_DOF, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_DOF, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_DOF, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_DOF, MFS_CHECKED);
							cs->DofEnable = 1;
						}
						bAutoFocus = dayoprj_ini["Effect"]["AutoFocus"].as<int>();
						if (bAutoFocus == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MFS_CHECKED);
							bAutoFocus = TRUE;
						}
						bBloomEnable = dayoprj_ini["Effect"]["Bloom"].as<int>();
						if (bBloomEnable == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_BLOOM, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_BLOOM, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_BLOOM, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_BLOOM, MFS_CHECKED);
							bBloomEnable = TRUE;
						}
						bFxaaEnable = dayoprj_ini["Effect"]["Fxaa"].as<int>();
						if (bFxaaEnable == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_FXAA, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_FXAA, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_FXAA, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_FXAA, MFS_CHECKED);
							bFxaaEnable = TRUE;
						}
						cs->FogEnable = dayoprj_ini["Effect"]["Fog"].as<int>();
						if (cs->FogEnable == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_FOG, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_FOG, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_FOG, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_FOG, MFS_CHECKED);
							cs->FogEnable = 1;
						}
						bUserComSEffect = dayoprj_ini["Effect"]["CS_User"].as<int>();
						if (bUserComSEffect == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_USERCOM, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_USERCOM, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_USERCOM, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_USERCOM, MFS_CHECKED);
							bUserComSEffect = TRUE;
						}
						bUserPostEffect = dayoprj_ini["Effect"]["PS_User"].as<int>();
						if (bUserPostEffect == 0) {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_USERPS, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_USERPS, MFS_UNCHECKED);
						}
						else {
							HMENU hMenu = GetMenu(dxr->hWnd());
							UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_USERPS, MF_BYCOMMAND);
							CheckMenuItem(hMenu, ID_MENU_EFFECT_USERPS, MFS_CHECKED);
							bUserPostEffect = TRUE;
						}
					}
				}
				else if (msg.wParam == ID_MENU_FILE_SAVEPRJ) {
					//プロジェクトファイルの保存
					if (FileDialog(filename, { L"MikuMikuDayo Prj(*.pdayo)",L"*.pdayo" }, BasePath, true)) {
						std::wstring filename_with_ext = AddExtensionIfNeeded(filename, L".pdayo");
						ini::IniFile dayoprj_ini;

						// 60FPS 設定
						dayoprj_ini["AviSetting"]["60FPSEnable"] = dayoWork->m_DayoWorkData.m_fps60enable;

						// モデル・カメラ・スカイボックス
						dayoprj_ini["Charactor"]["ModelFile"] = wstring_to_utf8(modelFilename).c_str();
						dayoprj_ini["Charactor"]["ModelMotionFile"] = wstring_to_utf8(vmdFilename).c_str();
						dayoprj_ini["Stage"]["ModelFile"] = wstring_to_utf8(stgmodelFilename).c_str();
						dayoprj_ini["Stage"]["ModelMotionFile"] = wstring_to_utf8(stgvmdFilename).c_str();
						dayoprj_ini["Camera"]["MotionFile"] = wstring_to_utf8(vmdcamFilename).c_str();
						dayoprj_ini["Skybox"]["TextureFile"] = wstring_to_utf8(skyboxFilename).c_str();
						dayoprj_ini["Skybox"]["skyboxPhi"] = skyboxPhi;

						// 環境設定
						EnvSetting env_setting = {};
						envSettingWindow.GetEnvEnvSetting(env_setting);
						dayoprj_ini["Environment"]["Brightness"] = env_setting.m_V_Gain;
						dayoprj_ini["Environment"]["Saturation"] = env_setting.m_S_Gain;
						dayoprj_ini["Environment"]["FogColor_RED"] = dayoWork->m_DayoWorkData.fog_color.x;
						dayoprj_ini["Environment"]["FogColor_GREEN"] = dayoWork->m_DayoWorkData.fog_color.y;
						dayoprj_ini["Environment"]["FogColor_BLUE"] = dayoWork->m_DayoWorkData.fog_color.z;
						dayoprj_ini["Environment"]["MaterialJson"] = wstring_to_utf8(dayoWork->m_DayoWorkData.m_materialJsonFileName.c_str());

						// Aviファイル設定
						AVISetting aviSetting = {};
						aviSettingWindow.GetAviParameters(aviSetting);
						dayoprj_ini["AviSetting"]["Width"] = aviSetting.m_Avi_Width;
						dayoprj_ini["AviSetting"]["Height"] = aviSetting.m_Avi_height;
						dayoprj_ini["AviSetting"]["StartFrame"] = aviSetting.m_StartFrame;
						dayoprj_ini["AviSetting"]["EndFrame"] = aviSetting.m_EndFrame;
						dayoprj_ini["AviSetting"]["AccumCount"] = aviSetting.m_AccumFrame;

						// レンダリング設定
						dayoprj_ini["Rendering"]["SPECTRAL"] = cs->spectralRendering;
						dayoprj_ini["Rendering"]["SHADOW"] = cs->ShadowEnable;
						dayoprj_ini["Rendering"]["LensR"] = cs->lensR;
						dayoprj_ini["Rendering"]["Pint"] = cs->pint;
						dayoprj_ini["Rendering"]["MotionLoop"] = bMotionLoop;

						// エフェクト
						dayoprj_ini["Effect"]["Dof"] = cs->DofEnable;
						dayoprj_ini["Effect"]["AutoFocus"] = bAutoFocus;
						dayoprj_ini["Effect"]["Bloom"] = bBloomEnable;
						dayoprj_ini["Effect"]["Fxaa"] = bFxaaEnable;
						dayoprj_ini["Effect"]["Fog"] = cs->FogEnable;
						dayoprj_ini["Effect"]["CS_User"] = bUserComSEffect;
						dayoprj_ini["Effect"]["PS_User"] = bUserPostEffect;

						// 書き込み
						dayoprj_ini.save(wstring_to_utf8(filename_with_ext));
					}
				}
				else if (msg.wParam == ID_MENU_FILE_SKYBOX) {
					// スカイボックスの読み込み
					if (FileDialog(filename, { L"画像ファイル(*.hdr,*.dds,*.jpg,*.png,*.bmp)",L"*.jpg;*.png;*.hdr;*.dds;*.bmp" }, BasePath + L"sample")) {
						skyboxFilename = filename;
						loadSkyboxLambda(filename);
					}
				} else if (msg.wParam == ID_MENU_FILE_CH_MODEL) {
					// キャラクター モデル読み込み
					if (FileDialog(filename, { L"PMXファイル(*.pmx)",L"*.pmx" }, BasePath + L"sample")) {
						modelFilename = filename;
						loadPMXLambda(filename, chModelIdx);
					}
				} else if (msg.wParam == ID_MENU_FILE_CH_VMD) {
					//キャラクター モーション読み込み
					if (FileDialog(filename, { L"VMDファイル(*.vmd)",L"*.vmd" }, BasePath + L"sample")) {
						vmdFilename = filename;
						loadVMDLambda(filename, chModelIdx);
					}
				}
				else if (msg.wParam == ID_MENU_FILE_ST_MODEL) {
					// ステージ モデル読み込み
					if (FileDialog(filename, { L"PMXファイル(*.pmx)",L"*.pmx" }, BasePath + L"sample")) {
						stgmodelFilename = filename;
						loadPMXLambda(filename, stModelIdx);
					}
				}
				else if (msg.wParam == ID_MENU_FILE_ST_VMD) {
					//ステージ モーション読み込み
					if (FileDialog(filename, { L"VMDファイル(*.vmd)",L"*.vmd" }, BasePath + L"sample")) {
						stgvmdFilename = filename;
						loadVMDLambda(filename, stModelIdx);
					}
				} else if (msg.wParam == ID_MENU_FILE_CAM) {
					//カメラモーション読み込み
					if (FileDialog(filename, { L"VMDファイル(*.vmd)",L"*.vmd" }, BasePath + L"sample")) {
						vmdcamFilename = filename;
						loadVMDCameraLambda(filename);
					}
				} else if (msg.wParam == ID_MENU_FILE_EXIT) {
					// アプリ終了
					DestroyWindow(app->hWnd());
				} else if (msg.wParam == ID_MENU_VIEW_DENOISE) {
					//デノイズ on/off
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_VIEW_DENOISE, MF_BYCOMMAND);
					if (state & MF_CHECKED) {
						CheckMenuItem(hMenu, ID_MENU_VIEW_DENOISE, MFS_UNCHECKED);
						denoise = false;
					} else {
						CheckMenuItem(hMenu, ID_MENU_VIEW_DENOISE, MFS_CHECKED);
						denoise = true;
					}
				}
				else if (msg.wParam == ID_MENU_VIEW_SPECTRAL) {
					//分光 on/off
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SPECTRAL, MF_BYCOMMAND);
					if (state & MF_CHECKED) {
						CheckMenuItem(hMenu, ID_MENU_VIEW_SPECTRAL, MFS_UNCHECKED);
						cs->spectralRendering = 0;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_VIEW_SPECTRAL, MFS_CHECKED);
						cs->spectralRendering = 1;
					}
					iFrame = 0;
				} else if (msg.wParam == ID_MENU_VIEW_SHADOW) {
					//シャドウ(影) on/off
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SHADOW, MF_BYCOMMAND);
					if (state & MF_CHECKED) {
						CheckMenuItem(hMenu, ID_MENU_VIEW_SHADOW, MFS_UNCHECKED);
						cs->ShadowEnable = 0;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_VIEW_SHADOW, MFS_CHECKED);
						cs->ShadowEnable = 1;
					}
					iFrame = 0;
				} else if (msg.wParam == ID_MENU_VIEW_SHADER_0) {
					//シェーダ変更 : プレビュー
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SHADER_0, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
						ShaderMode = 0;
						CheckMenuRadioItem(hMenu, ID_MENU_VIEW_SHADER_0, ID_MENU_VIEW_SHADER_2, ID_MENU_VIEW_SHADER_0, MF_BYCOMMAND);
						ReCreatePasses(Passes, dxr, tlas, cb, aperture, samp, dxrOut, dxrFlare, hdrRT,
							modelTex[chModelIdx], modelBuf[chModelIdx], modelTex[stModelIdx], modelBuf[stModelIdx],
							skyboxTex, skyboxBuf, oidnBuf);
						pm = Passes[0];
						pc[chModelIdx] = Passes[1];
						pc[stModelIdx] = Passes[2];
						px = Passes[3];
						SetCursor(hcurPrev);
						iFrame = 0;
					}
				} else if (msg.wParam == ID_MENU_VIEW_SHADER_1) {
					//シェーダ変更 : 片方向パストレ
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SHADER_1, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
						ShaderMode = 1;
						CheckMenuRadioItem(hMenu, ID_MENU_VIEW_SHADER_0, ID_MENU_VIEW_SHADER_2, ID_MENU_VIEW_SHADER_1, MF_BYCOMMAND);
						ReCreatePasses(Passes, dxr, tlas, cb, aperture, samp, dxrOut, dxrFlare, hdrRT,
							modelTex[chModelIdx], modelBuf[chModelIdx],modelTex[stModelIdx], modelBuf[stModelIdx],
							skyboxTex, skyboxBuf, oidnBuf);
						pm = Passes[0];
						pc[chModelIdx] = Passes[1];
						pc[stModelIdx] = Passes[2];
						px = Passes[3];
						SetCursor(hcurPrev);
						iFrame = 0;
					}
				}
				else if (msg.wParam == ID_MENU_VIEW_SHADER_2) {
					//シェーダ変更 : 双方向パストレ
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_VIEW_SHADER_2, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						HCURSOR hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));
						ShaderMode = 2;
						CheckMenuRadioItem(hMenu, ID_MENU_VIEW_SHADER_0, ID_MENU_VIEW_SHADER_2, ID_MENU_VIEW_SHADER_2, MF_BYCOMMAND);
						ReCreatePasses(Passes, dxr, tlas, cb, aperture, samp, dxrOut, dxrFlare, hdrRT,
							modelTex[chModelIdx], modelBuf[chModelIdx], modelTex[stModelIdx], modelBuf[stModelIdx],
							skyboxTex, skyboxBuf, oidnBuf);
						pm = Passes[0];
						pc[chModelIdx] = Passes[1];
						pc[stModelIdx] = Passes[2];
						px = Passes[3];
						SetCursor(hcurPrev);
						iFrame = 0;
					}
				}
				else if (msg.wParam == ID_MENU_VIEW_WORK) {
					// ワークスペースウィンドウ
					dayoWork->m_DayoWorkData.m_showWindow = true;
				} else if (msg.wParam == ID_MENU_VIEW_ENV) {
					// 色調調整ウィンドウ
					envSettingWindow.ShowDialog(hInstance, app->hWnd());					
				} else if (msg.wParam == ID_MENU_MOTION_START) {
					// モーション動作開始
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_MOTION_START, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuRadioItem(hMenu, ID_MENU_MOTION_START, ID_MENU_MOTION_STOP, ID_MENU_MOTION_START, MF_BYCOMMAND);
					}
					animation = true;
					iFrame = 0;
				} else if (msg.wParam == ID_MENU_MOTION_STOP) {
					// モーション動作停止
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_MOTION_STOP, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuRadioItem(hMenu, ID_MENU_MOTION_START, ID_MENU_MOTION_STOP, ID_MENU_MOTION_STOP, MF_BYCOMMAND);
					}
					animation = false;
					iFrame = 0;
				} else if (msg.wParam == ID_MENU_MOTION_JUMP) {
					// モーション 指定フレームにジャンプ
					MotionSetting motionSetting = {};
					DialogRet ret = motionSettingWindow.ShowDialog(hInstance, app->hWnd(), iTick);
					if (ret == DLG_OK) {
						motionSettingWindow.GetMotionParameters(motionSetting);
						iTick = motionSetting.m_Frame;
						prevSolvedFrame = iTick;
						solver[chModelIdx]->Solve(prevSolvedFrame);
						//physics[chModelIdx]->Reset();
						physics[chModelIdx]->Prewarm(120);
						solver[stModelIdx]->Solve(prevSolvedFrame);
						//physics[stModelIdx]->Reset();
						physics[stModelIdx]->Prewarm(120);
						CameraDX = 0;
						CameraDY = 0;
						CameraDTheta = 0;
						CameraDPhi = 0;
						CameraDistanceDz = 0;
						FovD = 0;
						iFrame = 0;
					}					
				} else if (msg.wParam == ID_MENU_MOTION_LOOP) {
					// モーションループ可否
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_MOTION_LOOP, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_MOTION_LOOP, MF_CHECKED);
						bMotionLoop = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_MOTION_LOOP, MF_UNCHECKED);
						bMotionLoop = FALSE;
					}
				} else if (msg.wParam == ID_MENU_MOTION_RESET) {
					// モーションリセット
					animation = false;
					//物理リセット…剛体の位置・回転をボーンアニメーションを適用しただけの状態に戻す
					iTick = 0;
					prevSolvedFrame = iTick;
					solver[chModelIdx]->Solve(prevSolvedFrame);
					//physics[chModelIdx]->Reset();
					physics[chModelIdx]->Prewarm(120);
					solver[stModelIdx]->Solve(prevSolvedFrame);
					//physics[stModelIdx]->Reset();
					physics[stModelIdx]->Prewarm(120);
					CameraDX = 0;
					CameraDY = 0;
					CameraDTheta = 0;
					CameraDPhi = 0;
					CameraDistanceDz = 0;
					FovD = 0;
					iFrame = 0;
				}
				else if (msg.wParam == ID_MENU_EFFECT_DOF) {
					// DOF有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_DOF, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_DOF, MF_CHECKED);
						cs->DofEnable = 1;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_DOF, MF_UNCHECKED);
						cs->DofEnable = 0;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_AUTOFOCUS) {
					// オートフォーカス有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MF_CHECKED);
						bAutoFocus = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_AUTOFOCUS, MF_UNCHECKED);
						bAutoFocus = FALSE;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_BLOOM) {
					// Bloom有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_BLOOM, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_BLOOM, MF_CHECKED);
						bBloomEnable = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_BLOOM, MF_UNCHECKED);
						bBloomEnable = FALSE;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_FXAA) {
					// FXAA有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_FXAA, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_FXAA, MF_CHECKED);
						bFxaaEnable = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_FXAA, MF_UNCHECKED);
						bFxaaEnable = FALSE;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_FOG) {
					// Fog有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_FOG, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_FOG, MF_CHECKED);
						cs->FogEnable = 1;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_FOG, MF_UNCHECKED);
						cs->FogEnable = 0;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_USERCOM) {
					// ユーザーコンピュートシェーダー 有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_USERCOM, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_USERCOM, MF_CHECKED);
						bUserComSEffect = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_USERCOM, MF_UNCHECKED);
						bUserComSEffect = FALSE;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_USERPS) {
					// ユーザーポストエフェクト 有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_USERPS, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_USERPS, MF_CHECKED);
						bUserPostEffect = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_USERPS, MF_UNCHECKED);
						bUserPostEffect = FALSE;
					}
				}
				else if (msg.wParam == ID_MENU_EFFECT_DRAWDEPTH) {
					// Rayシェーダ生成深度マップ表示 有効/無効
					HMENU hMenu = GetMenu(dxr->hWnd());
					UINT state = GetMenuState(hMenu, ID_MENU_EFFECT_DRAWDEPTH, MF_BYCOMMAND);
					if ((state & MF_CHECKED) == 0) {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_DRAWDEPTH, MF_CHECKED);
						bDrawDepthEnable = TRUE;
					}
					else {
						CheckMenuItem(hMenu, ID_MENU_EFFECT_DRAWDEPTH, MF_UNCHECKED);
						bDrawDepthEnable = FALSE;
					}
				}
				else if (msg.wParam == ID_MENU_CAPTURE_PNG) {
					// PNGファイルに保存
					app->set_borderless(true);
					if (FileDialog(filename, { L"PNGファイル(*.png)",L"*.png" }, BasePath, true)) {
						std::wstring filename_with_ext = AddExtensionIfNeeded(filename, L".png");
						
						//<GDIとstb_image_writeを使ったPNGファイルの保存>
						CaptureGdiAndSavePng(app->hWnd(), filename_with_ext.c_str());
						
						//<DirectXバックバッファとDirectXTexのSaveToWICFileを使ったPNGファイルの保存>
						//dxr->Snapshot(filename_with_ext.c_str());		// トーンマッピングする前を保存してしまう？のかスクリーン表示とPNGファイルの内容が違うので使用しない.
					}
					app->set_borderless(false);
				}
				else if (msg.wParam == ID_MENU_CAPTURE_AVI) {
					// AVIファイルに保存
					// ボーダレスウィンドウに切替
					app->set_borderless(true);

					// AVIファイル書き込み設定ウィンドウ表示
					AVISetting aviSetting = {};
					aviSetting.m_EndFrame = vmd[chModelIdx]->lastFrame;
					if (aviSetting.m_EndFrame < vmd_camera->lastFrame) {
						aviSetting.m_EndFrame = vmd_camera->lastFrame;
					}
					if (dayoWork->m_DayoWorkData.m_fps60enable) {
						aviSetting.m_FPS = 60;
					}
					else {
						aviSetting.m_FPS = 30;
					}
					DialogRet ret = aviSettingWindow.ShowDialog(hInstance, app->hWnd(), aviSetting.m_EndFrame, aviSetting.m_FPS);
					aviSettingWindow.GetAviParameters(aviSetting);
					// AVIファイル書き込み準備
					if (ret == DLG_OK)
					{
						RECT rect = {};
						int renderWith = aviSetting.m_Avi_Width;
						int renderHeight = aviSetting.m_Avi_height;
						LONG width = renderWith;
						LONG height = renderHeight;
						ULONG sizeimage = width * height * 4;

						bitMapInfoHeader = { sizeof(BITMAPINFOHEADER),width,height,1,32,BI_RGB,sizeimage,0,0,0,0 };
						iFrame = 0;
						frameCounter = aviSetting.m_StartFrame;
						iTick = aviSetting.m_StartFrame;
						prevSolvedFrame = iTick;
						solver[chModelIdx]->Solve(prevSolvedFrame);
						//physics[chModelIdx]->Reset();
						physics[chModelIdx]->Prewarm(120);
						solver[stModelIdx]->Solve(prevSolvedFrame);
						//physics[stModelIdx]->Reset();
						physics[stModelIdx]->Prewarm(120);
						CameraDX = 0;
						CameraDY = 0;
						CameraDTheta = 0;
						CameraDPhi = 0;
						CameraDistanceDz = 0;
						FovD = 0;
						
						bResizeOK = FALSE;
						RECT nowRect = { 0 };	GetWindowRect(app->hWnd(), &nowRect);
						MoveWindow(app->hWnd(), nowRect.left, nowRect.top, renderWith, renderHeight, TRUE);
						
						// レンダリングサイズの変更、デノイズクォリティの変更(バランス->最高)
						{
							dxr->Resize(renderWith, renderHeight);
							W = dxr->Width();
							H = dxr->Height();
							cs->resolution = DirectX::XMFLOAT2(W, H);

							updateDenoiseQuality_And_CreateSizeDependentResource(OIDN_QUALITY_HIGH);
						}

						if (FileDialog(filename, { L"AVIファイル(*.avi)",L"*.avi" }, BasePath, true)) {
							// AVIファイル書き込み指示
							aviFileWriter.CloseAVIFile();
							std::wstring filename_with_ext = AddExtensionIfNeeded(filename, L".avi");
							if (dayoWork->m_DayoWorkData.m_fps60enable) { aviSetting.m_FPS = 60; }
							bool bret = aviFileWriter.CreateAVIFile(filename_with_ext.c_str(), width, height, aviSetting.m_FPS, aviSetting.m_FPS);

							if (bret == true)
							{
								accumCounter = aviSetting.m_AccumFrame;	//2フレーム累積
								endframeCounter = aviSetting.m_EndFrame;
								animation = true;
								bActive_GDI_MovCap = TRUE;
							}
							else
							{
								app->set_borderless(false);
							}
						}
					}
					else {
						app->set_borderless(false);
					}
				}
				else if (msg.wParam == ID_MENU_CAPTURE_WINDOWSIZE) {
					// ウィンドウサイズの変更
					RECT nowRect = { 0 };	GetWindowRect(app->hWnd(), &nowRect);
					int wWidth = nowRect.right - nowRect.left;
					int wHeight = nowRect.bottom - nowRect.top;

					WindowSetting windowSetting = {};
					DialogRet ret = windowSettingWindow.ShowDialog(hInstance, app->hWnd(), wWidth, wHeight);
					if (ret == DLG_OK) {
						windowSettingWindow.GetWindowParameters(windowSetting);
						::MoveWindow(app->hWnd(), nowRect.left, nowRect.top, 
									windowSetting.m_Width, windowSetting.m_Height, TRUE);
					}
				}
			} else if (msg.message == WM_SIZE) {
				//リサイズ
				if (LOWORD(msg.lParam) != 0 && HIWORD(msg.lParam) != 0) {
					//Win+Dで切り替えられた場合はサイズ0が指定されるので、その場合は無視する
					d2d.UnwrapBackBuffers();

					bResizeOK = TRUE;

					dxr->Resize(LOWORD(msg.lParam), HIWORD(msg.lParam));
					W = dxr->Width();
					H = dxr->Height();
					cs->resolution = DirectX::XMFLOAT2(W, H);
					iFrame = 0;

					OIDNQuality nowQuality = oidnQuality;
					updateDenoiseQuality_And_CreateSizeDependentResource(nowQuality);
				}
			}
		}
		//↓はこのウィンドウにフォーカスがあってなくても発生する
		if (msg.message == WM_DROPFILES) {
			 HDROP hdrop = (HDROP)msg.wParam;
			 std::wstring dropped;
			 dropped.resize(MAX_PATH);
			 DragQueryFile(hdrop, 0, dropped.data(), MAX_PATH);
			 DragFinish(hdrop);
			 auto k = dropped.size();
			 auto ext = std::filesystem::path(dropped).extension().wstring();
			 auto idx = ext.find(L'\0');
			 if (idx != std::string::npos)
				 ext.resize(idx);
			 if (ext == L".vmd") {
				 loadVMDLambda(dropped, chModelIdx);
			 } else if (ext == L".pmx") {
				 modelFilename = dropped;
				 loadPMXLambda(dropped, chModelIdx);
			 } else if (ext == L".bmp" || ext == L".jpg" || ext == L".png" || ext == L".hdr" || ext == L".dds") {
				 loadSkyboxLambda(dropped);
			 }
		}

		//カメラパラメータの更新
		if (iTick <= vmd_camera->lastFrame + 1)
		{
			cam_solver->Solve(iTick % (vmd_camera->lastFrame + 1), 0, &camera);
		}
		CameraDistance = -1.0f * camera.distance + CameraDistanceDz;
		CameraTarget = camera.position;
		CameraTheta = -1.0f * camera.rotation.x + CameraDTheta;
		CameraPhi= -1.0f * camera.rotation.y + CameraDPhi;
		CameraPsi= -1.0f * camera.rotation.z;
		cs->fov = 1 / tanf(((camera.viewAngle / 2.0f) + FovD) * PI / 180);
		
		// Z軸回転(スクリーンの回転)を実施
		//double cz = cos(CameraPsi), sz = sin(CameraPsi);
		DirectX::XMFLOAT3 X = DirectX::XMFLOAT3(1, 0, 0);
		DirectX::XMFLOAT3 Y = DirectX::XMFLOAT3(0, 1, 0);
		DirectX::XMFLOAT3 Z = DirectX::XMFLOAT3(0, 0, 1);
		DirectX::XMMATRIX rotRz = DirectX::XMMatrixRotationZ(CameraPsi);
		DirectX::XMStoreFloat3(&Y, XMVector3Transform(XMLoadFloat3(&Y), rotRz));
		X = Cross(-Z, Y);
		//Z = Cross(Y, X);	//左右反転

		// パン・チルト回転を実施
		//double ct = cos(CameraTheta), st = sin(CameraTheta);
		//double cp = cos(CameraPhi), sp = sin(CameraPhi);
		DirectX::XMMATRIX rotRx = DirectX::XMMatrixRotationX(CameraTheta);
		DirectX::XMMATRIX rotRy = DirectX::XMMatrixRotationY(CameraPhi);
		DirectX::XMMATRIX rotR = rotRx.operator*(rotRy);
		DirectX::XMStoreFloat3(&X, XMVector3Transform(XMLoadFloat3(&X), rotR));
		DirectX::XMStoreFloat3(&Y, XMVector3Transform(XMLoadFloat3(&Y), rotR));
		DirectX::XMStoreFloat3(&Z, XMVector3Transform(XMLoadFloat3(&Z), rotR));

		// ユーザーコンピュートシェーダーで、カメラ・照明情報 追加更新(処理時間が犠牲になるが, プラグインとして)
		void* pData_user;
		DirectX::XMFLOAT3 LightPos = {3000.0f, 10000.0f, -5000.0f};
		// データ入力  MikuMikuDayoのカメラ・照明情報
		float cs_pint = cs->pint;
		CameraAndLight cs_cal = { Y, X, Z, CameraTarget + (-Z * CameraDistance), cs_pint , LightPos };
		if (bUserComSEffect == TRUE) {
			// データ入力  MikuMikuDayoのカメラ・照明情報
			cs_CameraAndLight_in.res->Map(0, nullptr, &pData_user);
			memcpy(pData_user, &cs_cal, sizeof(CameraAndLight));
			cs_CameraAndLight_in.res->Unmap(0, nullptr);
			// データ入力  MikuMikuDayoのワールド情報
			WorldInfomation cs_wi = { iFrame, iTick };
			cs_WorldInfomation.res->Map(0, nullptr, &pData_user);
			memcpy(pData_user, &cs_wi, sizeof(WorldInfomation));
			cs_WorldInfomation.res->Unmap(0, nullptr);
			// ユーザーコンピュートシェーダー 呼び出し
			dxr->OpenCommandListCS();
			pcUser->Compute(1, 1, 1);
			dxr->ExecuteCommandListCS();
			// データ回収 & 更新
			dxr->Download(&cs_cal, cs_CameraAndLight_out);
			LightPos = cs_cal.light_position;
		}
		
		// シェーダー用にカメラ情報を更新
		if (bUserComSEffect == TRUE) {
			// ユーザーコンピュートシェーダー で更新された情報を設定
			cs->cameraRight = cs_cal.camera_right; //X;
			cs->cameraUp = cs_cal.camera_up; //Y;
			cs->cameraForward = cs_cal.camera_forward; //Z;
			cs->cameraPosition = cs_cal.camera_position + (X * CameraDX + Y * CameraDY);
			//cs->cameraPosition = CameraTarget + ( -Z * CameraDistance + X * CameraDX + Y * CameraDY);
			cs->pint = cs_cal.camera_pint;
			cs->lightPosition = LightPos;
		}
		else {
			// カメラモーションのデータをそのまま設定
			cs->cameraRight = X;
			cs->cameraUp = Y;
			cs->cameraForward = Z;
			cs->cameraPosition = CameraTarget + ( -Z * CameraDistance + X * CameraDX + Y * CameraDY);
			cs->lightPosition = LightPos;
		}
		cs->skyboxPhi = skyboxPhi;

		// 色調情報
		EnvSetting envSetting = {};
		envSettingWindow.GetEnvEnvSetting(envSetting);
		cs->brigtnessGain = envSetting.m_V_Gain;
		cs->saturationGain = envSetting.m_S_Gain;

		// フォグ情報
		cs->fogColor.x = dayoWork->m_DayoWorkData.fog_color.x;
		cs->fogColor.y = dayoWork->m_DayoWorkData.fog_color.y;
		cs->fogColor.z = dayoWork->m_DayoWorkData.fog_color.z;
		cs->fogColor.w = dayoWork->m_DayoWorkData.fog_color.w;

		// フレーム情報
		cs->iTick = iTick;
		cs->iFrame = iFrame;

		//レンダリングに時間が掛かりまくっていると反応が悪くなる事に対策
		auto tm = timeGetTime();
		if (tm - prev_tm > 60) {
			prev_tm = tm;
			continue;
		}

		bool solved = false;

		// FPS計算変数・定数
		static float mFps = 0.0f;  // fps
		static int mStartTime = 0; // 測定開始時刻
		static int mCount = 0;     // カウンタ
		static const int N = 5;    // 平均を取るサンプル数
		static int add = 0;		   // 次のフレームまでのスキップ数
		
		//描画はじまるよー
		for (int i = 0; i < 1; i++) {	// シーン？は1つだけ

			//現在のフレームのポーズにboneBufを変更する
			//モーションブラー用
			/*
			for (int j = 0; j < 4; j++) {
				solver[chModelIdx]->Solve(iTick % (vmd[chModelIdx]->lastFrame + 1) + j / 8.0);//シャッター間隔はフレーム間隔の倍くらいらしいので単純に4分割ではなく8分割して半分を使う
				solver[chModelIdx]->physics[chModelIdx]->m_fps = 120;
				solver[chModelIdx]->UpdatePhysics(1 / 30.0f);
			}
			*/

			//FPS計算
			{
				if (mCount == 0) {
					// 1フレーム目なら時刻を記憶
					mStartTime = GetTickCount();
				}
				if (mCount == N) {
					// 5フレーム目なら平均を計算する
					int t = GetTickCount();
					mFps = 1000.f / ((t - mStartTime) / (float)N);
					mCount = 0;
					mStartTime = t;
				}
				mCount++;
			}

			//[ToDo]フレームスキップ
			//add = 30 - (int)mFps;
			//if (add < 1) add = 1;

			//[ToDo]フレームレートリミット
			// Sleep(xxx)

			// プレビュー中は累積を行わない
			if (bActive_GDI_MovCap == FALSE)
			{
				add = 1;
			}

			//モデルの更新
			if (iTick >= (prevSolvedFrame + add) || iTick <= (prevSolvedFrame - add)) {
				// フレームカウンタ更新
				int solve_Tick[maxModelNum];
				if (bMotionLoop == TRUE) {
					// モーションループする
					solve_Tick[chModelIdx] = iTick % (vmd[chModelIdx]->lastFrame + 1);
					solve_Tick[stModelIdx] = iTick % (vmd[stModelIdx]->lastFrame + 1);
				}
				else {
					// モーションループしない
					if(iTick <= vmd[chModelIdx]->lastFrame){
						solve_Tick[chModelIdx] = iTick;
					}
					else{
						solve_Tick[chModelIdx] = vmd[chModelIdx]->lastFrame;
					}
					if (iTick <= vmd[stModelIdx]->lastFrame) {
						solve_Tick[stModelIdx] = iTick;
					}
					else {
						solve_Tick[stModelIdx] = vmd[stModelIdx]->lastFrame;
					}
				}
				// ボーン・モーフ・物理更新
				if ((bActive_GDI_MovCap == FALSE) ||
					(bActive_GDI_MovCap == TRUE && iFrame == 0))
				{
					// 以下の実装だと, キャラクターとステージの物理が別々の世界になってしまう(接触しない)が,
					// モーションを流し込んでダンスさせるだけなら, ほぼ問題がない.
					// 別々のオブジェクトの剛体も接触させることは今後のToDoとする.
					
					// キャラクターモデル
					solver[chModelIdx]->Solve(solve_Tick[chModelIdx]);
					if (dayoWork->m_DayoWorkData.m_fps60enable == false) {
						physics[chModelIdx]->Update(1 / 30.0f/*60.0F*/);
					}
					else {
						physics[chModelIdx]->Update(1 / 60.0f/*30.0F*/);
					}
					// ステージモデル
					solver[stModelIdx]->Solve(solve_Tick[stModelIdx]);
					if (dayoWork->m_DayoWorkData.m_fps60enable == false) {
						physics[stModelIdx]->Update(1 / 30.0f/*60.0F*/);
					}
					else {
						physics[stModelIdx]->Update(1 / 60.0f/*30.0F*/);
					}
					prevSolvedFrame = iTick;
				}
			}

			//ボーン行列を更新(キャラクター⇒ステージ)
			void* pData;
			modelBuf[chModelIdx][7].res->Map(0, nullptr, &pData);
			memcpy(pData, solver[chModelIdx]->boneMatrices.data(), solver[chModelIdx]->boneMatrices.size() * sizeof(DirectX::XMMATRIX));
			modelBuf[chModelIdx][7].res->Unmap(0, nullptr);
			modelBuf[stModelIdx][7].res->Map(0, nullptr, &pData);
			memcpy(pData, solver[stModelIdx]->boneMatrices.data(), solver[stModelIdx]->boneMatrices.size() * sizeof(DirectX::XMMATRIX));
			modelBuf[stModelIdx][7].res->Unmap(0, nullptr);

			Sleep(0);

			//モーフ値の更新(キャラクター⇒ステージ)
			modelBuf[chModelIdx][8].res->Map(0, nullptr, &pData);
			memcpy(pData, solver[chModelIdx]->morphValues.data(), solver[chModelIdx]->morphValues.size() * sizeof(float));
			modelBuf[chModelIdx][8].res->Unmap(0, nullptr);
			modelBuf[stModelIdx][8].res->Map(0, nullptr, &pData);
			memcpy(pData, solver[stModelIdx]->morphValues.data(), solver[stModelIdx]->morphValues.size() * sizeof(float));
			modelBuf[stModelIdx][8].res->Unmap(0, nullptr);

			Sleep(0);

			// DXGI_ERROR_DEVICE_HUNGが発生することがあるので、OpenCommandListしてから、
			// ExecuteCommandListするまでの間でいっぺんにたくさんのコマンドをGPUに発行しないようにすること。
			// (根本的な対策がわからない。CopyResourceはあまり実行すべきではないらしい）
			// (各所に入れてあるSleep(0)の微小待ちはおまじない。効果があるかはわからない）

			//CSでスキニング・表情モーフなどをやって、BLAS,TLASに反映
			if (!solved || animation) {
				dxr->OpenCommandListCS();
				pc[chModelIdx]->Compute(vb_c.desc().Width / vb_c.elemSize, 1, 1);
				pc[stModelIdx]->Compute(vb_s.desc().Width / vb_s.elemSize, 1, 1);
				dxr->ExecuteCommandListCS();

				Sleep(0);

				dxr->OpenCommandList();
				dxr->UpdateBLAS(blas[0], modelBuf[chModelIdx][11],ib_c);
				dxr->UpdateBLAS(blas[1], modelBuf[stModelIdx][11],ib_s);
				dxr->ExecuteCommandList();	//ここまでで一回全部実行する
				dxr->OpenCommandList();
				dxr->UpdateTLAS(tlas, blas.size(), blas.data());
				dxr->ExecuteCommandList();	//ここまでで一回全部実行する
				solved = true;
			}

			dxr->OpenCommandList();
			pAF->Render();		//オートフォーカス用測距(Rayシェーダー)
			pDepth->Render();	//ポストプロセス用深度バッファ更新(Rayシェーダー)
			dxr->ExecuteCommandList();
			
			// AutoFocus
			if (animation == true && bAutoFocus == TRUE) {
				float z;
				dxr->Download(&z, AFtex);
				cs->pint = z;
			}

			// レイトレによるレンダリング
			int nSpatio = (iFrame < 60 ? 1 : 1);	// 累積数は,動画出力中なのかプレビュー中なのか等で変わるので,ここでは累積しない.
			for (int j = 0; j < nSpatio; j++) {
				dxr->OpenCommandList();
				pm->Render();		//[dxrOut,dxrFlare]へレイトレによるメインのレンダリング(キャラクター,ステージモデル)
				pa->Render();		//アキュムレーション(dxroutとhistoryをブレンドしてaccRTへ)
				dxr->CopyResource(historyTex, accRT);	//accRTの内容をhistoryTexにコピーする
				pclear->Render();	//[dxrOut,dxrFlare]のクリア
				dxr->ExecuteCommandList();
				//ConstantBufferの更新は描画が完了してからやるべし
				//そうでないと実行中にCBの中身が入れ替わっておかしなことになる
				//それかCBのダブルバッファリングをやるべし
				iFrame++;
			}
		}

		// デノイズ
		if (denoise) {
			// デノイズ 1回目
			//OIDNへの入力(color,albedo,normal)をまとめる
			dxr->OpenCommandList();
			dxr->CopyResource(hdrRT, historyTex);	//historyTexの内容をhdrRTにコピーする
			px->Render();
			dxr->ExecuteCommandList();

			//OIDNでデノイズ
			oidnExecuteFilter(filter);
			const char* errorMessage;
			if (oidnGetDeviceError(odev, &errorMessage) != OIDN_ERROR_NONE)
				YRZ::DEBA("{}", errorMessage);

			//OIDNからの出力をフォーマット変更
			dxr->OpenCommandList();
			po->Render();							// OIDNの出力バッファの内容をhdrRTにコピーする
			dxr->ExecuteCommandList();

			// デノイズ 2回目
			dxr->OpenCommandList();
			px->Render();
			dxr->ExecuteCommandList();

			//OIDNでデノイズ
			oidnExecuteFilter(filter);
			if (oidnGetDeviceError(odev, &errorMessage) != OIDN_ERROR_NONE)
				YRZ::DEBA("{}", errorMessage);

			//OIDNからの出力をフォーマット変更
			dxr->OpenCommandList();
			po->Render();							// OIDNの出力バッファの内容をhdrRTにコピー
			dxr->ExecuteCommandList();
		} else {
			//デノイズなし
			dxr->OpenCommandList();
			dxr->CopyResource(hdrRT, historyTex);	//historyTexの内容をhdrRTにコピーする
			dxr->ExecuteCommandList();
		}
		//ポストプロセス
		
		// Bloomエフェクト
		dxr->OpenCommandList();
		if(bBloomEnable == TRUE) {
			// Bloom有効
			pbloom_fist->Render();
			pbloom_passSx->Render();
			pbloom_passSy->Render();
			pbloom_passX->Render();
			dxr->CopyResource(hdr_Post_RT, hdrRT);	// hdrRTの内容をhdr_Post_RTにコピーする
			pbloom_passY_Mix->Render();
		}
		else {
			// Bloom無効
			dxr->CopyResource(hdr_Post_RT, hdrRT);	// hdrRTの内容をhdr_Post_RTにコピーする
		}
		dxr->ExecuteCommandList();

		// フォグエフェクト
		dxr->OpenCommandList();
		dxr->CopyResource(hdrFullSizeTex, hdr_Post_RT);	// hdr_Post_RTの内容をhdrFullSizeTexにコピーする
		ppFog->Render();
		dxr->ExecuteCommandList();
		
		// ユーザーポストエフェクト
		dxr->OpenCommandList();
		if(bUserPostEffect == TRUE) {
			dxr->CopyResource(hdrFullSizeTex, hdr_Post_RT);
			ppUser->Render();
			dxr->CopyResource(hdr_Post_RT, hdrFullSizeRT);
		}

		// Ray深度バッファ デバッグモニタ用
		if (bDrawDepthEnable == TRUE) {
			pDepthView->Render();
			dxr->CopyResource(hdr_Post_RT, hdrFullSizeRT);
		}
		dxr->ExecuteCommandList();

		// アンチエイリアス
		dxr->OpenCommandList();
		if (bFxaaEnable == TRUE) {
			// アンチエイリアス有効
			pfxaa->Render();
		}
		else {
			// アンチエイリアス無効
			dxr->CopyResource(ppOut_Fxaa, hdr_Post_RT);
		}
		dxr->ExecuteCommandList();


		// トーンマッピング & ガンマ補正 & 色調補正
		dxr->OpenCommandList();
		pp->Render();

		//ImGui frameの表示
		dayoWork->RenderWorkspace(dxr->CommandList().Get());
		dxr->ExecuteCommandList();

		// テロップ(バックバッファに書き込み)
		dxr->OpenCommandList();
		telop->Render();
		dxr->ExecuteCommandList();	//テロップ入れの前にここまでのレンダリング結果をバックバッファに書き込む

		//d2dでラップしたバックバッファに書き込み準備
		d2d.BeginDraw();

		//ヘルプ
		if (HelpMode == 0) {
			D2D1_ROUNDED_RECT rc = { { 0.2 * W, 0.1 * H, 0.8 * W,0.9 * H }, W * 0.01, W * 0.01 };
			d2d.DC()->FillRoundedRectangle(rc, shadowbrush.Get());
			D2D1_RECT_F dest = { 0,0,W,H };
			d2d.DC()->DrawBitmap(help.Get(), &dest);

			//ハンコ
			float hankoAspect = hanko->GetSize().width / hanko->GetSize().height * H / W;
			D2D1_RECT_F hankorect = { W - W * hankoAspect * 0.2 - 8, H - H * 0.2 - 8 , W - 8 , H - 8 };
			d2d.DC()->DrawBitmap(hanko.Get(), &hankorect);
		}

		//情報表示用文字列作成
		//上部テロップ
		//std::wstring telopstr = std::format(L"{} samples 🐰", iFrame);
		std::wstring render_m;
		if (ShaderMode == 0) { render_m = std::format(L"preview"); }
		else if (ShaderMode == 1) { render_m = std::format(L"pathtracing"); }
		else if (ShaderMode == 2) { render_m = std::format(L"bidirectional pathtracing"); }
		std::wstring telopstr = std::format(
			L"FPS : {0:.2f}\r\n Denoise : {1} Render mode : {2} \r\n \
C.x:{3:.2f} C.y:{4:.2f} C.z:{5:.2f} C.rx:{6:.2f} C.ry:{7:.2f} C.rz:{8:.2f} C.d:{9:.2f}\r\n CDx:{10:.2f} CDy:{11:.2f}",
		mFps, denoise, render_m,
		CameraTarget.x, CameraTarget.y, CameraTarget.z,
		(CameraTheta * 180.0f / PI), (-1.0f * CameraPhi * 180.0f / PI), (-1.0f * CameraPsi * 180.0f / PI),
		CameraDistance, CameraDX, CameraDY);

		//下部テロップ
		std::wstring telopstr2 = std::format(L"frame: {} accum: {}\r\n \
fov : {:.0f}[deg]\r\naperture : {:.2f}[mm], working distance : {:.2f}[m]",
		iTick, iFrame, atan(1 / cs->fov) / PI * 360, cs->lensR * 160, cs->pint * 0.08);

		//情報表示(サブウィンドウ)
		std::wstring infostr = std::format(
			L"FPS : {:.2f}  Frame: {}, Accum: {}\r\n Denoise : {}, Render mode : {} \r\n fov : {:.0f}[deg], aperture : {:.2f}[mm]\r\n working distance : {:.2f}[m]",
			mFps, iTick, iFrame, denoise, render_m, atan(1 / cs->fov) / PI * 360, cs->lensR * 160, cs->pint * 0.08);
		envSettingWindow.SetStatusText(infostr);

		//情報表示(メインウィンドウ)
		if (HelpMode <= 1) {
			D2D1_RECT_F textrect = { 12,12, (float)W,(float)H };
			D2D1_RECT_F textrect2 = { 12,H - 112, (float)W,(float)H };
			//まず影を書く
			d2d.DC()->DrawTextW(telopstr.c_str(), telopstr.size(), textformat.Get(), &textrect, shadowbrush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
			d2d.DC()->DrawTextW(telopstr2.c_str(), telopstr2.size(), textformat2.Get(), &textrect2, shadowbrush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
			//本体
			textrect.top -= 3;
			textrect.left -= 3;
			textrect.bottom -= 3;
			textrect.right -= 3;
			textrect2.top -= 1.5;
			textrect2.left -= 1.5;
			textrect2.bottom -= 1.5;
			textrect2.right -= 1.5;
			d2d.DC()->DrawTextW(telopstr.c_str(), telopstr.size(), textformat.Get(), &textrect, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
			d2d.DC()->DrawTextW(telopstr2.c_str(), telopstr2.size(), textformat2.Get(), &textrect2, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
		}

		//書き込み終了
		d2d.EndDraw();

		//バックバッファを表示する
		dxr->Present();

		// 動画書き出し
		if (bActive_GDI_MovCap == TRUE)
		{
			if ( (bResizeOK == TRUE)		// ボーダレスウィンドウにリサイズ済み
			   &&(iFrame >= accumCounter) )	// フレームの指定回数の累積完了済み
			{
				// 動画書き出しの際は,各フレームで指定回数累積を待ち,累積が終わってから, AVIファイルに書き出す.
				// ※ 1回レイトレーシングしただけでは結果がnoisyで, denoizeしきれない(動画として見た時, 結果がちらつく).
				//    レイの反射がどれくらい複雑になっているかにより, 累積しなければならない数は変わる.
				//	  屋外で反射が少なければ5回ぐらいで大丈夫そうだが、屋内(=反射が多い)だと場合によっては25回ぐらい必要.
				
				HWND target = app->hWnd();
				gdiCapture.CaptureWindow(target, [](const void* data, int w, int h) {
					aviFileWriter.StreamWrite(frameCounter, (LPVOID)data, &bitMapInfoHeader);
					});

				frameCounter++;
				if (frameCounter > endframeCounter)
				{
					// 全てのフレームを書き出し完了 -> AVIファイルクローズ処理
					aviFileWriter.CloseAVIFile();
					bActive_GDI_MovCap = FALSE;
					animation = FALSE;
					app->set_borderless(false);
					// デノイズクォリティの変更(最高->バランス)
					updateDenoiseQuality_And_CreateSizeDependentResource(OIDN_QUALITY_BALANCED);
				}
			}
		}

		// 現在フレームNoの更新
		if (animation)
		{
			if (bActive_GDI_MovCap == TRUE)
			{
				// 動画書き出し中. 指定された数,フレームの累積を待ってから次フレームへ.
				if ( (bResizeOK == TRUE)		// ボーダレスウィンドウにリサイズ済み
				   &&(iFrame >= accumCounter) )	// フレームの指定回数の累積完了済み
				{
					add = 1;					// 1フレームずつ進める
					iFrame = 0;					// 累積カウンタリセット
					iTick += add;				// 現在フレームNo インクリメント
				}
				else
				{
					if ( (bResizeOK == FALSE)
					   &&(iFrame >= 10 ) )
					{
						// リサイズが行われなくても, 10フレーム経過したら出力開始する.
						// ※ ボーダレスウィンドウに切り替わって, DXRのリソースのリサイズが完了するのを待ちたいだけ. もっといい方法は..?
						bResizeOK = TRUE;
					}

					add = 0;
				}
			}
			else
			{
				// プレビュー時は、累積を行わない
				iFrame = 0;
				iTick += add;
			}

			Sleep(0);
			//dxr->Snapshot(Format(L"out\\%d.png",iTick).c_str());
		}

		//SetWindowTextW(dxr->hWnd(), YRZ::Format(L"samples %d @ frame %d", iFrame, iTick).c_str());
	}

}


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	ExePath = YRZ::ExePath() + L"\\";
	BasePath = ExePath + L"..\\..\\MikuMikuDayo\\";
	HLSLPath = BasePath.c_str();
	CompileOption = { L"-I", HLSLPath };

	YRZ::LOG(L"{}{}", L"MikuMikuDayo V", GetProductVersionString());

	try {
		Dayo(hInstance);
	} catch (std::exception ex) {
		MessageBoxA(0, ex.what(), "dayooooo!", MB_OK);
		YRZ::LOG(L"{}", YRZ::ANSITowstr(ex.what()));
	}
}
