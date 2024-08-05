#pragma once

//Windows
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#if __INTELLISENSE__
#include "PMXLoader.ixx"
#else
import PMXLoader;
#endif

// DirectX
#include "d3dx12.h"

// imgui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Data::Json;

// グローバル定数
//モデル関連
constexpr DWORD maxModelNum = 2;					   // 総モデル数
constexpr DWORD chModelIdx = 1;						   // キャラクターモデルIdx
constexpr DWORD stModelIdx = 0;						   // ステージモデルIdx

// 共有型定義
//dayo ⇔ dayoWorkspace 共有データ
struct DayoWorkspaceData {
	bool m_showWindow;									// Workspace imguiウィンドウの表示可否
	bool m_fps60enable;									// 60FPSモードへの切替SW
	bool m_resetPhysics;								// 物理リセット要求
	ImVec4 fog_color;									// フォグカラー
	PMX::PMXModel* pmx[maxModelNum] = { NULL, NULL };	// PMXモデルデータ
	std::vector<std::wstring>matchPreset[maxModelNum];	// 各材質に割り当てられたプリセット名
	bool m_loadMaterialJsonReq;							// マテリアル設定Jsonファイル読み込み要求
	std::wstring m_materialJsonFileName;				// マテリアル設定Jsonファイル名
};

//材質情報
enum MaterialCategory { mcDefault = 0, mcGlass = 1, mcMetal = 2, mcSubsurface = 3 };
struct Material {
	int texture;
	int twosided;	//両面描画フラグ
	float autoNormal;
	int category;
	DirectX::XMFLOAT3 albedo;
	DirectX::XMFLOAT3 emission;
	float alpha;
	DirectX::XMFLOAT2 roughness;	//x:Tangent方向、y:Binormal方向の粗さ
	DirectX::XMFLOAT2 IOR;			//xy : Cauchyの分散公式のAB(η = A + B/λ^2 λはnm単位), y=Bが0の時、波長依存性無し
	DirectX::XMFLOAT4 cat;			//カテゴリ固有パラメータ
	float lightCosFalloff;			//スポットライトの減衰が終わる範囲(片側)
	float lightCosHotspot;			//スポットライトの減衰無し範囲(片側)
};


// グローバル関数定義
//JsonObjectヘルパー
//スカラーと2～4次元ベクトル、文字列についての読み込み。keyが無い場合はdefがセットされる
double GetJson1(JsonObject& jo, const std::wstring& key, const double def);
DirectX::XMFLOAT2 GetJson2(JsonObject& jo, const std::wstring& key, const DirectX::XMFLOAT2& def);
DirectX::XMFLOAT3 GetJson3(JsonObject& jo, const std::wstring& key, const DirectX::XMFLOAT3& def);
DirectX::XMFLOAT4 GetJson4(JsonObject& jo, const std::wstring& key, const DirectX::XMFLOAT4& def);
//wcharの下位バイトをつなげてUTF-8→UTF-16に変換しないとjsonに入ってる日本語はまともに読めない
//1バイト目が 0xcX 0xdX のUTF-8では2バイトにエンコードされる文字群(キリル文字とかアラビア文字など)がどうなるのか?
//↑これも0xFFXXとしてエンコードされる様子。↓のコードはUTF-8で2バイトの文字も処理できる様子
std::wstring CorrectJsonStr(const wchar_t* c);
std::wstring GetJsonStr(JsonObject& jo, const std::wstring& key, const std::wstring& def);
//マテリアル情報をJsonObjectから得る。書かれていないパラメータはdefから得る
Material GetMaterialFromJSON(JsonObject& jo, const Material& def);
//小文字にする
extern std::wstring wtolower(const std::wstring& str);
//線形にする？
extern DirectX::XMFLOAT3 ToLinear(DirectX::XMFLOAT3 c);
//nameの中にstrsの中の文字列を1つでも含むかテスト
bool MatchName(const std::wstring& name, const std::vector<std::wstring>& strs);
//設定JSON読み込み、キーワードリストを作成
bool LoadConfigJSonAndMakeKeyword(std::wstring filename, JsonObject& jo,
	JsonObject& jo_defmat, Material& defmat,
	JsonObject& jo_defmat_st, Material& defmat_st,
	std::vector<Material>& preset, std::vector<std::wstring>& presetName, JsonArray& jo_mat,
	JsonArray& jo_rule, int& nRule, std::vector<std::vector<std::wstring>>& keyword, std::vector<std::wstring>& keyword2preset);
//文字列関連
// UTF-8文字列をwstringに変換
std::wstring utf8_to_wstring(const std::string& str);
// wstringをUTF-8文字列に変換
std::string wstring_to_utf8(const std::wstring& str);

// クラス定義
class DayoWorkspace
{
private:
	HWND m_HWnd;										// ウィンドウハンドル
	ID3D12Device* m_d3d12Device;						// DirectX12 Device
	ComPtr<ID3D12DescriptorHeap> m_pd3dSrvDescHeap;		// フォント用SRVディスクリプタ

	void BuildWorkspace();								// imgui 構築処理

public:
	DayoWorkspace();
	~DayoWorkspace();

	DayoWorkspaceData m_DayoWorkData;					// dayo ⇔ dayoWorkspace 共有データ
														// I/Fで設定/取得するようにすべきだが, めんどくさいのでこのままでいいや...

	// imgui IO取得処理
	ImGuiIO& GetIO();
	// imguiセットアップ処理
	bool SetupWorkspace(HWND hwnd, ID3D12Device* pd3d12Device, int backBufferCount);
	// imguiレンダリング処理
	void RenderWorkspace(ID3D12GraphicsCommandList* pd3dCommandList);
};
