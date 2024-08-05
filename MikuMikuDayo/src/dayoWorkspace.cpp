#if __INTELLISENSE__
#include "YRZ.ixx"
#else
import YRZ;
#endif

//STL
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

//DirectX
#include <d2d1_3.h>
#include <d3d11on12.h>
#include "d3dx12.h"
#include <DirectXMath.h>

//その他外部ライブラリ
#include <DirectXTex.h>

#include "DayoWorkspace.h"
#include <codecvt>

//JsonObjectヘルパー
//スカラーと2～4次元ベクトル、文字列についての読み込み。keyが無い場合はdefがセットされる
double GetJson1(JsonObject& jo, const std::wstring& key, const double def)
{
	if (jo.HasKey(key)) {
		return jo.GetNamedNumber(key);
	}
	else {
		return def;
	}
}


DirectX::XMFLOAT2 GetJson2(JsonObject& jo, const std::wstring& key, const DirectX::XMFLOAT2& def)
{
	if (jo.HasKey(key)) {
		DirectX::XMFLOAT2 v;
		v.x = jo.GetNamedArray(key).GetNumberAt(0);
		v.y = jo.GetNamedArray(key).GetNumberAt(1);
		return v;
	}
	else {
		return def;
	}
}

DirectX::XMFLOAT3 GetJson3(JsonObject& jo, const std::wstring& key, const DirectX::XMFLOAT3& def)
{
	if (jo.HasKey(key)) {
		DirectX::XMFLOAT3 v;
		v.x = jo.GetNamedArray(key).GetNumberAt(0);
		v.y = jo.GetNamedArray(key).GetNumberAt(1);
		v.z = jo.GetNamedArray(key).GetNumberAt(2);
		return v;
	}
	else {
		return def;
	}
}


DirectX::XMFLOAT4 GetJson4(JsonObject& jo, const std::wstring& key, const DirectX::XMFLOAT4& def)
{
	if (jo.HasKey(key)) {
		DirectX::XMFLOAT4 v;
		v.x = jo.GetNamedArray(key).GetNumberAt(0);
		v.y = jo.GetNamedArray(key).GetNumberAt(1);
		v.z = jo.GetNamedArray(key).GetNumberAt(2);
		v.w = jo.GetNamedArray(key).GetNumberAt(3);
		return v;
	}
	else {
		return def;
	}
}

//wcharの下位バイトをつなげてUTF-8→UTF-16に変換しないとjsonに入ってる日本語はまともに読めない
//1バイト目が 0xcX 0xdX のUTF-8では2バイトにエンコードされる文字群(キリル文字とかアラビア文字など)がどうなるのか?
//↑これも0xFFXXとしてエンコードされる様子。↓のコードはUTF-8で2バイトの文字も処理できる様子
std::wstring CorrectJsonStr(const wchar_t* c)
{
	auto buf = std::vector<::byte>(wcslen(c) + 1);

	int idx = 0;
	while (*c != 0) {
		WORD w = *c;
		buf[idx] = w & 0xff;
		idx++;
		c++;
	}
	buf[idx] = 0;

	auto ret = YRZ::UTF8Towstr((char*)buf.data());
	return ret;
}

std::wstring GetJsonStr(JsonObject& jo, const std::wstring& key, const std::wstring& def)
{
	if (jo.HasKey(key)) {
		return CorrectJsonStr(jo.GetNamedString(key).c_str());
	}
	else {
		return def;
	}
}
//マテリアル情報をJsonObjectから得る。書かれていないパラメータはdefから得る
Material GetMaterialFromJSON(JsonObject& jo, const Material& def)
{
	Material m = def;
	using YRZ::PM::Math::PI;

	m.autoNormal = GetJson1(jo, L"autoNormal", def.autoNormal);
	m.cat = GetJson4(jo, L"cat", def.cat);

	auto category = GetJsonStr(jo, L"category", L"");
	if (category == L"metal")
		m.category = mcMetal;
	else if (category == L"subsurface")
		m.category = mcSubsurface;
	else if (category == L"glass")
		m.category = mcGlass;
	else
		m.category = mcDefault;

	m.emission = GetJson3(jo, L"emission", def.emission);
	m.IOR = GetJson2(jo, L"IOR", def.IOR);

	m.lightCosFalloff = GetJson1(jo, L"lightFalloff", def.lightCosFalloff);
	if (m.lightCosFalloff != 0)
		m.lightCosFalloff = cosf(m.lightCosFalloff * PI / 180);

	m.lightCosHotspot = GetJson1(jo, L"lightHotspot", def.lightCosHotspot);
	if (m.lightCosHotspot != 0)
		m.lightCosHotspot = cosf(m.lightCosHotspot * PI / 180);

	m.roughness = GetJson2(jo, L"roughness", def.roughness);

	//light属性が入ってる場合、フラグとしてemission.xにマイナスの値を入れる
	//後でalbedoが決定した際にemissionには albedo * -emission.xがセットされる
	float l = GetJson1(jo, L"light", -1);
	if (l >= 0)
		m.emission.x = -l;

	return m;
}

//nameの中にstrsの中の文字列を1つでも含むかテスト
bool MatchName(const std::wstring& name, const std::vector<std::wstring>& strs)
{
	std::wstring n = wtolower(name);

	for (int i = 0; i < strs.size(); i++)
		if (n.find(strs[i]) != std::wstring::npos)
			return true;

	return false;
}

//設定JSON読み込み、キーワードリストを作成
bool LoadConfigJSonAndMakeKeyword(std::wstring filename, JsonObject& jo,
	JsonObject& jo_defmat, Material& defmat,
	JsonObject& jo_defmat_st, Material& defmat_st,
	std::vector<Material>& preset, std::vector<std::wstring>& presetName, JsonArray& jo_mat,
	JsonArray& jo_rule, int& nRule, std::vector<std::vector<std::wstring>>& keyword, std::vector<std::wstring>& keyword2preset)
{
	//設定JSON読み込み
	try {
		std::ifstream jsonfile(filename.c_str());
		std::wstring jsonstr((std::istreambuf_iterator<char>(jsonfile)), std::istreambuf_iterator<char>());
		jo = JsonObject::Parse(winrt::param::hstring(jsonstr));
	}
	catch (...) {
		MessageBox(0, L"can't open config.json", nullptr, MB_OK);
		return false;
	}

	//デフォルトマテリアル(キャラクター)
	try {
		jo_defmat = jo.GetNamedObject(L"default_material");
		defmat = GetMaterialFromJSON(jo_defmat, {});
	}
	catch (...) {
		MessageBox(0, L"invalid default_material in config.json", nullptr, MB_OK);
		return false;
	}

	//デフォルトマテリアル(ステージ)
	try {
		jo_defmat_st = jo.GetNamedObject(L"default_material_stage");
		defmat_st = GetMaterialFromJSON(jo_defmat_st, {});
	}
	catch (...) {
		MessageBox(0, L"invalid default_material_stage in config.json", nullptr, MB_OK);
		return false;
	}

	//マテリアルリストの読み込み
	try {
		jo_mat = jo.GetNamedArray(L"material");
	}
	catch (...) {
		MessageBox(0, L"array \"material\" is not found in config.json", nullptr, MB_OK);
		return false;
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
		}
		catch (...) {
			MessageBox(0, std::format(L"invalid material {}", debstr).c_str(), nullptr, MB_OK);
			return false;
		}
	}

	//変換規則の読み込み
	try {
		jo_rule = jo.GetNamedArray(L"rule");
	}
	catch (...) {
		MessageBox(0, L"\"rule\" is not found in config.json", nullptr, MB_OK);
		return false;
	}
	nRule = jo_rule.Size();
	keyword.resize(nRule);			//キーワードリスト(std::vector<std::vector<std::wstring>>)
	keyword2preset.resize(nRule);	//キーワードに対応するプリセット名(std::vector<std::wstring>)
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
		}
		catch (...) {
			MessageBox(0, std::format(L"invalid rule {}", debstr).c_str(), nullptr, MB_OK);
			return false;
		}
	}

	return true;
}

// UTF-8文字列をwstringに変換
std::wstring utf8_to_wstring(const std::string& str) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.from_bytes(str);
}

// wstringをUTF-8文字列に変換
std::string wstring_to_utf8(const std::wstring& str) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}

DayoWorkspace::DayoWorkspace() {
	m_HWnd = nullptr;
	m_d3d12Device = nullptr;

	m_DayoWorkData.m_showWindow = true;
	m_DayoWorkData.fog_color = ImVec4(0.8f, 1.0f, 1.3f, 1.00f);;
}

DayoWorkspace::~DayoWorkspace() {
	// imgui Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

// imgui IOの取得
ImGuiIO& DayoWorkspace::GetIO() {
	return ImGui::GetIO();
}

// imguiセットアップ処理
bool DayoWorkspace::SetupWorkspace(HWND hwnd, ID3D12Device* pd3d12Device, int backBufferCount)
{
	m_HWnd = hwnd;
	m_d3d12Device = pd3d12Device;

	// imguiの初期化(Step1)	Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;      // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;       // Enable Gamepad Controls
	ImGui::StyleColorsDark();									 // Setup Dear ImGui style
	ImGui_ImplWin32_Init(m_HWnd);								 // Setup Platform/Renderer backends

	// imgui 初期化Step2(フォントオブジェクト作成)	
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (m_d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pd3dSrvDescHeap.GetAddressOf())) != S_OK)
			YRZ::ThrowIfFailed(E_FAIL, L"Dayo::CreateDescriptorHeap() for imgui failed ... ", __FILEW__, __LINE__);
	}
	ImGui_ImplDX12_Init(m_d3d12Device, backBufferCount/*dxr->BackBufferCount()*/,
		DXGI_FORMAT_R8G8B8A8_UNORM, m_pd3dSrvDescHeap.Get(),
		m_pd3dSrvDescHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
		m_pd3dSrvDescHeap.Get()->GetGPUDescriptorHandleForHeapStart());	
	
	// 日本語フォント読み込み
	char font_path[MAX_PATH];
	SHGetSpecialFolderPathA(NULL, font_path, CSIDL_FONTS, 0);
	std::string fontFolderPath = std::format("{:s}", font_path);
	fontFolderPath.append("\\meiryo.ttc");
	ImWchar const ranges[] = { 0x0020, 0xfffd, 0, };
	ImFont* font = io.Fonts->AddFontFromFileTTF(fontFolderPath.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	IM_ASSERT(font != nullptr);

	return true;
}

// imguiレンダリング処理
void DayoWorkspace::RenderWorkspace(ID3D12GraphicsCommandList* pd3dCommandList)
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	BuildWorkspace();

	ImGui::Render();

	ID3D12DescriptorHeap* ppHeaps[] = { m_pd3dSrvDescHeap.Get() };
	pd3dCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pd3dCommandList);

}

// imgui 構築処理
void DayoWorkspace::BuildWorkspace() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// 任意のImGui関数の呼び出し
	if (m_DayoWorkData.m_showWindow) {
		//ワークスペースウィンドウ作成
		ImGui::Begin("Workspace",&m_DayoWorkData.m_showWindow);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

		// Setting セクション
		ImGui::SeparatorText("Setting");
		ImGui::Checkbox("60fps Enable", &(m_DayoWorkData.m_fps60enable));
		ImGui::ColorEdit3("fog color", (float*)&m_DayoWorkData.fog_color, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float); // Edit 3 floats representing a color
		if (ImGui::Button("Physics reset")) {
			m_DayoWorkData.m_resetPhysics = true;
		}

		// Material セクション
		ImGui::SeparatorText("Material");
		ImGui::TextWrapped("Material Json : %s", wstring_to_utf8(m_DayoWorkData.m_materialJsonFileName).c_str());

		// マテリアル用Jsonファイルロードボタン
		if (ImGui::Button("Load Json file..")) {
			m_DayoWorkData.m_loadMaterialJsonReq = true;
		}

		// キャラクターモデル 各材質ごとの割り当てプリセット名表示　ツリーノード
		std::string model_name = std::format("Charactor material  [{:s}/{:s}]",
			wstring_to_utf8(m_DayoWorkData.pmx[chModelIdx]->name),
			wstring_to_utf8(m_DayoWorkData.pmx[chModelIdx]->name_e));
		if (ImGui::TreeNode(model_name.c_str())) {
			for (int idx = 0; idx < m_DayoWorkData.pmx[chModelIdx]->materials.size(); idx++)
			{
				std::string node_name = std::format("{:s}/{:s}", 
					wstring_to_utf8(m_DayoWorkData.pmx[chModelIdx]->materials[idx].name),
					wstring_to_utf8(m_DayoWorkData.pmx[chModelIdx]->materials[idx].name_e));
				if (ImGui::TreeNode(node_name.c_str())) {
					ImGui::Text("Preset : %s", wstring_to_utf8(m_DayoWorkData.matchPreset[chModelIdx][idx]).c_str());
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		// ステージモデル 各材質ごとの割り当てプリセット名表示　ツリーノード
		std::string stage_name = std::format("Stage material  [{:s}/{:s}]",
			wstring_to_utf8(m_DayoWorkData.pmx[stModelIdx]->name),
			wstring_to_utf8(m_DayoWorkData.pmx[stModelIdx]->name_e));
		if (ImGui::TreeNode(stage_name.c_str())) {
			for (int idx = 0; idx < m_DayoWorkData.pmx[stModelIdx]->materials.size(); idx++)
			{
				std::string node_name = std::format("{:s}/{:s}",
					wstring_to_utf8(m_DayoWorkData.pmx[stModelIdx]->materials[idx].name),
					wstring_to_utf8(m_DayoWorkData.pmx[stModelIdx]->materials[idx].name_e));
				if (ImGui::TreeNode(node_name.c_str())) {
					ImGui::Text("Preset : %s", wstring_to_utf8(m_DayoWorkData.matchPreset[stModelIdx][idx]).c_str());
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		ImGui::End();
		
	}
}
