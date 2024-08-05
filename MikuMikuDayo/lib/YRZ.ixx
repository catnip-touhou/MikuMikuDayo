module;
//DirectXTexが必要なのでよろしく


//ReleaseビルドでもD3Dデバッグレイヤーだけ使いたい場合はこれをon(パフォーマンスは低下します)
#define DEBUG_LAYER

//デバッグレイヤーからのメッセージをyrz.logに出力する
#define LOG_DEBUG_LAYER

//STL
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <format>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

//Windows
#include <atlcomcli.h>
#include <ImageHlp.h>
#include <wincodec.h>
#include <wrl.h>
#include <windowsx.h>
#include <dwmapi.h>

//DirectX
#include <d2d1_3.h>
#include <d3d11on12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dwrite.h>

#if defined(_DEBUG) || defined(DEBUG_LAYER)
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

//外部ライブラリ
#include <DirectXTex.h>

// imgui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//リソース
#include "resource.h"

//リンクしてほしいライブラリ
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "Dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "imagehlp.lib")
#pragma comment(lib, "Dwmapi.lib")


export module YRZ;


//このシンボルが有効な場合、exeファイルと同じディレクトリのyrz.logというファイルに動作ログが書き込まれる
//YRZ::LOG関数により、動作ログにアプリケーションから追記できる
//YRZ::DEB関数により、デバッグ出力が出来る
#define YRZ_LOGGING
#define YRZ_LOGFILENAME L"yrz.log"

//グローバル変数
extern bool YRZLogInitialized;


//文字列の扱いについて
// 
// よろずDXR内部では基本的にstd::wstring、wchar_t*で扱う。
// WinRTの奇怪なJSONとのやり取りなど至極稀な例外を除いてwchar_tに格納された文字はUTF-16 LEでエンコードされていると仮定する
//
// シェーダのソースコードなどテキストファイルに納められている物はUTF-8を想定する
// テキストファイルへの書き出しは逐次UTF-8に変換してBOM無しで書き出す
// 
// テキストファイルから読み込んだ文字コードの変換については、読み込んだ文字列を渡す外部API(シェーダコンパイラなど)の仕様によって決める
// よろず内部で使用する場合は読み込んだ直後にwstringで(UTF-16 LEに変換して)格納する
//
// 動作ログのファイルへの出力はUTF-8で出力する
// デバッグログ出力はYRZ::DEB関数を用い、UTF-16 LEで出力する、これはDebugビルドではスキップされる
// 例外オブジェクトでのメッセージ出力はstd::runtime_errorの都合により、ANSIで出力する

/*参考文献

Microsoft社 "Direct3D 12 raytracing samples"ほか、Direct2DなどMicrosoft社公式サンプル
https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-raytracing-samples-win32/

川野 竜一氏 "DirectX12の魔導書"
https://www.amazon.co.jp/dp/4798161934

techbito様 "DirectX Raytracing Programming Vol.1"
https://booth.pm/ja/items/3073983

Simon Coenen氏 "Using the DirectXShaderCompiler C++ API"
https://simoncoenen.com/blog/programming/graphics/DxcCompiling

わびさびサンプルソース様 "stringをwstringへ変換する"
https://www.wabiapp.com/WabiSampleSource/windows/string_to_wstring.html

Silicon Studio社 "DRED を使った D3D12 の GPU デバッグ手法"
https://blog.siliconstudio.co.jp/2021/08/1258/

Daniel Sieger氏 "Generating Platonic Solids in C++"
https://www.danielsieger.com/blog/2021/01/03/generating-platonic-solids.html

@ikiuo氏 "正多面体のデータを作る"
https://qiita.com/ikiuo/items/f5905c353858fc43e597

an-embedded-engineer氏
"C++でスタックトレース(関数コール履歴)の取得"
"C++で例外発生箇所(ファイルパス/ファイル行番号/関数名)の特定"
https://an-embedded-engineer.hateblo.jp/entry/2020/08/24/160436
https://an-embedded-engineer.hateblo.jp/entry/2020/08/24/212511

melak47氏 "BorderlessWindow"
https://github.com/melak47/BorderlessWindow

*/

#pragma warning(disable: 4267 4244 4305)


export namespace YRZ {

	using Microsoft::WRL::ComPtr;

	class D2D;

	//Yorozu関連オブジェクトからの例外
	class YRZException : public std::runtime_error
	{
	private:
		std::string message;
	public:
		YRZException(const std::string& msg) : runtime_error(msg) {};
	};

	//テキスト揃えのための列挙型
	enum class HA { left, center, right };
	enum class VA { top, middle, bottom };

	//WIC(png,jpgなど)とtga画像読み込み時のカラースペースについてのオプション
	//none : メタデータに指定があれば_SRGB有り、無ければ_SRGB無しフォーマットで作られる(DirectXTexのデフォルト)
	//srgb : メタデータに指定が無い場合も_SRGB付きフォーマットで作られる (ガンマsRGBで描かれたbaseColorマップなどに良い)
	//linear : メタデータがあっても_SRGB無しフォーマットで作られる (法線マップやroughnessマップなどに良い)
	//※_SRGB付きフォーマットのテクスチャはシェーダから読み取られる時にリニア色空間に自動的に変換される
	//  このため、法線マップなどが_SRGB付きフォーマットで作られてしまうと使い物にならなくなる
	enum class ColorSpace { none, srgb, linear };

	enum class ResType { buf, tex2D, tlas };

	//そのリソースは何ができるか？
	enum class ResCaps {
		none = 0,
		map = 0x01,	//Map可能(CPUから直接読みor書きor両方できる、CreateBufCPUで作られたバッファ, CB)
		uav = 0x02,	//UAVで利用可能←CreateRWxxxで作られたリソース
		rtv = 0x04,	//RTVで利用可能←CreateRT2Dで作られたテクスチャ
		dsv = 0x08,	//DSVで利用可能←CreateZBufで作られたテクスチャ
		srv = 0x10,	//SRVで利用可能←ほとんどのリソース(例外：ConstantBuffer,CreateZBufで作られたリソース)
		cbv = 0x20,	//CBVで利用可能←ConstantBuffer専用
		d2d = 0x40	//DirectX11,Direct2Dと相互利用可能
	};

	//リソースを格納する構造体の基本
	struct Res {
	public:
		ResCaps caps = ResCaps::none;
		ResType type = ResType::buf;
		ComPtr<ID3D12Resource> res;	//GPUに乗っかってるリソース
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_DESC desc() const { return res->GetDesc(); }
		std::wstring Name();
		//リソースに名前を付ける。デバッグ時に役立つ
		void SetName(const wchar_t* name);
		virtual void i_am_polymorphic() {};
	};

	//ConstantBuffer
	struct CB : public Res {
		UINT elemSize = 0;
		void* pData = nullptr;	//CBの中身への窓口
	};

	//2D Texture
	struct Tex2D : public Res {
	};

	//1D Buffer
	struct Buf : public Res {
		UINT elemSize = 0;
	};

	//Bottom level acceleration structure
	struct BLAS : public Res {
		ComPtr<ID3D12Resource> update;	//更新用リソース
		Buf AABBUploader = {};			//AABBアップロード用バッファ(プロシージャル用)
		D3D12_RAYTRACING_AABB* pAABB = nullptr;	//AABBアップロード用バッファの書き込み用アドレス
		//以下はTLAS作成時に使われる情報。適宜書き換えて使う
		DirectX::XMFLOAT3X4 transform = DirectX::XMFLOAT3X4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);
		UINT ID = 0;
		UINT mask = 0xFF;
		UINT contributionToHitGroupIndex = 0;
		D3D12_RAYTRACING_INSTANCE_FLAGS flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	};

	//Top level acceleration structure
	struct TLAS : public Res {
		ComPtr<ID3D12Resource> update;	//更新用リソース
		ComPtr<ID3D12Resource> instanceDescBuffer;	//更新用リソースその2
	};


	//ポストプロセス枠のための頂点フォーマット
	struct PostProcessVertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT2 uv;
	};

	//シェーダのエントリポイント名とシェーダブロブの組み
	struct Shader {
		std::wstring entrypoint = L"";
		ComPtr<IDxcBlob> blob = nullptr;
		Shader() {};
		Shader(const wchar_t* filename, const wchar_t* _entrypoint);	//コンパイル済みシェーダをファイルから読み込む
		void SaveToFile(const wchar_t* filename);
	};

	//レンダーターゲットとデプスステンシルのリソースとクリア情報を入れた物
	struct CV {
		bool clear = false;
		D3D12_CLEAR_FLAGS flags = {};
		D3D12_CLEAR_VALUE value = {};	//value.Formatは使われてない
		//デフォルト。クリアしない
		CV() {};
		//レンダーターゲットをcolでクリアする
		CV(const DirectX::XMFLOAT4& col) {
			clear = true;
			value.Color[0] = col.x; value.Color[1] = col.y; value.Color[2] = col.z; value.Color[3] = col.w;
		}
		//レンダーターゲットをr,g,b,aでクリアする
		CV(float r, float g, float b, float a) {
			clear = true;
			value.Color[0] = r; value.Color[1] = g; value.Color[2] = b; value.Color[3] = a;
		}
		//デプスバッファをdepthでクリアする
		CV(float depth) {
			clear = true;
			flags = D3D12_CLEAR_FLAG_DEPTH;  value.DepthStencil.Depth = depth;
		}
		//デプスステンシルバッファを奥行depth,ステンシルstencilでクリアする
		CV(float depth, UINT8 stencil) {
			clear = true;
			flags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
			value.DepthStencil.Depth = depth; value.DepthStencil.Stencil = stencil;
		}
	};
	

	//めんどくさがり向けWindowsアプリケーションを抽象化したクラス
	//インスタンスを複数作る事を防止する機構は何もないですがシングルトンです
	//「二つ以上作らないように、宜しく」って書いとけばシングルトンです
	class App {
	private:
		WNDCLASSEX m_windowClass;
		HWND m_hWnd;
	public:
		//メインウィンドウの作成
		//title : ウィンドウのタイトル文字列
		//width, height : クライアント領域の幅・高さ(ピクセル単位)
		//dwStyle : ウィンドウのスタイルを指定するフラグ(https://learn.microsoft.com/ja-jp/windows/win32/winmsg/window-styles 参照)
		App(const wchar_t* title, int width, int height, HINSTANCE hInstance, DWORD dwStyle = WS_OVERLAPPEDWINDOW);
		~App();
		//メインウィンドウへのハンドル
		HWND const hWnd() const { return m_hWnd; }

		//メインループを回す。falseになったらループを抜けるべし
		//msg : ウィンドウに届いたメッセージ
		bool MainLoop(MSG& msg ,HWND hDlg);

		// ボーダレス 切替
		bool borderless        = false; // is the window currently borderless
		bool borderless_resize = false; // should the window allow resizing by dragging the borders while borderless
	    bool borderless_drag   = true; // should the window allow moving my dragging the client area
	    bool borderless_shadow = true; // should the window display a native aero shadow while borderless
		auto set_borderless(bool enabled) -> void;
		// ボーダレスウィンドウ 影 切替
		auto set_borderless_shadow(bool enabled) -> void;
		// ボーダレスウィンドウ hit_test
		auto hit_test(POINT cursor) const -> LRESULT;

	};

	//DirectX Graphicsを抽象化したクラス
	class DXR {
		const static int m_BBcount = 2;		//バックバッファの数
		const static DXGI_FORMAT m_BBformat = DXGI_FORMAT_R8G8B8A8_UNORM;
		const static int m_UDTileSize = 256;	//画像をアップロード・ダウンロードする時のタイルの一辺のサイズ
		const static int m_UDSize = m_UDTileSize * m_UDTileSize * 16;	//アップロード・ダウンロードバッファのサイズ
	private:
		bool m_raytracingSupport = false;	//レイトレサポートは必須か

		//Raytracing、PostProcess用
		ComPtr<IDXGIFactory4> m_factory;
		ComPtr<IDXGIAdapter1> m_adapter;
		ComPtr<ID3D12Device5> m_device;
		ComPtr<ID3D12GraphicsCommandList4> m_cmdList;
		ComPtr<ID3D12CommandQueue> m_cmdQueue;
		ComPtr<ID3D12CommandAllocator> m_cmdAlloc[m_BBcount];
		ComPtr<IDXGISwapChain3> m_swapChain;
		ComPtr<ID3D12Resource> m_backBuffer[m_BBcount];
		ComPtr<ID3D12DescriptorHeap> m_backBufferDH;	//バックバッファのためのディスクリプタヒープ
		D3D12_CPU_DESCRIPTOR_HANDLE m_backBufferCPUHandle[m_BBcount];
		UINT m_backBufferIndex = 0;
		ComPtr<ID3D12Fence> m_fence;
		UINT64 m_fenceValues[2] = { 0, 0 };
		Microsoft::WRL::Wrappers::Event m_fenceEvent;

		//ComputeShader用
		ComPtr<ID3D12CommandAllocator> m_CSAlloc;
		ComPtr<ID3D12GraphicsCommandList> m_CSList;
		ComPtr<ID3D12CommandQueue> m_CSQueue;
		ComPtr<ID3D12Fence> m_CSFence;
		UINT64 m_CSFenceValue = 0;
		Microsoft::WRL::Wrappers::Event m_CSFenceEvent;

		Buf m_PostProcessVB;	//ポストプロセスのための画面枠
		Buf m_uploadBuf;		//GPUとのアップロード・ダウンロードのためのバッファ
		Buf m_downloadBuf;

		//シェーダキャッシュ
		std::map<std::wstring, Shader>m_shaderCache;

		// DirectX Raytracing (DXR) attributes
		ComPtr<ID3D12StateObject> m_dxrStateObject;

		//UINT m_frameIndex = 0;

		HWND m_hWnd;
		int m_width, m_height;

		void CreateDevice(D3D_FEATURE_LEVEL featureLevel);
		void CreateWindowSizeDependentResources();

		void MoveToNextFrame();
		void HandleDeviceLost();

		ComPtr<ID3D12Resource> AllocateUploadBuffer(const void* pData, UINT64 datasize);
		void AllocateUAVBufferAligned(UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState, size_t align);
		void AllocateUAVBuffer(UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON);
		void BuildASFromScratchBuffer(ComPtr<ID3D12Resource>& xlas, ComPtr<ID3D12Resource>& scratchResource, D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc);
		BLAS BuildBLASFlow(D3D12_RAYTRACING_GEOMETRY_DESC& gd);
		void UpdateBLASFlow(BLAS& blas, D3D12_RAYTRACING_GEOMETRY_DESC& gd);
	public:
		/*** コンストラクタ・デストラクタ ***/
		DXR(HWND hWnd, bool raytracing = true, D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0);
		~DXR();

		/*** システム応答 ***/
		//バックバッファのサイズを変更する
		void Resize(int width, int height);

		/*** メンバ読み出し用 ***/
		//D3D12Device
		ComPtr<ID3D12Device5> Device() const { return m_device; };
		//CommandList(Raystacing, PostProcess, Rasterizer, Direct2D用)
		ComPtr<ID3D12GraphicsCommandList4> CommandList() const { return m_cmdList; }
		//ComputeShader用CommandList
		ComPtr<ID3D12GraphicsCommandList> CommandListCS() const { return m_CSList; }
		//CommandAllocator
		ComPtr<ID3D12CommandAllocator> CommandAlloc(int index) const { return m_cmdAlloc[index]; }
		//ComputeShader用CommandAllocator
		ComPtr<ID3D12CommandAllocator> CommandAllocCS() const { return m_CSAlloc; }
		//CommandQueue
		ComPtr<ID3D12CommandQueue> CommandQueue() const { return m_cmdQueue; }
		//ComputeShader用CommandQueue
		ComPtr<ID3D12CommandQueue> CommandQueueCS() const { return m_CSQueue; }
		//Fence
		ComPtr<ID3D12Fence> Fence() const { return m_fence; }
		//ComputeShader用Fence
		ComPtr<ID3D12Fence> FenceCS() const { return m_CSFence; }
		//スワップチェーンと関連付けられたウィンドウのハンドル
		HWND hWnd() const { return m_hWnd; }
		//バックバッファの幅
		int Width() const { return m_width; }
		//バックバッファの高さ
		int Height() const { return m_height; }
		//バックバッファ、ステートを変更した場合はなるべく早めにPRESENTに戻してね
		ComPtr<ID3D12Resource> BackBuffers(int index) const { return m_backBuffer[index]; }
		//バックバッファの数(現状では2に固定)
		int BackBufferCount() const { return m_BBcount; }
		//これから表示される(今は表示されていない方の)バックバッファのインデックス(0か1)
		int BackBufferIndex() const { return m_backBufferIndex; }
		//これから表示される(今は表示されていない方の)バックバッファ
		ComPtr<ID3D12Resource> CurrentBackBuffer() const { return m_backBuffer[m_backBufferIndex]; }
		//バックバッファのフォーマット
		DXGI_FORMAT BackBufferFormat() const { return m_BBformat; }
		//バックバッファのCPUハンドル
		D3D12_CPU_DESCRIPTOR_HANDLE BackBufferCPUHandle(int index) const { return m_backBufferCPUHandle[index]; }
		//初期化時にレイトレーシングをサポートする、として初期化されたか？
		bool RaytracingSupport() const { return m_raytracingSupport; }
		//ポストプロセス用画面枠を格納したVertexBuffer
		Buf PostProcessVB() const { return m_PostProcessVB; }


		/*** 制御 ***/
		//コマンドリストをリセットしコマンド書き込み可能な状態にする
		//既にコマンドリストが開いている場合、先にExecuteCommandList()しなければならない
		void OpenCommandList();
		//コマンドリストをクローズしコマンド実行を開始する
		//waitfor ... true:完了まで待つ false:完了を待たない(明示的にWaitForGPU()を呼ぶ必要がある)
		void ExecuteCommandList(bool waitfor = true);
		//ExecuteCommandListの結果を待つ。完了後、次のOpenCommandListが可能になる
		void WaitForGPU() noexcept;
		//これまで描画対象にしていたバッファと表示されているバッファを交代する
		void Present(UINT syncInterval = 1, UINT flags = 0);

		//ComputeShader用コマンドリストをリセットしコマンド書き込み可能状態にする
		//既にComputeShader用コマンドリストが開いている場合、先にExecuteCommandListCS()しなければならない
		void OpenCommandListCS();
		//ComputeShader用コマンドリストをクローズしコマンド実行を開始する
		//waitfor ... true:完了まで待つ false:完了を待たない(明示的にWaitForCS()を呼ぶ必要がある)
		void ExecuteCommandListCS(bool waitfor = true);
		//ComputeShaderの実行完了を待つ。完了後、次のOpenCommandListCSが可能になる
		void WaitForCS() noexcept;

		/*** リソース作成 ***/
		//1Dのバッファ(VertexBuffer,IndexBufferなど)を作る(dataがnullptrの場合は空のバッファが出来る)
		//dataがnullptr以外の場合はコマンドリストが閉じていないと実行できない
		Buf CreateBuf(const void* data, UINT elemsize, UINT count, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//Mapを使ってCPUから読み書き可能だが低速なバッファ(dataがnullptrの場合は空のバッファが出来る)
		Buf CreateBufCPU(const void* data, UINT elemsize, UINT count, bool upload = true, bool download = true, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//GPUから読み書き可能なバッファ(CPUから直接の読み書きできない)
		Buf CreateRWBuf(UINT elemsize, UINT count, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//ConstantBufferを作成する dataがnullptrの場合は空のバッファが出来る
		CB CreateCB(const void* data, UINT elemsize, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//2Dのテクスチャを画像ファイルから作成する(CPUからの読み書きにはUpload,Downloadを使う)
		//コマンドリストが閉じていないと実行できない
		Tex2D CreateTex2D(const wchar_t* filename, ColorSpace opt = ColorSpace::none, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//サイズを指定して空のテクスチャを作る
		Tex2D CreateTex2D(int width, int height, DXGI_FORMAT fmt, int mipLevels = 1, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//シェーダから読み書き両用のテクスチャを作る(CPUからの読み書きにはUpload,Downloadを使う)
		Tex2D CreateRWTex2D(int width, int height, DXGI_FORMAT fmt, int mipLevels = 1, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//レンダーターゲット
		Tex2D CreateRT2D(int width, int height, DXGI_FORMAT fmt, int mipLevels = 1, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//デプスステンシルバッファの作成
		Tex2D CreateZBuf(int width, int height, DXGI_FORMAT fmt, int mipLevels = 1, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);
		//AABBからBLASを作る(プロシージャル用)

		/*** シェーダコンパイル ***/
		// シェーダのコンパイル hlslのコードはUTF-8で書いてね
		Shader CompileShader(const wchar_t* filename, const wchar_t* entrypoint, const wchar_t* target, const std::vector<LPCWSTR>& options = {});
		//メモリに置かれたシェーダのコンパイル。nameはコンパイルエラー時に表示される名前
		Shader CompileShaderMemory(const void* pShaderSource, size_t shaderSourceSize, IDxcIncludeHandler* pIncludeHandler, const wchar_t* name, const wchar_t* entrypoint, const wchar_t* target, const std::vector<LPCWSTR>& options = {});

		/*** ユーティリティ ***/
		//r->state!=afterの時、リソースバリアリストbarに対してrの状態をafterに遷移させるためのバリアを追加し、r->stateをafterに書き換える
		void AddBarrier(Res* r, std::vector<D3D12_RESOURCE_BARRIER>& bar, D3D12_RESOURCE_STATES after) const;

		/*** コマンドリストが開いていないと実行できない関数群 ***/
		/* BLAS,TLASの更新 */
		//BLASの更新
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void UpdateBLAS(BLAS& blas, int aabbCount, const D3D12_RAYTRACING_AABB* aabb, D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
		//BLASの更新
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void UpdateBLAS(BLAS& blas, Buf& VB, Buf& IB, DXGI_FORMAT positionFormat = DXGI_FORMAT_R32G32B32_FLOAT, D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
		//TLASの更新
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void UpdateTLAS(TLAS& tlas, int count, const BLAS* BLASs);
		/* リソースコピー */
		//リソースのコピー。フォーマットとサイズの同じリソース間でのみコピー可
		//コピー前の状態をdestState,srcStateに指定する。コピー中はCOPY_SRC/DESTになり、完了後、指定されたステートに戻る
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void CopyResource(ID3D12Resource* dest, D3D12_RESOURCE_STATES destState, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState);
		//リソースのコピー。フォーマットとサイズの同じリソース間でのみコピー可
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void CopyResource(Res& dest, Res& src);
		//バックバッファからdestへコピー、indexに-1を指定すると「最後にレンダリングされたバックバッファ」からコピー
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void CopyResourceFromBB(Tex2D& dest, int index = -1);
		//srcからバックバッファへコピー。-1を指定すると「これからレンダリングされるバックバッファ」へコピー
		//コマンドキューにコマンドを発行するのでコマンドリストが開いていないと実行できない
		void CopyResourceToBB(Tex2D& src, int index = -1);

		/*** コマンドリストが閉じていないと実行できない関数群 ***/
		//BLASの作成、プロシージャルジオメトリ用
		//コマンドリストが閉じていないと実行できない
		BLAS BuildBLAS(int aabbCount, const D3D12_RAYTRACING_AABB* aabb, D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
		//1つずつのVBとIBからBLASを作成する(三角ポリゴン用)
		//コマンドリストが閉じていないと実行できない
		BLAS BuildBLAS(Buf& VB, Buf& IB, DXGI_FORMAT positionFormat = DXGI_FORMAT_R32G32B32_FLOAT, D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
		//count個のBLASからTLASを作成する
		//コマンドリストが閉じていないと実行できない
		TLAS BuildTLAS(int count, const BLAS* BLASs, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE);
		//GPUに格納されたsrcの内容をCPU側のメモリdestにコピーする
		//コマンドリストが閉じていないと実行できない
		void Download(void* dest, Buf& src, size_t destStartInBytes = 0, size_t srcStartInBytes = 0, size_t countInBytes = std::wstring::npos);
		//CPU側のメモリに格納されたsrcの内容をGPU側のメモリdestにコピーする
		//コマンドリストが閉じていないと実行できない
		void Upload(Buf& dest, const void* src, size_t destStartInBytes = 0, size_t srcStartInBytes = 0, size_t countInByte = std::wstring::npos);
		//GPUに格納されたsrcの内容をCPU側のメモリdestにコピーする
		//コマンドリストが閉じていないと実行できない
		void Download(void* dest, Tex2D& src, int subResource = 0, int mip = 0, size_t destX = 0, size_t destY = 0, const D3D12_BOX* srcbox = nullptr);
		//CPU側のメモリに格納されたsrcの内容をGPU側のメモリdestにコピーする
		//コマンドリストが閉じていないと実行できない
		void Upload(Tex2D& dest, const void* src, int subResource = 0, int mip = 0, size_t destX = 0, size_t destY = 0, const D3D12_BOX* srcbox = nullptr);
		//テクスチャの内容を保存 .dds .jpg .pngのみ対応。dds形式を選択するとミップマップ込みで保存できる
		//コマンドリストが閉じていないと実行できない
		void SaveTex2DToFile(Tex2D& tex, const wchar_t* filename);
		//バックバッファの内容のスクショを取る .dds .jpg .pngのみ対応
		//コマンドリストが閉じていないと実行できない
		void Snapshot(const wchar_t* filename);
		//srcからmipmapチェーン付きのRTV対応テクスチャを生成する(UAV非対応)
		//出力テクスチャは入力テクスチャと同じフォーマットを持つ
		//converToLinearがtrueの時、ガンマ→リニア色変換が行われる(結果は入力ピクセルの約2.2乗した値になる)
		//コマンドリストが閉じていないと実行できない
		Tex2D GenerateMipmap(Tex2D& src, bool converToLinear = false);
		//sRGBガンマ色空間で描かれたテクスチャsrcをリニア色空間に変換されたRTV対応テクスチャにする(UAV非対応)
		//結果は入力ピクセルの約2.2乗した値になる
		//コマンドリストが閉じていないと実行できない
		Tex2D ToLinear(Tex2D& src);

		//↓はDirect2D対応に伴い削除されました
		//テキストの描き込まれたテクスチャを作る(サイズはちょうど収まるように自動的に計算される、改行こみで作れるため各行の水平揃えの指定が出来る)
		//Tex2D CreateTextTexture(const wchar_t* str, const LOGFONTW& logfont, HA halign = HA::left, const wchar_t* name = L"");
		//指定サイズでテキストの描き込まれたテクスチャを作る
		//Tex2D CreateTextTexture(int width, int height, HA halign, VA valign, const wchar_t* str, const LOGFONTW& logfont, const wchar_t* name = L"");
	};

	//PassMakerで作ったパスの種類
	enum class PassType { none = 0, raytracing = 1, postprocess = 2, rasterizer = 3, compute = 4 };

	//ヒットグループ情報
	struct HitGroup {
		D3D12_HIT_GROUP_TYPE type = D3D12_HIT_GROUP_TYPE_TRIANGLES;	//ポリゴン用か、プロシージャル用か
		std::wstring closesthit;
		std::wstring anyhit;
		std::wstring intersection;	//プロシージャル用の場合はintersectionの指定必須。ポリゴン用の場合はintersection.blobはnullptrにすべし
		HitGroup(const wchar_t* _closesthit) { closesthit = _closesthit;  };
		HitGroup(const wchar_t* _closesthit, const wchar_t* _anyhit) { closesthit = _closesthit; anyhit = _anyhit; };
		HitGroup(const wchar_t* _closesthit, const wchar_t* _anyhit, const wchar_t* _intersection) {
			type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE; closesthit = _closesthit; anyhit = _anyhit; intersection = _intersection;
		};
	};


	//パスを作った時の状態保持用ポストプロセス用
	struct PassSaverPP {
		Shader vs, ps;
	};

	//パスを作った時の状態保持用ComputeShader用
	struct PassSaverCS {
		Shader cs;
	};

	//パスを作った時の状態保持用レイトレーシング用
	struct PassSaverRT {
		Shader raygen;
		std::vector<std::wstring> miss;
		std::vector<HitGroup> hitgroup;
		std::vector<std::wstring> callable;
		UINT maxPayloadSize=0, maxAttributeSize=0, maxRecursionDepth=0;
	};
	//パスを作った時の状態保持用ラスタライザ用
	struct PassSaverRaster {
		Shader vs = {}, ps = {};
		std::vector<Buf*> vbs, ibs;
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
		D3D12_BLEND_DESC blendDesc = {};
		D3D12_RASTERIZER_DESC rasterizerDesc = {};
		D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
		D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType = {};
		UINT sampleMask = 0;
	};

	//RTVに放り込むアイテム
	struct RTVItem {
		//書き込み対象のレンダーターゲットテクスチャ
		Tex2D* tex;
		//レンダリング直前にクリアする/しない、する場合はどんな値？という設定値。デフォルトではクリアしない設定になります
		CV cv = {};
		//書き込み対象のmiplevel
		int mipSlice = 0;
	};

	//DSVに放り込むアイテム
	struct DSVItem {
		//書き込み対象のデプスステンシルテクスチャ
		Tex2D* tex;
		//レンダリング直前にクリアする/しない、する場合はどんな値？という設定値。デフォルトではクリアしない設定になります
		CV cv = {};
		//書き込み対象のmiplevel
		int mipSlice = 0;
	};

	//UAVに放り込むアイテム
	struct UAVItem {
		//読み書き対象のリソース
		Res* res;
		//読み書き対象のmiplevel、resがBufの場合は無視されます
		int mipSlice = 0;	
	};

	//これから作るパスで使いたいリソースを放り込むとRootsigとかを作ってくれるヤツ
	class Pass {
		void* m_savepoint = nullptr;
		DXR* m_dxr;
		PassType m_type = PassType::none;

		CV m_BBcv;	//レンダリング時のクリア設定

		//↑のリソースが↓2つの入れ物のどれかに入る(RWTex以外はSRVに入る)
		std::vector<UINT> m_RPIdx;	//各ルートパラメータに対応するディスクリプタヒープ上の位置

		//Raytracing, PostProcess用
		ComPtr<ID3D12RootSignature>	m_rootSig;				//ルートシグネチャ
		ComPtr<ID3D12RootSignature>	m_localRootSig;			//空のローカルルートシグネチャ
		ComPtr<ID3D12StateObject> m_PSO;		//レイトレーシング用
		ComPtr<ID3D12StateObjectProperties> m_PSOprop;
		ComPtr<ID3D12PipelineState> m_PSOPP;	//ポストプロセス用
		ComPtr<ID3D12DescriptorHeap> m_descHeap;			//SRV,CBV,UAVのディクスリプタヒープ
		ComPtr<ID3D12Resource>	m_shaderTable;
		ComPtr<ID3D12DescriptorHeap> m_descHeapRTV;			//RTVのディクスリプタヒープ
		ComPtr<ID3D12DescriptorHeap> m_descHeapDSV;			//DSVのディクスリプタヒープ
		D3D12_DISPATCH_RAYS_DESC m_dispatchRaysDesc;		//RaytracingPassの中で大体作られる
		
		//ComputeShader用
		ComPtr<ID3D12PipelineState> m_CSPSO;

		//Rasterizer用
		std::vector<Buf*> m_rasterVBs;
		std::vector<Buf*> m_rasterIBs;

		void CreateRootSignature();	//パス作成の途中までやる(xxPass関数から呼ばれる)

		//リソースの登録と置換
	public:
		Pass(DXR* yorozu);
		~Pass();

		//リソース名簿のクリア
		//keepSaverがtrueの時は最後にRaytracingPassメソッド等でパスを作成した際の引数の設定(シェーダなど)についての情報は保持される
		//falseの時は上記の設定もクリアされ、パス未作成の状態に戻る
		void Flush(bool keepSaver = true);
		

		//リソース名簿。リソースへの参照を生ポインタで入れとく。リソースが解放された後にRenderすると無事クラッシュする
		//そういうリスクがあるのに実体のコピーではなくポインタが入ってるのはなぜか？というとリソースにはstateがあり、
		//Render()の実行などに伴ってstateが変わる事を追跡できないといけないので

		//ShaderResourceViewでシェーダから読み込まれるリソース群
		//SRV[x]でspace(x)からアクセスできる
		std::vector<std::vector<Res*>>SRV;
		//RenderTagterViewでシェーダから書き込まれるリソース群
		std::vector<RTVItem>RTV;
		//DepthStencilViewでシェーダから書き込まれるリソース群
		std::vector<DSVItem>DSV;
		//UnorderedAccessViewでシェーダから読み書きされるリソース群
		std::vector<UAVItem>UAV;
		//StaticSampler群
		std::vector<D3D12_STATIC_SAMPLER_DESC>Samplers;
		//ConstantBufferViewでシェーダから読み込まれるリソース群
		std::vector<CB*>CBV;

		//レイトレーシングパス作成 全部の物体を同じシェーダで描く版
		//※全部のシェーダは一つのライブラリから生成されねばならない
		//void RaytracingPass(const Shader& raygen, const wchar_t* miss, const wchar_t* closesthit, const wchar_t* anyhit, const wchar_t* intersection, const std::vector<std::wstring>& callable, UINT maxPayloadSize, UINT maxAttributeSize, UINT maxRecursionDepth);
		
		//レイトレーシングパス作成
		//※全部のシェーダは一つのライブラリから生成されねばならない
		void RaytracingPass(const Shader& raygen, const std::vector<std::wstring>& miss, const std::vector<HitGroup>& hitgroup, const std::vector<std::wstring>& callable, UINT maxPayloadSize, UINT maxAttributeSize, UINT maxRecursionDepth);
		void PostProcessPass(const Shader& vs, const Shader& ps, bool bAlphaBlendAdd = false);
		//ラスタライザパス作成。vbsとibsの要素数は同じでなければならない
		void RasterizerPass(const Shader& vs, const Shader& ps, const std::vector<Buf*>& vbs, const std::vector<Buf*>& ibs,
			const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElementDescs,
			const D3D12_BLEND_DESC& blendDesc, const D3D12_RASTERIZER_DESC& rasterizerDesc,
			const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc,
			const D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			const UINT sampleMask = D3D12_DEFAULT_SAMPLE_MASK);
		//コンピュートパス作成
		void ComputePass(const Shader& cs);
		
		//ReplaceXXXでリソースオブジェクトの差し替えをやった後の状態を反映する
		void Update(bool bAlphaBlendAdd = false);
		
		//Render時のバックバッファのクリアの設定
		void BBClearSetting(bool clear = true, const DirectX::XMFLOAT4& clearValue = { 0,0,0,1 });
		//レンダーターゲットの大きさを指定してレンダリング
		//コマンドリストが開いていないと実行できない
		void Render(int width, int height, int depth = 1);
		// レンダーターゲットのサイズは以下の優先順で決められる
		// 1.RTVに何か入っている場合はRTV[0]に入れられたテクスチャのサイズ
		// 2.バックバッファのサイズ
		//コマンドリストが開いていないと実行できない
		void Render();
		//ComputeShader起動
		//ComputeShader用のコマンドリストが開いていないと実行できない
		void Compute(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ);

		//デバッグ補助用
		//異常が見つけられなかった場合(正常とは言ってない)はokstrが返り、異常が見つかった場合はエラーメッセージを返す
		//パスに放り込まれている物がおかしくないか以下の項目だけチェックする
		//1.RaytracingPass()などは実行済みか？
		//2.リソースビュー不一致は無いか(DSVにCreateZBufで作られたテクスチャ以外の物が放り込まれていないか等)
		//3.各リソース及びリソースがカプセル化しているID2DResourceがnullptrで無いか
		//resourceDescにリソース一覧が返る
		//※リソース一覧出力中にクラッシュする場合はリソースは解放されているがnullptrが代入されていないという事
		std::wstring Check(std::wstring& resourceDesc, const std::wstring& okstr = L"OK");
		
		//HLSLのリソースバインディングをファイルに出力(あんまり役に立たないので一旦隠しておく)
		//void OutputResourceBinding(const wchar_t* filename);

		// DescriptorHeaps(SRV,CBV,UAVのディクスリプタヒープ)へのポインタ
		//ID3D12DescriptorHeap* GetSrvHeap();
	};



	//CreateRT2Dで作られたテクスチャをDirect2Dのレンダーターゲットまたはビットマップに出来るようにするためのもの
	class RTWrapper {
	private:
		Tex2D* m_wrappedRT = nullptr;
		DXR* dxr = nullptr;
		D2D* d2d = nullptr;
		bool m_wrapping = false;
		ComPtr<ID3D11Resource> m_res;
		ComPtr<ID2D1Bitmap1> m_bmp;
	public:
		RTWrapper() {};
		RTWrapper(DXR* _dxr, D2D* _d2d) : dxr(_dxr), d2d(_d2d) {};
		RTWrapper(DXR* _dxr, D2D* _d2d, Tex2D& rt) : dxr(_dxr), d2d(_d2d) { Wrap(rt); };
		~RTWrapper();
		//CreateRTで作られたレンダーターゲットテクスチャにDirect2Dで読みor書きできるようにする
		//renderTarget
		// true : Direct2Dのレンダーターゲットにできるがビットマップとして読み込めない
		// false: Direct2Dのビットマップとして読み込めるがレンダーターゲットに出来ない
		void Wrap(Tex2D& rt, bool renderTarget = true);
		void Wrap(ComPtr<ID3D12Resource> d12res, bool renderTarget = true);
		//Wrapを解除する
		void Unwrap();
		//WrapされたレンダーターゲットをDirect2Dのレンダーターゲットとして使えるよう準備する
		void Aquire();
		//Direct2Dのレンダーターゲットとして使っていたのを戻す
		void Release();
		//ラップされたDirectX11互換リソース
		auto res() const { return m_res; }
		//ラップされたDirect2D互換ビットマップリソース
		auto bmp() const { return m_bmp; }
	};

	//D3D11on12,Direct2Dを使用するためのクラス
	class D2D {
	private:
		DXR* dxr;
		ComPtr<ID3D11On12Device> m_d3d11On12Device;
		ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
		ComPtr<ID2D1Factory3>m_d2dFactory;
		ComPtr<ID2D1Device2>m_d2dDevice;
		ComPtr<ID2D1DeviceContext2>m_d2dDeviceContext;
		ComPtr<IDWriteFactory>m_dWriteFactory;
		std::vector<RTWrapper>m_wrappers;	//バックバッファをD2Dで書き込めるようにするためのラッパー
		bool m_RTisBB;				//バックバッファを対象にしたBeginDrawが行われた？
		RTWrapper* m_targetWrapper;	//BeginDrawの対象のラッパー
	public:
		D2D(DXR* _dxr);
		~D2D();
		//バックバッファをDirect2Dのレンダーターゲットとして使えるようにする
		//バックバッファへの参照カウンタが増えるのでresizeの前にUnwrapBackBuffers()する必要がある
		void WrapBackBuffers();
		//バックバッファをDirect2Dのレンダーターゲットにするのをやめる
		void UnwrapBackBuffers();
		//DirectX11,Direct2Dと互換性のあるD3D12レンダーターゲットを作成する
		//現状で分かっている範囲ではRTVに入れられるテクスチャでないとDirect2Dから読み書きのどちらをやるかを問わず「Direct2Dと互換性が無い」と言われる
		Tex2D CreateCompatibleRT(int width, int height, DXGI_FORMAT fmt);

		//テキストを描き込んだテクスチャを作る
		Tex2D CreateTextTexture(
			const wchar_t* str, const wchar_t* fontFamily, float fontSize, float maxWidth, float maxHeight,
			const wchar_t* locale = nullptr,
			DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH fontStretch = DWRITE_FONT_STRETCH_NORMAL, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
		Tex2D CreateTextTexture(const wchar_t* str, ComPtr<IDWriteTextFormat>format, ComPtr<IDWriteTextLayout>layout, ComPtr<ID2D1Brush>brush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
		
		//targetを対象にDirect2Dによる描画を開始する
		void BeginDraw(RTWrapper& target);
		//バックバッファを対象にDirect2Dによる描画を開始する
		void BeginDraw();
		//Direct2Dによる描画の完了
		void EndDraw();

		//バックバッファのx,yを左上隅とする位置にテキストstrを描画する
		//便宜的に単発で描画を行うヘルパー関数
		//BeginDraw～EndDrawの間には実行できない
		void Print(const wchar_t* str, float x, float y, const wchar_t* fontFamily = L"System", float fontSize = 24, const D2D_COLOR_F& color = {1,1,1,1}, HA halign = HA::left, VA valign = VA::top);

		//バックバッファにビットマップを描画する
		//便宜的に単発で描画を行うヘルパー関数
		//BeginDraw～EndDrawの間には実行できない
		void Stamp(ComPtr<ID2D1Bitmap1> bmp, float x, float y);

		//バックバッファをcolorでクリアする
		//便宜的に単発で描画を行うヘルパー関数
		//BeginDraw～EndDrawの間には実行できない
		void Clear(const D2D_COLOR_F& color = {0,0,0,1});

		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<IDWriteTextFormat>TextFormat(const wchar_t* fontFamily, float size,
			DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL, const wchar_t* locale = nullptr);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<IDWriteTextLayout>TextLayout(const wchar_t* str, ComPtr<IDWriteTextFormat> format, float maxWidth, float maxHeight);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<ID2D1SolidColorBrush>SolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES& props);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<ID2D1SolidColorBrush>SolidColorBrush(const D2D1_COLOR_F& color);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<ID2D1LinearGradientBrush>LinearGradientBrush(float ax, float ay, float bx, float by, const std::vector<D2D1_GRADIENT_STOP>& stops);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<ID2D1RadialGradientBrush>RadialGradientBrush(float centerx, float centery, float offsetx, float offsety, float radiusx, float radiusy, const std::vector<D2D1_GRADIENT_STOP>& stops);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<ID2D1BitmapBrush1>BitmapBrush(ComPtr<ID2D1Bitmap1> bmp);
		//テキスト表示のためのインターフェイス作成ヘルパー
		ComPtr<ID2D1BitmapBrush1>BitmapBrush(ComPtr<ID2D1Bitmap1> bmp, const D2D1_BITMAP_BRUSH_PROPERTIES1& props);
		//ファイルからID2DBitmapを作る。bmpだけでなくpngやjpgなども対応してるっぽい
		ComPtr<ID2D1Bitmap1>CreateBitmap(const wchar_t* filename);

		//何も指定されなかった時のデフォルトのロケール(初期値は L"ja-JP" )
		std::wstring DefaultLocale;
		
		//D3D11onD12Device
		auto D3DDevice() const { return m_d3d11On12Device; }
		//D3D11DeviceContext
		auto D3DDC() const { return m_d3d11DeviceContext; }
		//D2DDevice
		auto D2DDevice() const { return m_d2dDevice; }
		//D2DDeviceContext
		auto DC() const { return m_d2dDeviceContext; }
		//DWriteFactory
		auto DWFactory() const { return m_dWriteFactory; }
		//D3D11用リソースとしてラップされたバックバッファ
		//WrapBackBuffers()が実行されていない場合は無効な値が返る
		auto BackBuffers(int idx) const { return m_wrappers[idx].res(); }
		//D3D11用リソースとしてラップされたバックバッファ
		//WrapBackBuffers()が実行されていない場合は無効な値が返る
		auto CurrentBackBuffer() const { return m_wrappers[dxr->BackBufferIndex()].res(); }
		//Direct2D用ビットマップとしてラップされたバックバッファ
		//WrapBackBuffers()が実行されていない場合は無効な値が返る
		auto BackBufferBitmaps(int idx) const { return m_wrappers[idx].bmp(); }
		//Direct2D用ビットマップとしてラップされたバックバッファ
		//WrapBackBuffers()が実行されていない場合は無効な値が返る
		auto CurrentBackBufferBitmap() const { return m_wrappers[dxr->BackBufferIndex()].bmp(); }
	};



	/************************************/
	// ボーダレスウィンドウ関連
	/************************************/
	// we cannot just use WS_POPUP style
	// WS_THICKFRAME: without this the window cannot be resized and so aero snap, de-maximizing and minimizing won't work
	// WS_SYSMENU: enables the context menu with the move, close, maximize, minize... commands (shift + right-click on the task bar item)
	// WS_CAPTION: enables aero minimize animation/transition
	// WS_MAXIMIZEBOX, WS_MINIMIZEBOX: enable minimize/maximize
	enum class Style : DWORD {
	    windowed = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
	    aero_borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
	    basic_borderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
	};

	auto maximized(HWND hwnd) -> bool {
	    WINDOWPLACEMENT placement;
	    if (!::GetWindowPlacement(hwnd, &placement)) {
	        return false;
	    }

	    return placement.showCmd == SW_MAXIMIZE;
	}

	/* Adjust client rect to not spill over monitor edges when maximized.
	    * rect(in/out): in: proposed window rect, out: calculated client rect
	    * Does nothing if the window is not maximized.
	    */
	auto adjust_maximized_client_rect(HWND window, RECT& rect) -> void {
	    if (!maximized(window)) {
	        return;
	    }

	    auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
	    if (!monitor) {
	        return;
	    }

	    MONITORINFO monitor_info{};
	    monitor_info.cbSize = sizeof(monitor_info);
	    if (!::GetMonitorInfoW(monitor, &monitor_info)) {
	        return;
	    }

	    // when maximized, make the client area fill just the monitor (without task bar) rect,
	    // not the whole window rect which extends beyond the monitor.
	    rect = monitor_info.rcWork;
	}

	auto last_error(const std::string& message) -> std::system_error {
	    return std::system_error(
	        std::error_code(::GetLastError(), std::system_category()),
	        message
	    );
	}

	auto composition_enabled() -> bool {
	    BOOL composition_enabled = FALSE;
	    bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
	    return composition_enabled && success;
	}

	auto select_borderless_style() -> Style {
	    return composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
	}

	auto set_shadow(HWND handle, bool enabled) -> void {
	    if (composition_enabled()) {
	        static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
	        ::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
	    }
	}

	/************************************/
	// ユーテリィティ
	/************************************/
	/*
	//wide文字列で書式付きデバッグログ出力
	void LOG(std::wstring format, ...);
	//ANSI文字列で書式付きデバッグログ出力
	void LOGA(std::string format, ...);
	//utf-8エンコードされたstringで書式付きデバッグログ出力
	void LOGU(std::string format, ...);
	*/

	//wide文字列でデバッグログ出力、改行を追加する
	template<typename... Args>
	void DEB(std::wformat_string<Args...> s, Args&&... args)
	{
		auto str = std::format(s, std::forward<Args>(args)...) + L"\n";
		OutputDebugStringW(str.c_str());
	}
	
	//ANSI文字列でデバッグログ出力、改行を追加する
	template<typename... Args>
	void DEBA(std::format_string<Args...> s, Args&&... args)
	{
		auto str = std::format(s, std::forward<Args>(args)...) + "\n";
		OutputDebugStringA(str.c_str());
	}



	//文字列フォーマッティング、書式はprintfと同
	//std::wstring Format(std::wstring format, ...);
	
	//文字列を区切り文字tokで分解してvectorで返す
	std::vector<std::wstring>SplitStr(const std::wstring& s, wchar_t tok);

	//シェーダのエラーの出力(以前のシェーダコンパイラ用、現在は使ってない)
	void OutputShaderError(ID3DBlob* err);


	//失敗したら例外出す
	HRESULT ThrowIfFailed(HRESULT hr, const std::wstring& msg, const wchar_t* filename, UINT line);

	//wstringをANSIエンコードに変換し、std::stringに格納
	std::string wstrToANSI(const std::wstring& w, UINT codepage = CP_ACP);

	//ANSI stringをwstringへ
	std::wstring ANSITowstr(const std::string& s, UINT codepage = CP_ACP);

	//std::stringに格納してあるUTF-8文字列をwstringへ
	std::wstring UTF8Towstr(const std::string& s);

	//wstringをUTF-8エンコードに変換しstd::stringに格納
	std::string wstrToUTF8(const std::wstring& w);

	//exeファイルのパス、末尾に\\は入ってない
	std::wstring ExePath();

	//動作ログ出力
	template<typename... Args>
	void LOG(std::wformat_string<Args...> s, Args&&... args)
	{
#ifdef YRZ_LOGGING
		auto logname = ExePath() + L"\\" + YRZ_LOGFILENAME;
		std::ofstream file;
		if (YRZLogInitialized)
			file = std::ofstream(logname, std::ios::binary | std::ios::app);
		else {
			file = std::ofstream(logname, std::ios::binary);
			YRZLogInitialized = true;
		}
		if (file.is_open()) {
			//時刻の取得
			/*
			auto now_utc = std::chrono::system_clock::now();
			auto now_local = std::chrono::zoned_time{ std::chrono::current_zone(), now_utc };

			std::ostringstream oss;
			oss << now_local << "|";

			std::wstring str = ANSITowstr(oss.str());
			*/
			//ログ出力
			std::wstring str = std::format(s, std::forward<Args>(args)...) + L"\n";
			auto u8s = wstrToUTF8(str);
			file.write(u8s.c_str(), u8s.size());
			file.close();
		}
#else
		return;
#endif
	}


	//DXGI_FORMATの文字列化
	std::wstring DXGIFormatToString(DXGI_FORMAT fmt);


		//文字画像格納用。グレースケールで1ピクセル1バイト
	struct Glyph {
		int leastWidth = 0, leastHeight = 0;//文字をキッチリ納めるために必要なピクセル数
		int width = 0, height = 0;			//glyphが実際に横何px, 縦何pxで納められているか
		int cellWidth = 0, cellHeight = 0;	//文字の周辺の余白込みで必要なピクセル数
		int top = 0, left = 0;				//文字をレンダリングする場合、左上隅をどこに合わせれば良いか
		std::vector<::byte> glyph;					//文字のラスタイメージ
	};

	//GDIによる文字列レンダリング用
	//Direct2D使った方が良いです
	LOGFONTW LogFont(const wchar_t* fontname, int heightInPixels, int weight = FW_NORMAL, bool italic = false, bool underline = false, bool strike = false);
	POINT GetTextGlyphs(const wchar_t* str, const LOGFONTW& logfont, std::vector<Glyph>* glyphs);
	//文字列をメモリ上の配列にレンダリングする。フォーマットはモノクロ8bit。destにはwidth*heightバイトのメモリが確保されている必要がある。
	//clearがfalseの場合、destをクリアせず元の内容とブレンドしてレンダリングする
	//ofsX,ofsYは文字ぞろえの位置を基準としたレンダリング位置の左上座標(X+右、Y+下)
	void RenderTextToMemory(::byte* dest, int width, int height, const wchar_t* str, const LOGFONTW& logfont, HA halign, VA valign, bool clear = true, int ofsX = 0, int ofsY = 0);
	//複数行にわたるテキスト描画 destがnullptrの場合、全体をレンダリングするための幅・高さを返す
	POINT RenderTextToMemoryMultiLine(::byte* dest, int width, int height, const wchar_t* str, const LOGFONTW& logfont, HA halign, VA valign, bool clear = true, int ofsX = 0, int ofsY = 0);


	//ポリゴンメーカー
	namespace PM {

		//ポリゴンメーカー用頂点フォーマット
		struct Vertex {
			DirectX::XMFLOAT3 pos = {};
			DirectX::XMFLOAT3 normal = {};
			DirectX::XMFLOAT2 uv = {};
		};

		//↑の頂点フォーマットをVertexShaderで使う時のInputElementDesc
		std::vector<D3D12_INPUT_ELEMENT_DESC> VertexLayout = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		//PolyMaker用の計算用簡易ユーティリティ。速さが必要な場合はSimpleMathとか使った方が良いよ。
		namespace Math {
			const float PI = acosf(-1);
			inline DirectX::XMFLOAT3 operator-(const DirectX::XMFLOAT3& v) { return DirectX::XMFLOAT3(-v.x, -v.y, -v.z); }
			inline DirectX::XMFLOAT3 operator-(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z); }
			inline DirectX::XMFLOAT3 operator+(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a.x + b.x, a.y + b.y, a.z + b.z); }
			inline DirectX::XMFLOAT3 operator*(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a.x * b.x, a.y * b.y, a.z * b.z); }
			inline DirectX::XMFLOAT3 operator*(const DirectX::XMFLOAT3& a, const float b) { return DirectX::XMFLOAT3(a.x * b, a.y * b, a.z * b); }
			inline DirectX::XMFLOAT3 operator*(const float& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a * b.x, a * b.y, a * b.z); }
			inline DirectX::XMFLOAT3 operator/(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a.x / b.x, a.y / b.y, a.z / b.z); }
			inline DirectX::XMFLOAT3 operator/(const DirectX::XMFLOAT3& a, const float b) { return DirectX::XMFLOAT3(a.x / b, a.y / b, a.z / b); }
			inline DirectX::XMFLOAT3 operator/(const float& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a / b.x, a / b.y, a / b.z); }
			inline DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return DirectX::XMFLOAT3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
			inline float Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return (a.x * b.x + a.y * b.y + a.z * b.z); }
			inline DirectX::XMFLOAT3 RotAxis(DirectX::XMFLOAT3 v, DirectX::XMFLOAT3 a, float t) { float s = sinf(t), c = cosf(t); return v * c + (1 - c) * Dot(a, v) * a + Cross(a, v) * s; }
			inline float Length(const DirectX::XMFLOAT3& a) { return sqrtf(Dot(a, a)); }
			inline DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& a) { float s2 = Dot(a, a); return (s2 == 0) ? DirectX::XMFLOAT3(0, 0, 0) : a / sqrtf(s2); }
			inline DirectX::XMFLOAT3 Reflect(const DirectX::XMFLOAT3& I, const DirectX::XMFLOAT3& N) { return I - 2.0f * Dot(N, I) * N; }

			inline DirectX::XMFLOAT2 operator-(const DirectX::XMFLOAT2& v) { return DirectX::XMFLOAT2(-v.x, -v.y); }
			inline DirectX::XMFLOAT2 operator-(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b) { return DirectX::XMFLOAT2(a.x - b.x, a.y - b.y); }
			inline DirectX::XMFLOAT2 operator+(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b) { return DirectX::XMFLOAT2(a.x + b.x, a.y + b.y); }
			inline DirectX::XMFLOAT2 operator*(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b) { return DirectX::XMFLOAT2(a.x * b.x, a.y * b.y); }
			inline DirectX::XMFLOAT2 operator*(const DirectX::XMFLOAT2& a, const float b) { return DirectX::XMFLOAT2(a.x * b, a.y * b); }
			inline DirectX::XMFLOAT2 operator*(const float& a, const DirectX::XMFLOAT2& b) { return DirectX::XMFLOAT2(a * b.x, a * b.y); }
			inline DirectX::XMFLOAT2 operator/(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b) { return DirectX::XMFLOAT2(a.x / b.x, a.y / b.y); }
			inline DirectX::XMFLOAT2 operator/(const DirectX::XMFLOAT2& a, const float b) { return DirectX::XMFLOAT2(a.x / b, a.y / b); }
			inline DirectX::XMFLOAT2 operator/(const float& a, const DirectX::XMFLOAT2& b) { return DirectX::XMFLOAT2(a / b.x, a / b.y); }
			inline float Dot(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b) { return (a.x * b.x + a.y * b.y); }
			inline DirectX::XMFLOAT2 Rot(const DirectX::XMFLOAT2& v, float t) { float c = cosf(t), s = sinf(t); return DirectX::XMFLOAT2(c * v.x - s * v.y, s * v.x + c * v.y); }
			inline float Length(const DirectX::XMFLOAT2& a) { return sqrtf(Dot(a, a)); }
			inline DirectX::XMFLOAT2 Normalize(const DirectX::XMFLOAT2& a) { float s2 = Dot(a, a); return (s2 == 0) ? DirectX::XMFLOAT2(0, 0) : a / sqrtf(s2); }
			inline DirectX::XMFLOAT3 xyz(const DirectX::XMFLOAT4& v) { return DirectX::XMFLOAT3(v.x, v.y, v.z); }

			inline DirectX::XMVECTOR XMV(const DirectX::XMFLOAT3& v, float w = 1) { return DirectX::XMVectorSet(v.x, v.y, v.z, w); }
			inline DirectX::XMVECTOR XMV(const DirectX::XMFLOAT2& v, float z = 0, float w = 1) { return DirectX::XMVectorSet(v.x, v.y, z, w); }
			inline DirectX::XMVECTOR XMV(float x=0, float y=0, float z = 0, float w = 1) { return DirectX::XMVectorSet(x, y, z, w); }

			inline float clamp(float x, float mi, float ma) { return max(min(x, ma), mi); }
			inline float saturate(float x) { return clamp(x, 0, 1); }
			inline float maxf(float x, float y) { return max(x, y); }
			inline DirectX::XMFLOAT2 vec2(float a) { return DirectX::XMFLOAT2(a, a); }
			inline DirectX::XMFLOAT3 vec3(float a) { return DirectX::XMFLOAT3(a, a, a); }
			inline DirectX::XMFLOAT4 vec4(float a) { return DirectX::XMFLOAT4(a, a, a, a); }

			#define YRZ_PM_MATH_VECTORIZE1(dub, func)\
				inline DirectX::XMFLOAT2 dub(const DirectX::XMFLOAT2& a){ return DirectX::XMFLOAT2( func(a.x), func(a.y));}\
				inline DirectX::XMFLOAT3 dub(const DirectX::XMFLOAT3& a) { return DirectX::XMFLOAT3( func(a.x), func(a.y), func(a.z)); }\
				inline DirectX::XMFLOAT4 dub(const DirectX::XMFLOAT4& a) { return DirectX::XMFLOAT4( func(a.x), func(a.y), func(a.z), func(a.w)); }

			#define YRZ_PM_MATH_VECTORIZE2(dub, func)\
				inline DirectX::XMFLOAT2 dub(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b){ return DirectX::XMFLOAT2( func(a.x,b.x), func(a.y,b.y)); }\
				inline DirectX::XMFLOAT3 dub(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b){ return DirectX::XMFLOAT3( func(a.x, b.x), func(a.y, b.y), func(a.z,b.z)); }\
				inline DirectX::XMFLOAT4 dub(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b){ return DirectX::XMFLOAT4( func(a.x, b.x), func(a.y, b.y), func(a.z, b.z), func(a.w,b.w)); }

			#define YRZ_PM_MATH_VECTORIZE3(dub, func)\
				inline DirectX::XMFLOAT2 dub(const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT2& c){ return DirectX::XMFLOAT2( func(a.x,b.x,c.x), func(a.y,b.y,c.y) );}\
				inline DirectX::XMFLOAT3 dub(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c){ return DirectX::XMFLOAT3( func(a.x,b.x,c.x), func(a.y,b.y,c.y), func(a.z,b.z,c.z) );}\
				inline DirectX::XMFLOAT4 dub(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b, const DirectX::XMFLOAT4& c){ return DirectX::XMFLOAT4( func(a.x,b.x,c.x), func(a.y,b.y,c.y), func(a.z,b.z,c.z), func(a.w,b.w,c.w) );}


			YRZ_PM_MATH_VECTORIZE3(clampv, clamp)
			YRZ_PM_MATH_VECTORIZE1(saturatev, saturate)
			YRZ_PM_MATH_VECTORIZE2(maxv, maxf)
			YRZ_PM_MATH_VECTORIZE2(powv, powf)
			YRZ_PM_MATH_VECTORIZE1(atanv, atanf)
			YRZ_PM_MATH_VECTORIZE2(atan2v, atan2f)
		}

	
		/*** 頂点・法線の操作 ***/
		//移動
		void Translate(std::vector<Vertex>& vs, const DirectX::XMFLOAT3& V);
		//回転(Oを中心としてaxis周りにtラジアン回転)
		void Rotate(std::vector<Vertex>& vs, const DirectX::XMFLOAT3& axis, float t, const DirectX::XMFLOAT3& O = DirectX::XMFLOAT3(0,0,0));
		//スケール(Oを中心として各軸scale倍。determinantがマイナスになる場合、面の右回り・左回りが反転するので注意)
		void Scale(std::vector<Vertex>& vs, const DirectX::XMFLOAT3& scale, const DirectX::XMFLOAT3& O = DirectX::XMFLOAT3(0, 0, 0));
		//一次変換
		void Transform(std::vector<Vertex>& vs, const DirectX::XMMATRIX& mtrx);
		//反転(planeに対する鏡像にする。面の右回り・左回りは保持される)
		void Flip(std::vector<Vertex>& vs, std::vector<UINT>&is, const DirectX::XMFLOAT4& plane);

		/*** UV の操作 ***/
		void TranslateUV(std::vector<Vertex>& vs, const DirectX::XMFLOAT2& V);
		//回転(Oを中心としてtラジアン回転)
		void RotateUV(std::vector<Vertex>& vs, float t, const DirectX::XMFLOAT2& O = DirectX::XMFLOAT2(0.5, 0.5));
		//スケール(Oを中心として各軸scale倍)
		void ScaleUV(std::vector<Vertex>& vs, const DirectX::XMFLOAT2& scale, const DirectX::XMFLOAT2& O = DirectX::XMFLOAT2(0.5, 0.5));
		//一次変換
		void TransformUV(std::vector<Vertex>& vs, const DirectX::XMMATRIX& mtrx);

		/*** メッシュ作成 ***/
		//alternateをtrueに設定すると、四角形を三角形に分割する方向が互い違いになる
		void ParametricSurface(std::vector<Vertex>& vs, std::vector<UINT>& is, Vertex(uv2vertex(float u, float v)), int ucount, int vcount, bool smooth = true, bool alternate = false, bool twosided = false);
		//-1～+1の範囲のXY平面
		void Plane(std::vector<Vertex>& vs, std::vector<UINT>& is, int xcount, int ycount, bool smooth = true, bool alternate = false, bool twosided = false);
		void Plane(std::vector<Vertex>& vs, std::vector<UINT>& is, const DirectX::XMFLOAT3& normal, int xcount, int ycount, bool smooth = true, bool alternate = false, bool twosided = false);

		//正多角形
		void RegNGon(std::vector<Vertex>& vs, std::vector<UINT>& is, int n);
		//交互に半径の変わる正多角形(星型)
		void StarNGon(std::vector<Vertex>& vs, std::vector<UINT>& is, int n, float rOdd);
		//vsとisに指定されたFanで描かれた図形をVだけ掃引, smooth:側面の法線をスムーズにする? 元図形の法線と反対側に掃引すると外向きの面が張られた角柱になる
		void SweepNGon(std::vector<Vertex>& vs, std::vector<UINT>& is, DirectX::XMFLOAT3 V, bool smooth = true);
		//角柱(RegNGon+SweepNGon) 半径1高さ1
		void Prism(std::vector<Vertex>& vs, std::vector<UINT>& is, int n, bool smooth = true);
		//分割数nU * nVでOを中心とする半径rの球
		void Sphere(std::vector<Vertex>& vs, std::vector<UINT>& is, int ucount, int vcount, bool smooth = true, bool alternate = false);
		//コーン(円周方向の分割数がucount, 高さ方向の分割数がvcount, 高さ1、半径1)
		void Cone(std::vector<Vertex>& vs, std::vector<UINT>& is, int ucount, int vcount, bool smooth = true, bool alternate = false);
		//4面体(辺の長さが2)
		void Tetrahedron(std::vector<Vertex>& vs, std::vector<UINT>& is);
		//6面体(±1の範囲での立方体)
		void Cube(std::vector<Vertex>& vs, std::vector<UINT>& is);
		//8面体
		void Octahedron(std::vector<Vertex>& vs, std::vector<UINT>& is);
		//20面体
		void Icosahedron(std::vector<Vertex>& vs, std::vector<UINT>& is);
		//12面体
		void Dodecahedron(std::vector<Vertex>& vs, std::vector<UINT>& is);

		/** メッシュ全体操作 複製・結合など ***/
		//destvs,destisにsrcvs,srcisをくっつける
		void Join(std::vector<Vertex>& destvs, std::vector<UINT>& destis, const std::vector<Vertex>& srcvs, const std::vector<UINT>& srcis);
		//鏡像 平面plane : plane・X = 0となるXの点の集合で反転コピー
		void Fold(std::vector<Vertex>& vs, std::vector<UINT>& is, const DirectX::XMFLOAT4& plane);
	}
}


module : private;

//DirectXTexが必要なのでよろしく

//グローバル変数
bool YRZLogInitialized = false;



//このモジュール内からしか使うアテの無いユーティリティ関数など
namespace {
	using Microsoft::WRL::ComPtr;

	//IDxCCompiler3用のインクルードハンドラ
	//Simon Coenen氏 "Using the DirectXShaderCompiler C++ API"
	//https://simoncoenen.com/blog/programming/graphics/DxcCompiling を参考にさせていただきました
	class CustomIncludeHandler : public IDxcIncludeHandler
	{
		ComPtr<IDxcUtils> pDxCUtils = nullptr;
	public:
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override { return E_NOINTERFACE; }
		ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
		ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

		std::unordered_set<std::wstring> IncludedFiles;
		std::wstring InitialPath = L"";

		HRESULT STDMETHODCALLTYPE LoadSource(_In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
		{
			if (!pDxCUtils)
				YRZ::ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pDxCUtils.GetAddressOf())), L"Include Handler Failed", __FILEW__, __LINE__);

			ComPtr<IDxcBlobEncoding> pEncoding;

			std::wstring path = InitialPath + pFilename;
			if (IncludedFiles.find(path) != IncludedFiles.end())
			{
				// Return empty string blob if this file has been included before
				static const char nullStr[] = " ";
				pDxCUtils->CreateBlobFromPinned(nullStr, ARRAYSIZE(nullStr), DXC_CP_ACP, pEncoding.GetAddressOf());
				*ppIncludeSource = pEncoding.Detach();
				return S_OK;
			}

			HRESULT hr = pDxCUtils->LoadFile(path.c_str(), nullptr, pEncoding.GetAddressOf());
			if (SUCCEEDED(hr))
			{
				IncludedFiles.insert(path);
				*ppIncludeSource = pEncoding.Detach();
			}
			return hr;
		}

	};

	//スタックトレース ↓を参考にさせていただきました
	//https://an-embedded-engineer.hateblo.jp/entry/2020/08/24/160436
	//https://an-embedded-engineer.hateblo.jp/entry/2020/08/24/212511

	//スタックトレース用
	struct StackTrace
	{
		/* トレース数 */
		uint32_t trace_size;
		/* トレースアドレスリスト */
		std::vector<void*> traces;
		/* トレースシンボルリスト */
		std::vector<std::string> symbols;
	};


	/* スタックトレース情報取得 */
	StackTrace DoStackTrace()
	{
		/* 最大トレースサイズ */
		constexpr size_t MaxSize = 256;
		/* トレースリスト */
		void* traces[MaxSize] = {};
		/* 現在のプロセスを取得 */
		HANDLE process = GetCurrentProcess();
		/* シンボルハンドラの初期化 */
		SymInitialize(process, NULL, TRUE);
		/* スタックトレースの取得 */
		uint16_t trace_size = CaptureStackBackTrace(0, MaxSize, traces, NULL);
		/* シンボル名最大サイズをセット */
		constexpr size_t MaxNameSize = 255;
		/* シンボル情報サイズを算出 */
		constexpr size_t SymbolInfoSize = sizeof(SYMBOL_INFO) + ((MaxNameSize + 1) * sizeof(char));
		/* シンボル情報のメモリ確保 */
		SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(SymbolInfoSize, 1);
		/* スタックトレース情報生成 */
		StackTrace stack_trace;
		/* シンボル情報メモリ確保成功 */
		if (symbol != nullptr) {
			/* シンボル名最大サイズをセット */
			symbol->MaxNameLen = MaxNameSize;
			/* シンボル情報サイズをセット */
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			/* トレースサイズをセット */
			stack_trace.trace_size = (uint32_t)trace_size;
			/* トレースリストのメモリ確保 */
			stack_trace.traces.reserve((size_t)trace_size);
			/* シンボルリストのメモリ確保 */
			stack_trace.symbols.reserve((size_t)trace_size);
			/* トレースサイズ分ループ */
			for (uint16_t i = 0; i < trace_size; i++) {
				/* トレースアドレスからシンボル情報を取得 */
				SymFromAddr(process, (DWORD64)(traces[i]), 0, symbol);
				/* トレースアドレスをトレースリストに追加 */
				stack_trace.traces.push_back(traces[i]);
				/* シンボル名をシンボルリストに追加 */
				stack_trace.symbols.push_back(std::string(symbol->Name));
			}
			/* シンボル情報のメモリ解放 */
			free(symbol);
		} else {
			/* Nothing to do */
		}
		return stack_trace;
	}


	//hlslからのエラーメッセージに{}入ってると怒られるので(現在未使用)
	std::wstring EscapeBrace(std::wstring s)
	{
		std::wstring r;
		r.reserve(s.size());

		for (auto w : s) {
			if (w == L'{')
				r += L"{{";
			else if (w == L'}')
				r += L"}}";
			else
				r += w;
		}
		return r;
	}

	
	//mipmap生成用シェーダ
	const char MipmapGenShader[] = R"(
		struct VSO { float4 pos:SV_POSITION; float2 uv:TEXCOORD; };
		Texture2D<float4> SrcTex : register(t0);
		cbuffer CB : register(b0) { int miplevel; }
		sampler samp : register(s0);
		VSO VS( float4 pos : POSITION, float2 uv:TEXCOORD ) { VSO o; o.pos = pos; o.uv = uv; return o; }
		float3 ToLinear(float3 gamma) {return (gamma <= 0.04045) ? gamma / 12.92 : pow((gamma + 0.055) / 1.055, 2.4);}
		float4 PSLinear(VSO vso) : SV_TARGET { float4 t = SrcTex.SampleLevel(samp, vso.uv, 0); return float4(ToLinear(t.rgb),t.a); }
		float4 PSCopy(VSO vso) : SV_TARGET { return SrcTex.SampleLevel(samp, vso.uv, 0); }
		float4 PSShrink(VSO vso) : SV_TARGET {
			uint W,H,mipCount;
			SrcTex.GetDimensions(miplevel, W,H,mipCount);
			float2 d = 0.5/float2(W,H);
			return SrcTex.SampleLevel(samp, vso.uv+d, miplevel);
		}
	)";

	//ガンマ→リニア変換
	const char ToLinearShader[] = R"(
		struct VSO { float4 pos:SV_POSITION; float2 uv:TEXCOORD; };
		Texture2D<float4> SrcTex : register(t0);
		cbuffer CB : register(b0) { int miplevel; }
		sampler samp : register(s0);
		VSO VS( float4 pos : POSITION, float2 uv:TEXCOORD ) { VSO o; o.pos = pos; o.uv = uv; return o; }
		float3 ToLinear(float3 gamma) {return (gamma <= 0.04045) ? gamma / 12.92 : pow((gamma + 0.055) / 1.055, 2.4);}
		float4 PSLinear(VSO vso) : SV_TARGET { float4 t = SrcTex.SampleLevel(samp, vso.uv, miplevel); return float4(ToLinear(t.rgb),t.a); }
	)";

}


namespace YRZ {

	/***********************************************************************************************************/
	//	実装
	/***********************************************************************************************************/

	//アラインメント。sizeをalignの整数倍のうち最小の数に合わせる。但しalignは2の整数冪とする
	inline UINT Align(size_t size, UINT align) {
		return UINT(size + align - 1) & ~(align - 1);
	}

	/*
	//デバッグ用ログ出力
	void LOG(std::wstring format, ...) {
#ifdef _DEBUG
		wchar_t* strbuf = new wchar_t[YRZ_STRBUFSIZE];
		va_list args;
		va_start(args, format);
		vswprintf_s(strbuf, YRZ_STRBUFSIZE, format.c_str(), args);
		va_end(args);
		OutputDebugStringW(strbuf);
		OutputDebugStringW(L"\n");
		delete[] strbuf;
#endif
	}

	void LOGA(std::string format, ...) {
#ifdef _DEBUG
		char* strbuf = new char[YRZ_STRBUFSIZE];
		va_list args;
		va_start(args, format);
		vsprintf_s(strbuf, YRZ_STRBUFSIZE, format.c_str(), args);
		va_end(args);
		OutputDebugStringA(strbuf);
		OutputDebugStringA("\n");
		delete[] strbuf;
#endif
	}

	void LOGU(std::string format, ...)
	{
#ifdef _DEBUG
		char* strbuf = new char[YRZ_STRBUFSIZE];
		va_list args;
		va_start(args, format);
		vsprintf_s(strbuf, YRZ_STRBUFSIZE, format.c_str(), args);
		va_end(args);
		auto wstr = UTF8Towstr(strbuf);
		OutputDebugStringW(wstr.c_str());
		OutputDebugStringW(L"\n");
		delete[] strbuf;
#endif
	}


	std::wstring Format(std::wstring format, ...) {
		wchar_t* strbuf = new wchar_t[YRZ_STRBUFSIZE];
		va_list args;
		va_start(args, format);
		vswprintf_s(strbuf, YRZ_STRBUFSIZE, format.c_str(), args);
		va_end(args);
		std::wstring ret = strbuf;
		delete[] strbuf;
		return ret;
	}
	*/

	//sをtokで分解してvectorで返す
	std::vector<std::wstring>SplitStr(const std::wstring& s, wchar_t tok)
	{
		auto ret = std::vector<std::wstring>();

		size_t top = 0, len = 1;
		for (size_t i = 0; i < s.size(); i++) {
			if (s[i] == tok) {
				ret.push_back(s.substr(top, len - 1));
				top = i + 1;
				len = 1;
			} else {
				len++;
			}
		}

		if (top < s.size())
			ret.push_back(s.substr(top, len));

		return ret;
	}

	//エラーブロブの中のシェーダエラー出力
	void OutputShaderError(ID3DBlob* err)
	{
		std::string errstr;
		errstr.resize(err->GetBufferSize());
		copy_n((char*)err->GetBufferPointer(), err->GetBufferSize(), errstr.begin());
		OutputDebugStringA(errstr.c_str());
	}


	//失敗したら例外
	/*
	HRESULT ThrowIfFailed(HRESULT hr, std::wstring format, ...) {
		if (FAILED(hr)) {
			wchar_t* strbuf = new wchar_t[YRZ_STRBUFSIZE];
			va_list args;
			va_start(args, format);
			vswprintf_s(strbuf, YRZ_STRBUFSIZE, format.c_str(), args);
			va_end(args);
			std::wstring err = strbuf;
			delete[] strbuf;
			err += std::format(L"\nHRESULT={} : {}", hr, strTowstr(std::system_category().message(hr)).c_str() );
			OutputDebugStringW(err.c_str());
			throw std::exception(wstrTostr(err).c_str());
		}
		return hr;
	}
	*/
	HRESULT ThrowIfFailed(HRESULT hr, const std::wstring& msg, const wchar_t* filename, UINT line)
	{
		if (FAILED(hr)) {
			auto fname = std::filesystem::path(filename).filename().wstring();
			auto err = msg + L"\n" + std::format(L"{} {} : \nHRESULT={:x} : {}", fname, line, DWORD(hr), ANSITowstr(std::system_category().message(hr)));
			
			DEB(L"{}", err);
			LOG(L"{}", L"★★★ YRZException ★★★");
			LOG(L"{}", err);

			LOG(L"▼▼ stack trace begin ▼▼");
			auto st = DoStackTrace();
			for (int i = 0; auto & s : st.symbols) {
				auto msg = std::format("{}({}) -> ", s, st.traces[i]);
				LOG(L"{}",ANSITowstr(msg));
				i++;
			}
			LOG(L"▲▲ stack trace end ▲▲");

			throw YRZ::YRZException(wstrToANSI(err));
		} else {
			//auto fname = std::filesystem::path(filename).filename().wstring();
			//LOG(L"TiF OK {} {}", fname, line);
		}
		return hr;
	}



	//注：CP_ACPとCP_OEMCPの違い
	// 
	// bing AIによれば
	// CP_ACPはANSIコードページ。GUIアプリやWindowsで用いられる
	// CP_OEMCPはOEMコードページ。DOSアプリで用いられていた。罫線とかが入ってる事が多いとのこと
	//
	// 日本語の場合はたまたまCP_ACP = CP_OEMCP = 932になっているので両者で違いはないが
	// 英語はCP_ACP = 1252, CP_OEMCP = 437
	// ロシア語は CP_ACP = 1251, CP_OEMCP = 866
	// アラビア語は CP_ACP = 1256, CP_OEMCP = 720
	// となっており、基本的には違う(これもソースはbing AI)
	//
	// Windowsにおける"ANSI"とは何か？については↓の記事によると
	// https://enpedia.rxy.jp/wiki/ANSI_(%E6%96%87%E5%AD%97%E3%82%B3%E3%83%BC%E3%83%89)
	// 古いWindowsアプリで各国まちまちに使っていた文字コードの事を総称してANSIと、Microsoftは呼称しているらしい
	// 純粋にWindows環境の事だけを考えるのであればWindowsの歴史とともに存在するCP_ACPでよいので
	// MS-DOS時代の各国ローカルのアスキーアートなどを変換したいでもない限りはCP_OEMCPの出番は無いと思われる
	//
	// このソースコード中でも、Microsoftに倣ってANSIと言えば「各国版のWindowsごとの古い文字コード」を指す事にする


	//wstring(UTF-16 LE → ANSI string)
	std::string wstrToANSI(const std::wstring& w, UINT codepage) {
		int iBufferSize = WideCharToMultiByte(codepage, 0, w.c_str(), -1, (char*)NULL, 0, NULL, NULL);
		CHAR* cpMultiByte = new CHAR[iBufferSize];
		WideCharToMultiByte(codepage, 0, w.c_str(), -1, cpMultiByte, iBufferSize, NULL, NULL);
		std::string oRet(cpMultiByte, cpMultiByte + iBufferSize - 1);
		delete[] cpMultiByte;
		return(oRet);
	}

	//ANSI stringから wstring(UTF-16LE)へ
	std::wstring ANSITowstr(const std::string& s, UINT codepage) {
		int iBufferSize = MultiByteToWideChar(codepage, 0, s.c_str(), -1, (wchar_t*)NULL, 0);
		wchar_t* cpUCS2 = new wchar_t[iBufferSize];
		MultiByteToWideChar(codepage, 0, s.c_str(), -1, cpUCS2, iBufferSize);
		std::wstring oRet(cpUCS2, cpUCS2 + iBufferSize - 1);
		delete[] cpUCS2;
		return(oRet);
	}

	//wstring(UTF-16LE)からutf-8でエンコードされたstringへ
	std::string wstrToUTF8(const std::wstring& w) {
		return wstrToANSI(w, CP_UTF8);
	}

	//utf-8でエンコードされた文字列の入ったstringをwstring(UTF-16LE)へ
	std::wstring UTF8Towstr(const std::string& s) {
		return ANSITowstr(s, CP_UTF8);
	}

	//起動ファイルのパス名を取得。末尾のバックスラッシュは無し
	std::wstring ExePath()
	{
		wchar_t buffer[MAX_PATH];
		GetModuleFileName(NULL, buffer, MAX_PATH);
		std::filesystem::path path(buffer);
		return path.parent_path().wstring();
	}

	std::wstring DXGIFormatToString(DXGI_FORMAT fmt)
	{
		switch (fmt) {
			case DXGI_FORMAT_UNKNOWN: return L"DXGI_FORMAT_UNKNOWN";
			case DXGI_FORMAT_R32G32B32A32_TYPELESS: return L"DXGI_FORMAT_R32G32B32A32_TYPELESS";
			case DXGI_FORMAT_R32G32B32A32_FLOAT: return L"DXGI_FORMAT_R32G32B32A32_FLOAT";
			case DXGI_FORMAT_R32G32B32A32_UINT: return L"DXGI_FORMAT_R32G32B32A32_UINT";
			case DXGI_FORMAT_R32G32B32A32_SINT: return L"DXGI_FORMAT_R32G32B32A32_SINT";
			case DXGI_FORMAT_R32G32B32_TYPELESS: return L"DXGI_FORMAT_R32G32B32_TYPELESS";
			case DXGI_FORMAT_R32G32B32_FLOAT: return L"DXGI_FORMAT_R32G32B32_FLOAT";
			case DXGI_FORMAT_R32G32B32_UINT: return L"DXGI_FORMAT_R32G32B32_UINT";
			case DXGI_FORMAT_R32G32B32_SINT: return L"DXGI_FORMAT_R32G32B32_SINT";
			case DXGI_FORMAT_R16G16B16A16_TYPELESS: return L"DXGI_FORMAT_R16G16B16A16_TYPELESS";
			case DXGI_FORMAT_R16G16B16A16_FLOAT: return L"DXGI_FORMAT_R16G16B16A16_FLOAT";
			case DXGI_FORMAT_R16G16B16A16_UNORM: return L"DXGI_FORMAT_R16G16B16A16_UNORM";
			case DXGI_FORMAT_R16G16B16A16_UINT: return L"DXGI_FORMAT_R16G16B16A16_UINT";
			case DXGI_FORMAT_R16G16B16A16_SNORM: return L"DXGI_FORMAT_R16G16B16A16_SNORM";
			case DXGI_FORMAT_R16G16B16A16_SINT: return L"DXGI_FORMAT_R16G16B16A16_SINT";
			case DXGI_FORMAT_R32G32_TYPELESS: return L"DXGI_FORMAT_R32G32_TYPELESS";
			case DXGI_FORMAT_R32G32_FLOAT: return L"DXGI_FORMAT_R32G32_FLOAT";
			case DXGI_FORMAT_R32G32_UINT: return L"DXGI_FORMAT_R32G32_UINT";
			case DXGI_FORMAT_R32G32_SINT: return L"DXGI_FORMAT_R32G32_SINT";
			case DXGI_FORMAT_R32G8X24_TYPELESS: return L"DXGI_FORMAT_R32G8X24_TYPELESS";
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return L"DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return L"DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
			case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return L"DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";
			case DXGI_FORMAT_R10G10B10A2_TYPELESS: return L"DXGI_FORMAT_R10G10B10A2_TYPELESS";
			case DXGI_FORMAT_R10G10B10A2_UNORM: return L"DXGI_FORMAT_R10G10B10A2_UNORM";
			case DXGI_FORMAT_R10G10B10A2_UINT: return L"DXGI_FORMAT_R10G10B10A2_UINT";
			case DXGI_FORMAT_R11G11B10_FLOAT: return L"DXGI_FORMAT_R11G11B10_FLOAT";
			case DXGI_FORMAT_R8G8B8A8_TYPELESS: return L"DXGI_FORMAT_R8G8B8A8_TYPELESS";
			case DXGI_FORMAT_R8G8B8A8_UNORM: return L"DXGI_FORMAT_R8G8B8A8_UNORM";
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return L"DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
			case DXGI_FORMAT_R8G8B8A8_UINT: return L"DXGI_FORMAT_R8G8B8A8_UINT";
			case DXGI_FORMAT_R8G8B8A8_SNORM: return L"DXGI_FORMAT_R8G8B8A8_SNORM";
			case DXGI_FORMAT_R8G8B8A8_SINT: return L"DXGI_FORMAT_R8G8B8A8_SINT";
			case DXGI_FORMAT_R16G16_TYPELESS: return L"DXGI_FORMAT_R16G16_TYPELESS";
			case DXGI_FORMAT_R16G16_FLOAT: return L"DXGI_FORMAT_R16G16_FLOAT";
			case DXGI_FORMAT_R16G16_UNORM: return L"DXGI_FORMAT_R16G16_UNORM";
			case DXGI_FORMAT_R16G16_UINT: return L"DXGI_FORMAT_R16G16_UINT";
			case DXGI_FORMAT_R16G16_SNORM: return L"DXGI_FORMAT_R16G16_SNORM";
			case DXGI_FORMAT_R16G16_SINT: return L"DXGI_FORMAT_R16G16_SINT";
			case DXGI_FORMAT_R32_TYPELESS: return L"DXGI_FORMAT_R32_TYPELESS";
			case DXGI_FORMAT_D32_FLOAT: return L"DXGI_FORMAT_D32_FLOAT";
			case DXGI_FORMAT_R32_FLOAT: return L"DXGI_FORMAT_R32_FLOAT";
			case DXGI_FORMAT_R32_UINT: return L"DXGI_FORMAT_R32_UINT";
			case DXGI_FORMAT_R32_SINT: return L"DXGI_FORMAT_R32_SINT";
			case DXGI_FORMAT_R24G8_TYPELESS: return L"DXGI_FORMAT_R24G8_TYPELESS";
			case DXGI_FORMAT_D24_UNORM_S8_UINT: return L"DXGI_FORMAT_D24_UNORM_S8_UINT";
			case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return L"DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
			case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return L"DXGI_FORMAT_X24_TYPELESS_G8_UINT";
			case DXGI_FORMAT_R8G8_TYPELESS: return L"DXGI_FORMAT_R8G8_TYPELESS";
			case DXGI_FORMAT_R8G8_UNORM: return L"DXGI_FORMAT_R8G8_UNORM";
			case DXGI_FORMAT_R8G8_UINT: return L"DXGI_FORMAT_R8G8_UINT";
			case DXGI_FORMAT_R8G8_SNORM: return L"DXGI_FORMAT_R8G8_SNORM";
			case DXGI_FORMAT_R8G8_SINT: return L"DXGI_FORMAT_R8G8_SINT";
			case DXGI_FORMAT_R16_TYPELESS: return L"DXGI_FORMAT_R16_TYPELESS";
			case DXGI_FORMAT_R16_FLOAT: return L"DXGI_FORMAT_R16_FLOAT";
			case DXGI_FORMAT_D16_UNORM: return L"DXGI_FORMAT_D16_UNORM";
			case DXGI_FORMAT_R16_UNORM: return L"DXGI_FORMAT_R16_UNORM";
			case DXGI_FORMAT_R16_UINT: return L"DXGI_FORMAT_R16_UINT";
			case DXGI_FORMAT_R16_SNORM: return L"DXGI_FORMAT_R16_SNORM";
			case DXGI_FORMAT_R16_SINT: return L"DXGI_FORMAT_R16_SINT";
			case DXGI_FORMAT_R8_TYPELESS: return L"DXGI_FORMAT_R8_TYPELESS";
			case DXGI_FORMAT_R8_UNORM: return L"DXGI_FORMAT_R8_UNORM";
			case DXGI_FORMAT_R8_UINT: return L"DXGI_FORMAT_R8_UINT";
			case DXGI_FORMAT_R8_SNORM: return L"DXGI_FORMAT_R8_SNORM";
			case DXGI_FORMAT_R8_SINT: return L"DXGI_FORMAT_R8_SINT";
			case DXGI_FORMAT_A8_UNORM: return L"DXGI_FORMAT_A8_UNORM";
			case DXGI_FORMAT_R1_UNORM: return L"DXGI_FORMAT_R1_UNORM";
			case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return L"DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
			case DXGI_FORMAT_R8G8_B8G8_UNORM: return L"DXGI_FORMAT_R8G8_B8G8_UNORM";
			case DXGI_FORMAT_G8R8_G8B8_UNORM: return L"DXGI_FORMAT_G8R8_G8B8_UNORM";
			case DXGI_FORMAT_BC1_TYPELESS: return L"DXGI_FORMAT_BC1_TYPELESS";
			case DXGI_FORMAT_BC1_UNORM: return L"DXGI_FORMAT_BC1_UNORM";
			case DXGI_FORMAT_BC1_UNORM_SRGB: return L"DXGI_FORMAT_BC1_UNORM_SRGB";
			case DXGI_FORMAT_BC2_TYPELESS: return L"DXGI_FORMAT_BC2_TYPELESS";
			case DXGI_FORMAT_BC2_UNORM: return L"DXGI_FORMAT_BC2_UNORM";
			case DXGI_FORMAT_BC2_UNORM_SRGB: return L"DXGI_FORMAT_BC2_UNORM_SRGB";
			case DXGI_FORMAT_BC3_TYPELESS: return L"DXGI_FORMAT_BC3_TYPELESS";
			case DXGI_FORMAT_BC3_UNORM: return L"DXGI_FORMAT_BC3_UNORM";
			case DXGI_FORMAT_BC3_UNORM_SRGB: return L"DXGI_FORMAT_BC3_UNORM_SRGB";
			case DXGI_FORMAT_BC4_TYPELESS: return L"DXGI_FORMAT_BC4_TYPELESS";
			case DXGI_FORMAT_BC4_UNORM: return L"DXGI_FORMAT_BC4_UNORM";
			case DXGI_FORMAT_BC4_SNORM: return L"DXGI_FORMAT_BC4_SNORM";
			case DXGI_FORMAT_BC5_TYPELESS: return L"DXGI_FORMAT_BC5_TYPELESS";
			case DXGI_FORMAT_BC5_UNORM: return L"DXGI_FORMAT_BC5_UNORM";
			case DXGI_FORMAT_BC5_SNORM: return L"DXGI_FORMAT_BC5_SNORM";
			case DXGI_FORMAT_B5G6R5_UNORM: return L"DXGI_FORMAT_B5G6R5_UNORM";
			case DXGI_FORMAT_B5G5R5A1_UNORM: return L"DXGI_FORMAT_B5G5R5A1_UNORM";
			case DXGI_FORMAT_B8G8R8A8_UNORM: return L"DXGI_FORMAT_B8G8R8A8_UNORM";
			case DXGI_FORMAT_B8G8R8X8_UNORM: return L"DXGI_FORMAT_B8G8R8X8_UNORM";
			case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return L"DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
			case DXGI_FORMAT_B8G8R8A8_TYPELESS: return L"DXGI_FORMAT_B8G8R8A8_TYPELESS";
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return L"DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
			case DXGI_FORMAT_B8G8R8X8_TYPELESS: return L"DXGI_FORMAT_B8G8R8X8_TYPELESS";
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return L"DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
			case DXGI_FORMAT_BC6H_TYPELESS: return L"DXGI_FORMAT_BC6H_TYPELESS";
			case DXGI_FORMAT_BC6H_UF16: return L"DXGI_FORMAT_BC6H_UF16";
			case DXGI_FORMAT_BC6H_SF16: return L"DXGI_FORMAT_BC6H_SF16";
			case DXGI_FORMAT_BC7_TYPELESS: return L"DXGI_FORMAT_BC7_TYPELESS";
			case DXGI_FORMAT_BC7_UNORM: return L"DXGI_FORMAT_BC7_UNORM";
			case DXGI_FORMAT_BC7_UNORM_SRGB: return L"DXGI_FORMAT_BC7_UNORM_SRGB";
			case DXGI_FORMAT_AYUV: return L"DXGI_FORMAT_AYUV";
			case DXGI_FORMAT_Y410: return L"DXGI_FORMAT_Y410";
			case DXGI_FORMAT_Y416: return L"DXGI_FORMAT_Y416";
			case DXGI_FORMAT_NV12: return L"DXGI_FORMAT_NV12";
			case DXGI_FORMAT_P010: return L"DXGI_FORMAT_P010";
			case DXGI_FORMAT_P016: return L"DXGI_FORMAT_P016";
			case DXGI_FORMAT_420_OPAQUE: return L"DXGI_FORMAT_420_OPAQUE";
			case DXGI_FORMAT_YUY2: return L"DXGI_FORMAT_YUY2";
			case DXGI_FORMAT_Y210: return L"DXGI_FORMAT_Y210";
			case DXGI_FORMAT_Y216: return L"DXGI_FORMAT_Y216";
			case DXGI_FORMAT_NV11: return L"DXGI_FORMAT_NV11";
			case DXGI_FORMAT_AI44: return L"DXGI_FORMAT_AI44";
			case DXGI_FORMAT_IA44: return L"DXGI_FORMAT_IA44";
			case DXGI_FORMAT_P8: return L"DXGI_FORMAT_P8";
			case DXGI_FORMAT_A8P8: return L"DXGI_FORMAT_A8P8";
			case DXGI_FORMAT_B4G4R4A4_UNORM: return L"DXGI_FORMAT_B4G4R4A4_UNORM";
			case DXGI_FORMAT_P208: return L"DXGI_FORMAT_P208";
			case DXGI_FORMAT_V208: return L"DXGI_FORMAT_V208";
			case DXGI_FORMAT_V408: return L"DXGI_FORMAT_V408";
			default: return L"Unknown Format";
		}
	}


	bool IsDirectXRaytracingSupported(IDXGIAdapter1* adapter)
	{
		ComPtr<ID3D12Device> testDevice;
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};

		return SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice)))
			&& SUCCEEDED(testDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData)))
			&& featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	}



	void D3D12DREDLog(ID3D12Device* pDevice)
	{
		// D3D12_AUTO_BREADCRUMB_OP に対応するイベント名を用意しておきます
		// バージョンによってイベントが増えるので注意する
		static const TCHAR* OpNames[] =
		{
			_T("SETMARKER"),                                           // 0
			_T("BEGINEVENT"),                                          // 1
			_T("ENDEVENT"),                                            // 2
			_T("DRAWINSTANCED"),                                       // 3
			_T("DRAWINDEXEDINSTANCED"),                                // 4
			_T("EXECUTEINDIRECT"),                                     // 5
			_T("DISPATCH"),                                            // 6
			_T("COPYBUFFERREGION"),                                    // 7
			_T("COPYTEXTUREREGION"),                                   // 8
			_T("COPYRESOURCE"),                                        // 9
			_T("COPYTILES"),                                           // 10
			_T("RESOLVESUBRESOURCE"),                                  // 11
			_T("CLEARRENDERTARGETVIEW"),                               // 12
			_T("CLEARUNORDEREDACCESSVIEW"),                            // 13
			_T("CLEARDEPTHSTENCILVIEW"),                               // 14
			_T("RESOURCEBARRIER"),                                     // 15
			_T("EXECUTEBUNDLE"),                                       // 16
			_T("PRESENT"),                                             // 17
			_T("RESOLVEQUERYDATA"),                                    // 18
			_T("BEGINSUBMISSION"),                                     // 19
			_T("ENDSUBMISSION"),                                       // 20
			_T("DECODEFRAME"),                                         // 21
			_T("PROCESSFRAMES"),                                       // 22
			_T("ATOMICCOPYBUFFERUINT"),                                // 23
			_T("ATOMICCOPYBUFFERUINT64"),                              // 24
			_T("RESOLVESUBRESOURCEREGION"),                            // 25
			_T("WRITEBUFFERIMMEDIATE"),                                // 26
			_T("DECODEFRAME1"),                                        // 27
			_T("SETPROTECTEDRESOURCESESSION"),                         // 28
			_T("DECODEFRAME2"),                                        // 29
			_T("PROCESSFRAMES1"),                                      // 30
			_T("BUILDRAYTRACINGACCELERATIONSTRUCTURE"),                // 31
			_T("EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO"),    // 32
			_T("COPYRAYTRACINGACCELERATIONSTRUCTURE"),                 // 33
			_T("DISPATCHRAYS"),                                        // 34
			_T("INITIALIZEMETACOMMAND"),                               // 35
			_T("EXECUTEMETACOMMAND"),                                  // 36
			_T("ESTIMATEMOTION"),                                      // 37
			_T("RESOLVEMOTIONVECTORHEAP"),                             // 38
			_T("SETPIPELINESTATE1"),                                   // 39
			_T("INITIALIZEEXTENSIONCOMMAND"),                          // 40
			_T("EXECUTEEXTENSIONCOMMAND"),                             // 41
			_T("DISPATCHMESH"),                                        // 42
		};

		// D3D12_DRED_ALLOCATION_TYPE に対応する名前を用意しておきます
		static const TCHAR* AllocTypesNames[] =
		{
			_T("COMMAND_QUEUE"),             // 19 start is not zero!
			_T("COMMAND_ALLOCATOR"),         // 20
			_T("PIPELINE_STATE"),            // 21
			_T("COMMAND_LIST"),              // 22
			_T("FENCE"),                     // 23
			_T("DESCRIPTOR_HEAP"),           // 24
			_T("HEAP"),                      // 25
			_T("UNKNOWN"),                   // 26 not exist
			_T("QUERY_HEAP"),                // 27
			_T("COMMAND_SIGNATURE"),         // 28
			_T("PIPELINE_LIBRARY"),          // 29
			_T("VIDEO_DECODER"),             // 30
			_T("UNKNOWN"),                   // 31 not exist
			_T("VIDEO_PROCESSOR"),           // 32
			_T("UNKNOWN"),                   // 33 not exist
			_T("RESOURCE"),                  // 34
			_T("PASS"),                      // 35
			_T("CRYPTOSESSION"),             // 36
			_T("CRYPTOSESSIONPOLICY"),       // 37
			_T("PROTECTEDRESOURCESESSION"),  // 38
			_T("VIDEO_DECODER_HEAP"),        // 39
			_T("COMMAND_POOL"),              // 40
			_T("COMMAND_RECORDER"),          // 41
			_T("STATE_OBJECT"),              // 42
			_T("METACOMMAND"),               // 43
			_T("SCHEDULINGGROUP"),           // 44
			_T("VIDEO_MOTION_ESTIMATOR"),    // 45
			_T("VIDEO_MOTION_VECTOR_HEAP"),  // 46
			_T("VIDEO_EXTENSION_COMMAND"),   // 47
			_T("INVALID"),                   // 0xffffffff
		};

		// DRED Auto-Breadcrumb の先頭ノード
		const D3D12_AUTO_BREADCRUMB_NODE1* pBreadcrumbHead = nullptr;

		// DRED のインタフェースを取得
		ID3D12DeviceRemovedExtendedData1* pDred = nullptr;
		if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData1), (void**)&pDred)))
		{
			// Auto-Breadcrunmb の先頭ノードを取得
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 DredAutoBreadcrumbsOutput;
			HRESULT hr = pDred->GetAutoBreadcrumbsOutput1(&DredAutoBreadcrumbsOutput);
			if (SUCCEEDED(hr))
			{
				pBreadcrumbHead = DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
			} // End if
			else if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
			{
				LOG(_T("[DRED 1.2] The device is not in a removed state."));
				return;
			}
			else if (hr == DXGI_ERROR_UNSUPPORTED)
			{
				LOG(_T("[DRED 1.2] auto-breadcrumbs have not been enabled."));
				return;
			}
		} // End if (QueryInterface)

		if (pDred)
		{
			// Auto-Breadcrunmb を先頭からたどって出力していく
			// ノードはグラフィクスコマンドリストごとに一つ作成されている
			// 詳細：https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_auto_breadcrumb_node
			if (pBreadcrumbHead)
			{
				LOG(L"[DRED 1.2] Last tracked GPU operations:");

				UINT TracedCommandLists = 0;
				auto pNode = pBreadcrumbHead;
				while (pNode)
				{
					// 最後に実行された Breadcrumb の値
					UINT LastCompletedOp = *pNode->pLastBreadcrumbValue;

					// 最後に実行された Op の値と 全体の Breadcrumb の数が違うなら途中で止まっている
					if ((LastCompletedOp != pNode->BreadcrumbCount) && (LastCompletedOp != 0))
					{
						LOG(L"[DRED 1.2] Auto Breadcrunmb Info");
						LOG(L"\tCommandlistName:         {}", pNode->pCommandListDebugNameW);
						LOG(L"\tCommandQueue:            {}", pNode->pCommandQueueDebugNameW);
						LOG(L"\tNum Breadcrumbs:         {}", pNode->BreadcrumbCount);
						LOG(L"\tLast Breadcrumb:         {}", LastCompletedOp);
						LOG(L"\tNum Breadcrumb Contexts: {}", pNode->BreadcrumbContextsCount);
						LOG(L"------------------------------------");
						TracedCommandLists++;

						// Breadcrumb の数だけ Context (PIX のマーカー) 用の文字列を用意しておく
						// Map を使った方が効率がいい
						std::vector<std::wstring> Contexts;
						Contexts.resize(pNode->BreadcrumbCount);

						// Breadcrumb Context を走査して対応する Breadcrumb の番号に Context の文字列を格納する
						for (UINT i = 0; i < pNode->BreadcrumbContextsCount; ++i)
						{
							D3D12_DRED_BREADCRUMB_CONTEXT Context = pNode->pBreadcrumbContexts[i];
							Contexts[Context.BreadcrumbIndex] = std::format(_T("[{}]"), Context.pContextString);
						}

						// すべての Breadcrumb を出力する
						for (UINT Op = 0; Op < pNode->BreadcrumbCount; ++Op)
						{
							// 最後に実行された Op より前の Breadcrumb は実行されている（[reached]とする）
							const TCHAR* OpRearched = (Op <= LastCompletedOp) ? _T("[reached]") : _T("");

							// イベント名を解決する
							D3D12_AUTO_BREADCRUMB_OP BreadcrumbOp = pNode->pCommandHistory[Op];
							const TCHAR* OpName = (BreadcrumbOp < ARRAYSIZE(OpNames)) ? OpNames[BreadcrumbOp] : _T("Unknown Op");

							// コンテキストのテキストを解決する
							const TCHAR* OpContext = (!Contexts[Op].empty()) ? Contexts[Op].c_str() : _T("");

							// 出力
							LOG(L"\t{} {}| {}{}", Op, OpRearched, OpName, OpContext);
						} // End for (BreadcrumbCount)
					} // End if (LastCompletedOp)

					pNode = pNode->pNext;
				} // End while (pNode)

				if (TracedCommandLists == 0)
				{
					LOG(_T("[DRED 1.2] No command list found."));
				} // End if (TracedCommandLists)
			} // End if (pBreadcrumbHead)
			else
			{
				LOG(_T("[DRED 1.2] No Breadcrumb data."));
			} // End else

			// Page Fault Data を取得する
			D3D12_DRED_PAGE_FAULT_OUTPUT1 DredPageFaultOutput;
			if (SUCCEEDED(pDred->GetPageFaultAllocationOutput1(&DredPageFaultOutput)) && DredPageFaultOutput.PageFaultVA != 0)
			{
				// 取得した Page Fault の仮想アドレス
				LOG(_T("[DRED 1.2] PageFault VA: {:x}"), (long long)DredPageFaultOutput.PageFaultVA);

				// アロケート済みのノードを走査する
				const D3D12_DRED_ALLOCATION_NODE1* pNode = DredPageFaultOutput.pHeadExistingAllocationNode;
				if (pNode)
				{
					LOG(_T("[DRED  1.2] Allocation Nodes:"));
					while (pNode)
					{
						// アロケーションの名前を解決する
						// enum D3D12_DRED_ALLOCATION_TYPE is start with D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE(19)
						UINT index = pNode->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
						const TCHAR* AllocTypeName = (index < ARRAYSIZE(AllocTypesNames)) ? AllocTypesNames[index] : _T("Unknown Alloc");
						LOG(_T("\tName: {} (Type: {})"), pNode->ObjectNameW, AllocTypeName);

						pNode = pNode->pNext;
					} // End while (pNode)
				} // End if (pNode)

				// 直近に解放されたノードを走査する
				pNode = DredPageFaultOutput.pHeadRecentFreedAllocationNode;
				if (pNode)
				{
					LOG(_T("[DRED 1.2] Freed Nodes:"));
					while (pNode)
					{
						// アロケーションの名前を解決する
						UINT index = pNode->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
						const TCHAR* AllocTypeName = (index < ARRAYSIZE(AllocTypesNames)) ? AllocTypesNames[index] : _T("Unknown Alloc");
						LOG(_T("\tName: {} (Type: {})"), pNode->ObjectNameW, AllocTypeName);

						pNode = pNode->pNext;
					} // End while (pNode)
				} // End if (pNode)
			} // End if (DredPageFaultOutput)
			else
			{
				LOG(_T("[DRED 1.2] No PageFault data."));
			} // End else
		} // End if (pDred)
	} // End function


	// Pretty-print a state object tree.
	inline void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
	{
		std::wstringstream wstr;
		wstr << L"\n";
		wstr << L"--------------------------------------------------------------------\n";
		wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
		if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
		if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

		auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
			{
				std::wostringstream woss;
				for (UINT i = 0; i < numExports; i++) {
					woss << L"|";
					if (depth > 0) {
						for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
					}
					woss << L" [" << i << L"]: ";
					if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
					woss << exports[i].Name << L"\n";
				}
				return woss.str();
			};

		for (UINT i = 0; i < desc->NumSubobjects; i++) {
			wstr << L"| [" << i << L"]: ";
			switch (desc->pSubobjects[i].Type) {
			case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
				wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
				wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
				wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
			{
				wstr << L"DXIL Library 0x";
				auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
				wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
				wstr << ExportTree(1, lib->NumExports, lib->pExports);
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
			{
				wstr << L"Existing Library 0x";
				auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
				wstr << collection->pExistingCollection << L"\n";
				wstr << ExportTree(1, collection->NumExports, collection->pExports);
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
			{
				wstr << L"Subobject to Exports Association (Subobject [";
				auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
				UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
				wstr << index << L"])\n";
				for (UINT j = 0; j < association->NumExports; j++) {
					wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
				}
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
			{
				wstr << L"DXIL Subobjects to Exports Association (";
				auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
				wstr << association->SubobjectToAssociate << L")\n";
				for (UINT j = 0; j < association->NumExports; j++) {
					wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
				}
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
			{
				wstr << L"Raytracing Shader Config\n";
				auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
				wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
				wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
			{
				wstr << L"Raytracing Pipeline Config\n";
				auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
				wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
			{
				wstr << L"Hit Group (";
				auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
				wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
				wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
				wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
				wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
				break;
			}
			}
			wstr << L"|--------------------------------------------------------------------\n";
		}
		wstr << L"\n";

		OutputDebugStringW(wstr.str().c_str());
		LOG(L"{}", wstr.str());
	}


	/***********************************************************************/
	//Method for misc. class & struct
	/***********************************************************************/
	
	//ResCapsのフラグでor,andとれるようにする演算子
	ResCaps operator|(ResCaps L, ResCaps R) { return static_cast<ResCaps>(static_cast<uint64_t>(L) | static_cast<uint64_t>(R)); }
	ResCaps operator&(ResCaps L, ResCaps R) { return static_cast<ResCaps>(static_cast<uint64_t>(L) & static_cast<uint64_t>(R)); }

	Shader::Shader(const wchar_t* filename, const wchar_t* _entrypoint)
	{
		//コンパイル済みシェーダのロード
		ComPtr<IDxcLibrary> library;
		DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
		IDxcBlobEncoding* pBlob;
		UINT32 cp = 0;
		library->CreateBlobFromFile(filename, &cp, &pBlob);

		entrypoint = _entrypoint;
		blob = ComPtr<IDxcBlob>(pBlob);
	}

	void Shader::SaveToFile(const wchar_t* filename)
	{
		std::ofstream file(filename, std::ios::binary);
		file.write((char*)blob->GetBufferPointer(), blob->GetBufferSize());
	}

	void Res::SetName(const wchar_t* name)
	{
		res->SetName(name);
	}

	std::wstring Res::Name()
	{
		UINT size = 0;
		std::wstring ret;
		res->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, nullptr);
		ret.resize(size / sizeof(wchar_t));
		res->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, ret.data());
		return ret;
	}

	/***********************************************************************/
	//DXR
	/***********************************************************************/
	
	void DXR::AddBarrier(Res* r, std::vector<D3D12_RESOURCE_BARRIER>& bar, D3D12_RESOURCE_STATES after) const
	{
		//元がGENERIC_READなmap可能リソースの場合はアップロードバッファなので、それ以外にならない
		//24朝追加
		if ((r->state == D3D12_RESOURCE_STATE_GENERIC_READ) && ((r->caps & ResCaps::map) != ResCaps::none))
			return;

		if (after != r->state) {
			bar.push_back(CD3DX12_RESOURCE_BARRIER::Transition(r->res.Get(), r->state, after));
			r->state = after;
		}
	}


	//TLAS作成・更新用アップロードバッファの作成
	ComPtr<ID3D12Resource> DXR::AllocateUploadBuffer(const void* pData, UINT64 datasize)
	{
		ComPtr<ID3D12Resource> ppResource;
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto rd = CD3DX12_RESOURCE_DESC::Buffer(datasize);
		ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,IID_PPV_ARGS(&ppResource)), L"AllocateUploadBuffer Failed", __FILEW__, __LINE__);
		void* pMappedData;
		ppResource->Map(0, nullptr, &pMappedData);
		memcpy(pMappedData, pData, datasize);
		ppResource->Unmap(0, nullptr);
		return ppResource;
	}

	Buf DXR::CreateBuf(const void* data, UINT elemsize, UINT count, D3D12_HEAP_FLAGS flags) 
	{
		Buf bb = {};
		bb.caps = ResCaps::srv;
		bb.elemSize = elemsize;

		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto rd = CD3DX12_RESOURCE_DESC::Buffer(size_t(elemsize) * count);
		auto hr = m_device->CreateCommittedResource(&hp, flags, &rd, bb.state, nullptr, IID_PPV_ARGS(&bb.res));

		//GPUに中身アップロードする
		if (data != nullptr)
			Upload(bb, data);

		return bb;
	}


	Buf DXR::CreateBufCPU(const void* data, UINT elemsize, UINT count, bool upload, bool download, D3D12_HEAP_FLAGS flags) 
	{
		//LOG(L"CreateBufCPU called elemsize:{} count:{} upload:{} download:{} flags:{:x}", elemsize, count, upload, download, (DWORD)flags);

		Buf bb = {};
		bb.caps = ResCaps::map | ResCaps::srv;
		bb.elemSize = elemsize;
		HRESULT hr;

		if (upload && download) {
			//アップロード・ダウンロード両方
			auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);
			auto rd = CD3DX12_RESOURCE_DESC::Buffer(size_t(elemsize) * count);
			hr = m_device->CreateCommittedResource(&hp, flags, &rd, bb.state, nullptr, IID_PPV_ARGS(&bb.res));
			ThrowIfFailed(hr, L"CreateCommittedResource Failed", __FILEW__, __LINE__);
		} else if (download) {
			//GPU→CPUの読み込みのみ
			auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
			auto rd = CD3DX12_RESOURCE_DESC::Buffer(size_t(elemsize) * count);
			bb.state = D3D12_RESOURCE_STATE_COPY_DEST;	//23夜追加	RTX3060もこれで行ける？
			hr = m_device->CreateCommittedResource(&hp, flags, &rd, bb.state, nullptr, IID_PPV_ARGS(&bb.res));
			ThrowIfFailed(hr, L"CreateCommittedResource Failed", __FILEW__, __LINE__);
		} else if (upload) {
			//CPU→GPUの書き込みのみ
			auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			auto rd = CD3DX12_RESOURCE_DESC::Buffer(size_t(elemsize) * count);
			//D3D12_HEAP_TYPE_UPLOAD の時はGENERIC_READを指定しないといけないと、記述がある
			//でも、ComputeShaderのSRVに入れるためにステート変更しようとすると失敗するのでCOPY_SOURCEを指定していた
			// というわけで、↑についてはアップロードバッファはステートを変更できないという事にした
			bb.state = D3D12_RESOURCE_STATE_GENERIC_READ;	//23夜追加 
			hr = m_device->CreateCommittedResource(&hp, flags, &rd, bb.state, nullptr, IID_PPV_ARGS(&bb.res));
			ThrowIfFailed(hr, L"CreateCommittedResource Failed", __FILEW__, __LINE__);
		} else {
			hr = E_INVALIDARG;	//両方falseの場合は失敗させる
			ThrowIfFailed(hr, L"CreateCommittedResource Failed", __FILEW__, __LINE__);
		}
		

		//dataがnullptrでなければ内容の初期化
		if (data != nullptr) {
			void* pData;
			ThrowIfFailed(bb.res->Map(0, nullptr, &pData), L"Map Failed", __FILEW__, __LINE__);
			memcpy(pData, data, size_t(elemsize) * count);
			bb.res->Unmap(0, nullptr);
		}

		return bb;
	}

	Buf DXR::CreateRWBuf(UINT elemsize, UINT count, D3D12_HEAP_FLAGS flags) 
	{
		Buf rwb = {};
		rwb.caps = ResCaps::uav | ResCaps::srv;
		rwb.elemSize = elemsize;
		auto rd = CD3DX12_RESOURCE_DESC::Buffer(UINT64(elemsize) * count, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		ThrowIfFailed(m_device->CreateCommittedResource(&hp, flags, &rd, rwb.state, nullptr, IID_PPV_ARGS(rwb.res.ReleaseAndGetAddressOf())),
			L"CreateRWBuf failed", __FILEW__, __LINE__);
		return rwb;
	}


	CB DXR::CreateCB(const void* data, UINT elemsize, D3D12_HEAP_FLAGS flags) 
	{

		CB cb = {};
		cb.caps = ResCaps::cbv;
		cb.elemSize = elemsize;
		// 24朝変更 HEAP_TYPE_UPLOADなので生成時にはGENERIC_READにしとく
		cb.state = D3D12_RESOURCE_STATE_GENERIC_READ;	//D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		size_t sizeInGPU = Align(elemsize, 256);

		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInGPU);
		ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProperties,flags,&bufferDesc,cb.state,nullptr,
			IID_PPV_ARGS(cb.res.ReleaseAndGetAddressOf())), L"CreateCB Failed", __FILEW__, __LINE__);
		ThrowIfFailed(cb.res->Map(0, nullptr, &cb.pData), L"constantbuffer->Map failed", __FILEW__, __LINE__);
		if (data != nullptr)
			memcpy(cb.pData, data, elemsize);

		return cb;
	}

	Tex2D DXR::CreateTex2D(const wchar_t* filename, ColorSpace opt, D3D12_HEAP_FLAGS flags)
	{
		DirectX::TexMetadata meta;
		DirectX::ScratchImage simg;
		std::wstring textureName = filename;
		const wchar_t* resourceName = filename;

		Tex2D ret = {};
		ret.caps = ResCaps::srv;
		ret.type = ResType::tex2D;

		auto idx = textureName.rfind(L".");
		if (idx == std::string::npos) {
			ThrowIfFailed(E_FAIL, std::format(L"DXR::CreateTex2D failed ... Unsupproted image file {}  in YRZ.ixx line {}" ,filename), __FILEW__, __LINE__);
		}
		HRESULT hr;
		std::wstring ext = textureName.substr(idx, 4);
		std::transform(ext.begin(), ext.end(), ext.begin(), tolower);
		

		if (ext == L".dds") {
			hr = LoadFromDDSFile(textureName.c_str(), DirectX::DDS_FLAGS_NONE, &meta, simg);
		} else if (ext == L".tga") {
			auto f = DirectX::TGA_FLAGS_NONE;
			if (opt == ColorSpace::srgb)
				f = DirectX::TGA_FLAGS_DEFAULT_SRGB;
			else if (opt == ColorSpace::linear)
				f = DirectX::TGA_FLAGS_IGNORE_SRGB;

			hr = DirectX::LoadFromTGAFile(textureName.c_str(), f, &meta, simg);
		} else if (ext == L".hdr") {
			hr = LoadFromHDRFile(textureName.c_str(), &meta, simg);
		} else {
			auto f = DirectX::WIC_FLAGS_NONE;
			if (opt == ColorSpace::srgb)
				f = DirectX::WIC_FLAGS_DEFAULT_SRGB;
			else if (opt == ColorSpace::linear)
				f = DirectX::WIC_FLAGS_IGNORE_SRGB;
			hr = LoadFromWICFile(textureName.c_str(), f, &meta, simg);
		}

		ThrowIfFailed(hr, std::format(L"Failed to load image ({})", filename), __FILEW__, __LINE__);

		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto rd = CD3DX12_RESOURCE_DESC::Tex2D(meta.format, meta.width, meta.height, meta.arraySize, meta.mipLevels);
		ThrowIfFailed(m_device->CreateCommittedResource(&hp, flags, &rd, ret.state, nullptr, IID_PPV_ARGS(ret.res.ReleaseAndGetAddressOf())),
			std::format(L"CreateCommittedResource failed({})", resourceName), __FILEW__, __LINE__);

		ret.res->SetName(resourceName);

		//全MIPレベルを読み込む
		const DirectX::Image* pimg = simg.GetImages();
		for (int i = 0; i < simg.GetImageCount(); i++) {
			Upload(ret, pimg[i].pixels, i, i);
		}

		return ret;
	}

	Tex2D DXR::CreateTex2D(int width, int height, DXGI_FORMAT fmt, int mipLevels, D3D12_HEAP_FLAGS flags) 
	{
		//空のテクスチャを作る
		Tex2D ret;
		ret.caps = ResCaps::srv;
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto rd = CD3DX12_RESOURCE_DESC::Tex2D(fmt, width, height, 1, mipLevels);

		ThrowIfFailed(m_device->CreateCommittedResource(&hp, flags, &rd, ret.state, nullptr, IID_PPV_ARGS(ret.res.ReleaseAndGetAddressOf())),
			L"CreateCommittedResource Failed", __FILEW__, __LINE__);
		ret.type = ResType::tex2D;

		return ret;
	}

	void DXR::AllocateUAVBufferAligned(UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState, size_t align)
	{
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto rd = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, align);
		ThrowIfFailed(m_device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,initialResourceState,nullptr,
			IID_PPV_ARGS(ppResource)), L"AllocateUAVBuffer Failed in YRZ.ixx", __FILEW__, __LINE__);
	}


	void DXR::AllocateUAVBuffer(UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState)
	{
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto rd = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initialResourceState,
			nullptr, IID_PPV_ARGS(ppResource)), L"AllocateUAVBuffer Failed", __FILEW__, __LINE__);
	}


	//スクラッチバッファからBLAS/TLASを作る仕上げの部分だけ
	void DXR::BuildASFromScratchBuffer(ComPtr<ID3D12Resource>& xlas, ComPtr<ID3D12Resource>& scratchResource, D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc)
	{
		OpenCommandList();

		//肝心のビルドコマンド
		m_cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr); //&postbuildInfo

		//バリア
		D3D12_RESOURCE_BARRIER uavBarrier{};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = xlas.Get();
		m_cmdList->ResourceBarrier(1, &uavBarrier);

		ExecuteCommandList();
	}


	BLAS DXR::BuildBLASFlow(D3D12_RAYTRACING_GEOMETRY_DESC& gd)
	{
		//BuildBLAS ポリゴン版とプロシージャル版の共通部分
		BLAS blas = {};

		// Get required sizes for an acceleration structure.
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs = bottomLevelBuildDesc.Inputs;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		bottomLevelInputs.Flags = buildFlags;
		bottomLevelInputs.NumDescs = 1;
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.pGeometryDescs = &gd;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
		m_device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
		ThrowIfFailed(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0 ? S_OK : E_FAIL,
			std::format(L"DXR::BuildBLASFlow() failed ... incorrect bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes {}", bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes), __FILEW__, __LINE__);

		ComPtr<ID3D12Resource> scratchResource;
		AllocateUAVBuffer(bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &scratchResource, D3D12_RESOURCE_STATE_COMMON);

		// Allocate resources for acceleration structures.
		// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
		// Default heap is OK since the application doesn稚 need CPU read/write access to them. 
		// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
		// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
		//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
		//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
		D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		AllocateUAVBuffer(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, blas.res.ReleaseAndGetAddressOf(), initialResourceState);

		bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
		bottomLevelBuildDesc.DestAccelerationStructureData = blas.res->GetGPUVirtualAddress();

		//アップデート用バッファの作成
		AllocateUAVBuffer(bottomLevelPrebuildInfo.UpdateScratchDataSizeInBytes, blas.update.ReleaseAndGetAddressOf(), D3D12_RESOURCE_STATE_COMMON);

		//仕上げ
		BuildASFromScratchBuffer(blas.res, scratchResource, bottomLevelBuildDesc);

		blas.state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		return blas;
	}

	DXGI_FORMAT IndexFormatFromSize(UINT sizeInBytes)
	{
		if (sizeInBytes == 4)
			return DXGI_FORMAT_R32_UINT;
		else if (sizeInBytes == 2)
			return DXGI_FORMAT_R16_UINT;
		else if (sizeInBytes == 1)
			return DXGI_FORMAT_R8_UINT;
		else
			return DXGI_FORMAT_UNKNOWN;
	}

	BLAS DXR::BuildBLAS(Buf& VB, Buf& IB, DXGI_FORMAT positionFormat, D3D12_RAYTRACING_GEOMETRY_FLAGS flags)
	{
		//VBとIBの組からBLASを作る。positionFomatはVBの中で頂点の位置を示している部分のフォーマット
		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.IndexBuffer = IB.res->GetGPUVirtualAddress();
		geometryDesc.Triangles.IndexCount = (UINT)(IB.desc().Width / IB.elemSize);
		geometryDesc.Triangles.Transform3x4 = 0;
		geometryDesc.Triangles.VertexFormat = positionFormat;
		geometryDesc.Triangles.VertexCount = (UINT)(VB.desc().Width / VB.elemSize);
		geometryDesc.Triangles.VertexBuffer.StartAddress = VB.res->GetGPUVirtualAddress();
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = VB.elemSize;
		geometryDesc.Triangles.IndexFormat = IndexFormatFromSize(IB.elemSize);

		geometryDesc.Flags = flags;

		return BuildBLASFlow(geometryDesc);

	}

	BLAS DXR::BuildBLAS(int aabbCount, const D3D12_RAYTRACING_AABB* aabb, D3D12_RAYTRACING_GEOMETRY_FLAGS flags)
	{
		//BLASを作る(プロシージャル版 AABBの中でintersection shaderが起動されてカスタム当たり判定をとれる)
		//AABB配列をアップロード
		Buf uploader = CreateBufCPU(aabb, sizeof(D3D12_RAYTRACING_AABB), aabbCount, true, false);

		D3D12_RAYTRACING_GEOMETRY_DESC gd = {};
		gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
		gd.Flags = flags;
		gd.AABBs.AABBCount = aabbCount;
		gd.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
		gd.AABBs.AABBs.StartAddress = uploader.res->GetGPUVirtualAddress();

		BLAS blas = BuildBLASFlow(gd);
		blas.AABBUploader = uploader;
		uploader.res->Map(0, nullptr, (void**)(&blas.pAABB));
		return blas;
	}

	TLAS DXR::BuildTLAS(int count, const BLAS* BLASs, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags)
	{
		TLAS tlas = {};

		UINT nInst = count;

		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDesc(nInst);
		for (int i = 0; i < (int)nInst; i++) {
			instanceDesc[i].InstanceID = BLASs[i].ID;
			instanceDesc[i].InstanceContributionToHitGroupIndex = BLASs[i].contributionToHitGroupIndex;
			instanceDesc[i].InstanceMask = BLASs[i].mask;
			memcpy(instanceDesc[i].Transform, &BLASs[i].transform, sizeof(DirectX::XMFLOAT3X4));
			instanceDesc[i].AccelerationStructure = BLASs[i].res->GetGPUVirtualAddress();
			instanceDesc[i].Flags = BLASs[i].flags;
		}
		tlas.instanceDescBuffer = AllocateUploadBuffer(instanceDesc.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * nInst);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
		ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		ASInputs.InstanceDescs = tlas.instanceDescBuffer->GetGPUVirtualAddress();
		ASInputs.NumDescs = nInst;
		ASInputs.Flags = flags;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
		m_device->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

		ASPreBuildInfo.ResultDataMaxSizeInBytes = Align(ASPreBuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
		ASPreBuildInfo.ScratchDataSizeInBytes = Align(ASPreBuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
		size_t tlasSize = ASPreBuildInfo.ResultDataMaxSizeInBytes;

		ComPtr<ID3D12Resource>scratchBuffer;
		size_t alignment = max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		AllocateUAVBufferAligned(ASPreBuildInfo.ScratchDataSizeInBytes, &scratchBuffer, D3D12_RESOURCE_STATE_COMMON, alignment);
		AllocateUAVBufferAligned(ASPreBuildInfo.ResultDataMaxSizeInBytes, tlas.res.ReleaseAndGetAddressOf(), D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, alignment);
		
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
		buildDesc.Inputs = ASInputs;
		buildDesc.ScratchAccelerationStructureData = scratchBuffer->GetGPUVirtualAddress();
		buildDesc.DestAccelerationStructureData = tlas.res->GetGPUVirtualAddress();

		//アップデート用バッファの作成
		AllocateUAVBuffer(ASPreBuildInfo.UpdateScratchDataSizeInBytes, tlas.update.ReleaseAndGetAddressOf(), D3D12_RESOURCE_STATE_COMMON);

		BuildASFromScratchBuffer(tlas.res, scratchBuffer, buildDesc);

		tlas.caps = ResCaps::srv;
		tlas.state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		tlas.type = ResType::tlas;
		return tlas;
	}

	//UPdateBLAS共通部分
	void DXR::UpdateBLASFlow(BLAS& blas, D3D12_RAYTRACING_GEOMETRY_DESC& gd)
	{
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs = bottomLevelBuildDesc.Inputs;
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		bottomLevelInputs.Flags = buildFlags;
		bottomLevelInputs.NumDescs = 1;
		bottomLevelInputs.pGeometryDescs = &gd;

		bottomLevelBuildDesc.DestAccelerationStructureData = blas.res->GetGPUVirtualAddress();
		bottomLevelBuildDesc.SourceAccelerationStructureData = blas.res->GetGPUVirtualAddress();
		bottomLevelBuildDesc.ScratchAccelerationStructureData = blas.update->GetGPUVirtualAddress();

		//更新(コマリスに積むだけ。presentの時に一気にやる)
		m_cmdList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
		auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(blas.res.Get());
		m_cmdList->ResourceBarrier(1, &barrier);
	}

	void DXR::UpdateBLAS(BLAS& blas, Buf& VB, Buf& IB, DXGI_FORMAT positionFormat, D3D12_RAYTRACING_GEOMETRY_FLAGS flags)
	{
		//VBとIBにバリア
		std::vector<D3D12_RESOURCE_BARRIER>bar;
		AddBarrier(&VB, bar, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		AddBarrier(&IB, bar, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (bar.size())
			m_cmdList->ResourceBarrier(bar.size(), bar.data());

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.IndexBuffer = IB.res->GetGPUVirtualAddress();
		geometryDesc.Triangles.IndexCount = IB.desc().Width / IB.elemSize;
		geometryDesc.Triangles.IndexFormat = IndexFormatFromSize(IB.elemSize);
		geometryDesc.Triangles.Transform3x4 = 0;
		geometryDesc.Triangles.VertexFormat = positionFormat;
		geometryDesc.Triangles.VertexCount = VB.desc().Width / VB.elemSize;
		geometryDesc.Triangles.VertexBuffer.StartAddress = VB.res->GetGPUVirtualAddress();
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = VB.elemSize;
		geometryDesc.Flags = flags;

		UpdateBLASFlow(blas, geometryDesc);
	}

	void DXR::UpdateBLAS(BLAS& blas, int aabbCount, const D3D12_RAYTRACING_AABB* aabb, D3D12_RAYTRACING_GEOMETRY_FLAGS flags)
	{
		memcpy(blas.pAABB, aabb, aabbCount * sizeof(D3D12_RAYTRACING_AABB));

		//バリア
		std::vector<D3D12_RESOURCE_BARRIER>bar;
		AddBarrier(&blas.AABBUploader, bar, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (bar.size())
			m_cmdList->ResourceBarrier(bar.size(), bar.data());

		D3D12_RAYTRACING_GEOMETRY_DESC gd = {};
		gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
		gd.Flags = flags;
		gd.AABBs.AABBCount = aabbCount;
		gd.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
		gd.AABBs.AABBs.StartAddress = blas.AABBUploader.res->GetGPUVirtualAddress();

		UpdateBLASFlow(blas, gd);
	}

	void DXR::UpdateTLAS(TLAS& tlas, int count, const BLAS* BLASs)
	{
		int nInst = count;

		std::vector<D3D12_RAYTRACING_INSTANCE_DESC>instanceDesc;
		instanceDesc.resize(nInst);

		for (int i = 0; i < nInst; i++) {
			instanceDesc[i].InstanceID = BLASs[i].ID;
			instanceDesc[i].InstanceContributionToHitGroupIndex = BLASs[i].contributionToHitGroupIndex;
			instanceDesc[i].InstanceMask = BLASs[i].mask;
			memcpy(instanceDesc[i].Transform, &BLASs[i].transform, sizeof(DirectX::XMFLOAT3X4));
			instanceDesc[i].AccelerationStructure = BLASs[i].res->GetGPUVirtualAddress();
			instanceDesc[i].Flags = BLASs[i].flags;
		}
		tlas.instanceDescBuffer = AllocateUploadBuffer(instanceDesc.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * nInst);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
		ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		ASInputs.InstanceDescs = tlas.instanceDescBuffer->GetGPUVirtualAddress();
		ASInputs.NumDescs = nInst;
		ASInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
		buildDesc.Inputs = ASInputs;
		buildDesc.SourceAccelerationStructureData = tlas.res->GetGPUVirtualAddress();
		buildDesc.DestAccelerationStructureData = tlas.res->GetGPUVirtualAddress();
		buildDesc.ScratchAccelerationStructureData = tlas.update->GetGPUVirtualAddress();

		//更新(コマリスに積むだけ。presentの時に一気にやる)
		m_cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
		auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.res.Get());
		m_cmdList->ResourceBarrier(1, &barrier);
	}



	Shader DXR::CompileShaderMemory(const void* pShaderSource, size_t shaderSourceSize, IDxcIncludeHandler* pIncludeHandler, const wchar_t* name, const wchar_t* entrypoint, const wchar_t* target, const std::vector<LPCWSTR>& options)
	{
		Shader ret = {};

		//IDxcCompiler3での方法。HitGroup名をシェーダ内で記述しないといけないっぽい。include対応した
		ComPtr<IDxcCompiler3> pCompiler;
		ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler)), L"DxcCreateInstance failed", __FILEW__, __LINE__);

		ComPtr<IDxcUtils> pUtils;
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
		ComPtr<IDxcBlobEncoding> pSource;
		pUtils->CreateBlob(pShaderSource, shaderSourceSize, CP_UTF8, pSource.GetAddressOf());

		DxcBuffer Source = {};
		Source.Ptr = pSource->GetBufferPointer();
		Source.Size = pSource->GetBufferSize();
		Source.Encoding = DXC_CP_UTF8;
		ComPtr<IDxcResult> result;
		std::vector<LPCWSTR> args;
		args = options;
		args.push_back(L"-T");
		args.push_back(target);
		args.push_back(L"-E");
		args.push_back(entrypoint);

		HRESULT hr = pCompiler->Compile(&Source, args.data(), args.size(), pIncludeHandler, IID_PPV_ARGS(&result));
		result->GetStatus(&hr);


		if (FAILED(hr)) {
			ComPtr<IDxcBlobEncoding> errorsBlob;
			result->GetErrorBuffer(&errorsBlob);
			std::wstring errstr = std::format(L"\n++++++++Compile Error in ({}) ++++++++\n", name);
			errstr += UTF8Towstr((char*)errorsBlob->GetBufferPointer());
			errstr += L"\n++++++++ Compile Error End +++++++++\n";
			ThrowIfFailed(hr, errstr, __FILEW__, __LINE__);
			return ret;
		}

		ComPtr<IDxcBlob> byteCode;
		result->GetResult(&byteCode);

		ret.entrypoint = entrypoint;
		ret.blob = byteCode;
		return ret;
	}

	Shader DXR::CompileShader(const wchar_t* filename, const wchar_t* entrypoint, const wchar_t* target, const std::vector<LPCWSTR>& options)
	{
		Shader ret = {};

		FILE* fp;
		_wfopen_s(&fp, filename, L"rb");
		if (fp == nullptr) {
			ThrowIfFailed(E_FAIL, L"DXR::CompileShader() failed ... can't open file " + std::wstring(filename), __FILEW__, __LINE__);
			return ret;
		}
		fseek(fp, 0, SEEK_END);
		size_t shaderSourceSize = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char* pShaderSource = new char[shaderSourceSize];
		fread_s(pShaderSource, shaderSourceSize, shaderSourceSize, 1, fp);
		fclose(fp);
		
		//filenameのおいてあるディレクトリをデフォルトのインクルードパスとする
		std::wstring ipath = L"";
		std::wstring fnstr = filename;
		size_t idx = fnstr.rfind(L'\\');
		if (idx == std::wstring::npos)
			idx = fnstr.rfind(L'/');
		if (idx != std::wstring::npos) {
			ipath = fnstr.substr(0, idx + 1);
		}
		//インクルードハンドラの作成、途中にフォルダ名が含まれる場合、そのフォルダを基準にインクルードファイルを検索する
		//MMEの動作と同じで多段インクルードの場合は最初に読み込まれたhlslを基準にして探索するだけなので注意が必要かも
		auto includeHandler = CustomIncludeHandler();
		includeHandler.InitialPath = ipath;

		ret = CompileShaderMemory(pShaderSource, shaderSourceSize, &includeHandler, filename, entrypoint, target, options);

		delete[] pShaderSource;
		return ret;
	}

	Tex2D DXR::CreateRWTex2D(int width, int height, DXGI_FORMAT fmt, int mipLevels, D3D12_HEAP_FLAGS flags)
	{
		Tex2D dxrout = {};

		D3D12_RESOURCE_DESC desc = {};
		desc.DepthOrArraySize = 1;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Format = fmt;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		desc.Width = width;
		desc.Height = height;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.MipLevels = mipLevels;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		HRESULT hr = m_device->CreateCommittedResource(&hp, flags, &desc, dxrout.state, nullptr, IID_PPV_ARGS(dxrout.res.ReleaseAndGetAddressOf()));
		ThrowIfFailed(hr, L"Error: failed to create RWTex2D", __FILEW__, __LINE__);
		dxrout.caps = ResCaps::uav | ResCaps::srv;
		dxrout.type = ResType::tex2D;
		return dxrout;
	}


	Tex2D DXR::CreateRT2D(int width, int height, DXGI_FORMAT fmt, int mipLevels, D3D12_HEAP_FLAGS flags)
	{
		Tex2D tex = {};

		auto rd = CD3DX12_RESOURCE_DESC::Tex2D(fmt, width, height, 1, mipLevels);
		rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		float cc[4] = { 0,0,0,1 };
		D3D12_CLEAR_VALUE cv = CD3DX12_CLEAR_VALUE(fmt, cc);
		ThrowIfFailed(m_device->CreateCommittedResource(&hp, flags, &rd, tex.state, &cv,
			IID_PPV_ARGS(tex.res.ReleaseAndGetAddressOf())), L"CreateCommittedResource failed", __FILEW__, __LINE__);

		tex.caps = ResCaps::rtv | ResCaps::srv;
		tex.type = ResType::tex2D;
		
		return tex;
	}

	Tex2D DXR::CreateZBuf(int width, int height, DXGI_FORMAT fmt, int mipLevels, D3D12_HEAP_FLAGS flags)
	{
		Tex2D tex = {};

		auto rd = CD3DX12_RESOURCE_DESC::Tex2D(fmt, width, height, 1, mipLevels);
		rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_CLEAR_VALUE cv = CD3DX12_CLEAR_VALUE(fmt, 1,0);
		ThrowIfFailed(m_device->CreateCommittedResource(&hp, flags, &rd, tex.state, &cv,
			IID_PPV_ARGS(tex.res.ReleaseAndGetAddressOf())), L"CreateCommittedResource failed", __FILEW__, __LINE__);

		tex.caps = ResCaps::dsv;
		tex.type = ResType::tex2D;

		return tex;
	}


	void DXR::CreateDevice(D3D_FEATURE_LEVEL featureLevel)
	{
		bool debugDXGI = false;
#if defined(_DEBUG) || defined(DEBUG_LAYER)
		// D3D12 debug layerを有効にする
		{
			ID3D12Debug* debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();

				//強化版デバコン
				CComPtr<ID3D12Debug1> spDebugController1;
				debugController->QueryInterface(IID_PPV_ARGS(&spDebugController1));
				spDebugController1->SetEnableGPUBasedValidation(true);
				spDebugController1->SetEnableSynchronizedCommandQueueValidation(true);
				spDebugController1->EnableDebugLayer();

				CComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
				D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings));

				//忍びの五色米を撒くようにすべし
				pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
				pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

				ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
				if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.ReleaseAndGetAddressOf()))))
				{
					debugDXGI = true;

					ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(m_factory.ReleaseAndGetAddressOf())), L"CreateDXGIFactory2 Failed", __FILEW__, __LINE__);

					dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
					dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
				}

			}
		}
		LOG(L"D3D12 debug layer initialized");
#endif
		//必須デバイス作成
		if (!debugDXGI)
			ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory)), L"CreateDXGIFactory1 Failed", __FILEW__, __LINE__);
		
		//(DXRをサポートしてる)ソフトウェアエミュでないアダプターを探す
		int idx = -1;
		while (1) {
			idx++;
			if (m_factory->EnumAdapters1(idx, &m_adapter) != S_OK)
				break;
			
			DXGI_ADAPTER_DESC1 ad;
			m_adapter->GetDesc1(&ad);

			LOG(L"{} : VRAM {}MB, SysRAM {}MB, Shared {}MB",
				ad.Description, ad.DedicatedVideoMemory/1048576, ad.DedicatedSystemMemory/1048576, ad.SharedSystemMemory/1048576);

			if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				LOG(L"its' software adapter");
				continue;
			}
			
			if (!IsDirectXRaytracingSupported(m_adapter.Get()) && m_raytracingSupport) {
				LOG(L"its' not DXR support");
				continue;
			}

			HRESULT hr = D3D12CreateDevice(m_adapter.Get(), featureLevel, IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()));
			if (hr == S_OK) {
				LOG(L"it's good, device created");
				m_device->SetName(std::format(L"d3d12 device({})", ad.Description).c_str());
					break;
			} else {
				LOG(L"it's not supports D3DFeatureLevel {}.{}", ((UINT)featureLevel & 0xF000) >> 12 , ((UINT)featureLevel & 0xF00) >> 8);
			}
		}
		if (m_device == nullptr) {
			if (m_raytracingSupport)
				ThrowIfFailed(E_FAIL, L"DXR::CreateDevice() failed ...  not found DirectX Raytracing compliant device", __FILEW__, __LINE__);
			else
				ThrowIfFailed(E_FAIL, L"DXR::CreateDevice() failed ...  not found appropriate D3D12 device", __FILEW__, __LINE__);
		}

#if defined(_DEBUG) || defined(DEBUG_LAYER)
		ComPtr<ID3D12InfoQueue1> pInfoQueue;
		if (SUCCEEDED(m_device.As(&pInfoQueue))) {
			//D3D12からのエラーでブレークしたい場合はコメント外す
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);	// コメントアウトしない
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);			// コメントアウト
			//pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);		// コメントアウト
			D3D12_INFO_QUEUE_FILTER NewFilter = {};
			D3D12_MESSAGE_SEVERITY Severities[] =
			{
				D3D12_MESSAGE_SEVERITY_INFO
			};
			D3D12_MESSAGE_ID DenyIds[] = {
				//↓D3D11on12サンプルより
				// This occurs when there are uninitialized descriptors in a descriptor table, even when a
				// shader does not access the missing descriptors.
				D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
				//Direct2Dでテキスト描かせると↓の4つが猛烈な勢いで出るので
				//必要な場合はコメントアウトしてね
				D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED,
				D3D12_MESSAGE_ID_BEGIN_EVENT,
				D3D12_MESSAGE_ID_END_EVENT,
				D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_INCOMPATIBLE_RESOURCE_STATE,
				//↓これもウザかったので足した
				D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
			};
			//NewFilter.DenyList.NumCategories = _countof(Categories);
			//NewFilter.DenyList.pCategoryList = Categories;
			NewFilter.DenyList.NumSeverities = _countof(Severities);
			NewFilter.DenyList.pSeverityList = Severities;
			NewFilter.DenyList.NumIDs = _countof(DenyIds);
			NewFilter.DenyList.pIDList = DenyIds;
			ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter), L"PushStorageFilter Failed", __FILEW__, __LINE__);

			//デバッグレイヤーからの出力をログに出力す
			#ifdef LOG_DEBUG_LAYER
			DWORD cookie = 0;
			pInfoQueue->RegisterMessageCallback(
				[](D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity, D3D12_MESSAGE_ID ID, LPCSTR pDescription, void* pContext) {
						std::wstring msgw = ANSITowstr(pDescription);
						LOG(L"●DebugLayer 【{}】cat:{}, severity:{}, ID:{}", msgw, (DWORD)Category, (DWORD)Severity, (DWORD)ID);
				},
				D3D12_MESSAGE_CALLBACK_FLAG_NONE,
				nullptr,
				&cookie
			);
			#endif	//LOG_DEBUG_LAYER
		}
#endif	//defined(_DEBUG) || defined(DEBUG_LAYER)

		//コマキュー
		D3D12_COMMAND_QUEUE_DESC qd = {};
		ThrowIfFailed(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(m_cmdQueue.ReleaseAndGetAddressOf())), L"CreateCommandQueue Failed", __FILEW__, __LINE__);

		//コマアロ
		for (UINT n = 0; n < m_BBcount; n++)
			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_cmdAlloc[n].ReleaseAndGetAddressOf())), L"CreateCommandAllocator Failed", __FILEW__, __LINE__);

		//コマリス
		ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAlloc[0].Get(), nullptr, IID_PPV_ARGS(m_cmdList.ReleaseAndGetAddressOf())), L"CreateCommandList Failed", __FILEW__, __LINE__);
		ThrowIfFailed(m_cmdList->Close(), L"m_cmdList->Close() Failed", __FILEW__, __LINE__);

		//フェンス
		ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_backBufferIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())), L"CreateFence Failed", __FILEW__, __LINE__);
		m_fenceValues[m_backBufferIndex]++;

		m_fenceEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
		if (!m_fenceEvent.IsValid())
		{
			ThrowIfFailed(E_FAIL, L"DXR::CreateDevice() failed ... CreateEvent failed", __FILEW__, __LINE__);
		}

		//ComputeShader用
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(m_CSAlloc.ReleaseAndGetAddressOf())),L"CreateCommandAllocator (CS) Failed", __FILEW__, __LINE__);
		ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_CSAlloc.Get(), nullptr, IID_PPV_ARGS(m_CSList.ReleaseAndGetAddressOf())), L"CreateCommandList (CS) Failed", __FILEW__, __LINE__);
		D3D12_COMMAND_QUEUE_DESC csQDesc = {};
		csQDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		csQDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		ThrowIfFailed(m_device->CreateCommandQueue(&csQDesc, IID_PPV_ARGS(m_CSQueue.ReleaseAndGetAddressOf())), L"CreateCommandQueue (CS) Failed", __FILEW__, __LINE__);
		ThrowIfFailed(m_device->CreateFence(m_CSFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_CSFence.ReleaseAndGetAddressOf())), L"CreateFence(CS) Failed", __FILEW__, __LINE__);
		
		//生成直後のコマンドアロケータは記録中になっているらしい
		//一旦Closeしないと次のリセットが出来ずOpenCommandListCS()がエラーを返すので、一旦こうして閉じとく
		ExecuteCommandListCS();
	}

	//ウィンドウのサイズ変更の都度呼ぶものらしい
	void DXR::CreateWindowSizeDependentResources()
	{
		WaitForGPU();

		// スワップチェーンに結びつけられたリソースの解放とフェンス値の更新
		for (UINT n = 0; n < m_BBcount; n++)
		{
			m_backBuffer[n].Reset();
			m_fenceValues[n] = m_fenceValues[m_backBufferIndex];
		}

		if (m_swapChain) {
			//既にスワップチェーンがあるならリサイズする
			HRESULT hr = m_swapChain->ResizeBuffers(m_BBcount, m_width, m_height, m_BBformat, 0);
			if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
				DEB(L"Device Lost on ResizeBuffers: Reason code 0x%08X", hr);
				HandleDeviceLost();
				return;
			}
		} else {
			// スワップチェーンのディスクリプタ
			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
			swapChainDesc.Width = m_width;
			swapChainDesc.Height = m_height;
			swapChainDesc.Format = m_BBformat;
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.BufferCount = m_BBcount;
			swapChainDesc.SampleDesc.Count = 1;
			swapChainDesc.SampleDesc.Quality = 0;
			swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
			swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			swapChainDesc.Flags = 0;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = { 0 };
			fsSwapChainDesc.Windowed = TRUE;

			//ウィンドウにスワップチェーンを作る
			ComPtr<IDXGISwapChain1> swapChain;
			ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_cmdQueue.Get(), m_hWnd, &swapChainDesc, &fsSwapChainDesc, nullptr, &swapChain), L"CreateSwapChainForHwnd Failed", __FILEW__, __LINE__);
			ThrowIfFailed(swapChain.As(&m_swapChain), L"swapChain.As Failed", __FILEW__, __LINE__);

		}

		//m_backBufferの作成とRTVの作成
		for (UINT n = 0; n < m_BBcount; n++) {
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(m_backBuffer[n].ReleaseAndGetAddressOf())), L"GetBuffer Failed", __FILEW__, __LINE__);
			m_backBuffer[n]->SetName(std::format(L"Yorozu backbuffer {}", n).c_str());

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = m_BBformat;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		}

		m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

		//バックバッファのためのディスクリプタヒープ
		D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
		dhd.NumDescriptors = 2;
		dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(m_backBufferDH.ReleaseAndGetAddressOf())), L"CreateDescriptorHeap failed", __FILEW__, __LINE__);
		auto handle = m_backBufferDH->GetCPUDescriptorHandleForHeapStart();
		for (int i = 0; i < m_BBcount; i++) {
			m_backBufferCPUHandle[i] = handle;
			m_device->CreateRenderTargetView(m_backBuffer[i].Get(), nullptr, handle);
			handle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

	}

	void DXR::WaitForGPU() noexcept
	{
		if (m_cmdQueue && m_fence && m_fenceEvent.IsValid())
		{
			// Schedule a Signal command in the GPU queue.
			UINT64 fenceValue = m_fenceValues[m_backBufferIndex];
			if (SUCCEEDED(m_cmdQueue->Signal(m_fence.Get(), fenceValue)))
			{
				// Wait until the Signal has been processed.
				if (SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent.Get())))
				{
					WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);

					// Increment the fence value for the current frame.
					m_fenceValues[m_backBufferIndex]++;
				}
			}
		}
	}

	void DXR::MoveToNextFrame()
	{
		// Schedule a Signal command in the queue.
		const UINT64 currentFenceValue = m_fenceValues[m_backBufferIndex];
		ThrowIfFailed(m_cmdQueue->Signal(m_fence.Get(), currentFenceValue), L"m_cmdQueue->Signal Failed", __FILEW__, __LINE__);

		// Update the back buffer index.
		m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

		// If the next frame is not ready to be rendered yet, wait until it is ready.
		if (m_fence->GetCompletedValue() < m_fenceValues[m_backBufferIndex])
		{
			ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_backBufferIndex], m_fenceEvent.Get()), L"SetEventOnCompletion Failed", __FILEW__, __LINE__);
			WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);
		}

		// Set the fence value for the next frame.
		m_fenceValues[m_backBufferIndex] = currentFenceValue + 1;
	}

	void DXR::OpenCommandListCS()
	{
		ThrowIfFailed(CommandAllocCS()->Reset(), L"CommandAllocCS()->Reset() failed", __FILEW__, __LINE__);
		ThrowIfFailed(CommandListCS()->Reset(CommandAllocCS().Get(), nullptr), L"CommandListCS()->Reset failed", __FILEW__, __LINE__);
	}

	void DXR::ExecuteCommandListCS(bool waitfor)
	{
		ThrowIfFailed(CommandListCS()->Close(), L"CommandListCS()->Close failed", __FILEW__, __LINE__);
		//コマンドの実行
		ID3D12CommandList* com[] = { CommandListCS().Get() };
		CommandQueueCS()->ExecuteCommandLists(1, com);
		
		if (waitfor)
			WaitForCS();
	}

	void DXR::WaitForCS() noexcept
	{
		if (SUCCEEDED(CommandQueueCS()->Signal(FenceCS().Get(), ++m_CSFenceValue))) {
			if (m_CSFence->GetCompletedValue() != m_CSFenceValue) {
				//イベントハンドルの取得とセット
				auto event = CreateEvent(nullptr, false, false, nullptr);
				if (event) {
					if (SUCCEEDED(m_CSFence->SetEventOnCompletion(m_CSFenceValue, event))) {
						//待つ
						WaitForSingleObject(event, INFINITE);
						//閉じる
						CloseHandle(event);
					}
				}
			}
		}
	}

	void DXR::HandleDeviceLost()
	{
		try {
			WaitForGPU();
			WaitForCS();

			/* TODO : ここにデバイスロストハンドラの呼び出し
			if (m_deviceNotify)
			{
				m_deviceNotify->OnDeviceLost();
			}*/

			for (UINT n = 0; n < m_BBcount; n++)
			{
				m_cmdAlloc[n].Reset();
				m_backBuffer[n].Reset();
			}

			m_cmdQueue.Reset();
			m_cmdList.Reset();
			m_fence.Reset();
			//m_rtvDescriptorHeap.Reset();
			//m_dsvDescriptorHeap.Reset();
			m_swapChain.Reset();
			m_device.Reset();
			m_factory.Reset();
			m_adapter.Reset();

		}
		catch (std::runtime_error) {
			//特に何もせず、待つ
		} 
	}

	void DXR::OpenCommandList()
	{
		// Prepare the command list and render target for rendering.
		// Reset command list and allocator.
		ThrowIfFailed(m_cmdAlloc[m_backBufferIndex]->Reset(), L"m_cmdAlloc[m_backBufferIndex]->Reset Failed", __FILEW__, __LINE__);
		ThrowIfFailed(m_cmdList->Reset(m_cmdAlloc[m_backBufferIndex].Get(), nullptr), L"m_cmdList->Reset Failed", __FILEW__, __LINE__);
	}

	void DXR::Present(UINT syncInterval, UINT flags)
	{
		// Present the contents of the swap chain to the screen.
		HRESULT hr;
		// The first argument instructs DXGI to block until VSync, putting the application
		// to sleep until the next VSync. This ensures we don't waste any cycles rendering
		// frames that will never be displayed to the screen.
		hr = m_swapChain->Present(syncInterval, flags);

		// If the device was reset we must completely reinitialize the renderer.
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
			//クラッシュレポート
			DEB(L"Device Lost on Present: Reason code 0x%08X\n", hr);
			D3D12DREDLog(m_device.Get());
			HandleDeviceLost();
		} else {
			ThrowIfFailed(hr,L"m_swapChain->Present Failed", __FILEW__, __LINE__);
			MoveToNextFrame();
		}
	}

	void DXR::ExecuteCommandList(bool waitfor)
	{
		// Send the command list off to the GPU for processing.
		ThrowIfFailed(m_cmdList->Close(), L"m_cmdList->Close Failed", __FILEW__, __LINE__);
		ID3D12CommandList* commandLists[] = { m_cmdList.Get() };
		m_cmdQueue->ExecuteCommandLists(1, commandLists);
		
		if (waitfor)
			WaitForGPU();
	}

	void DXR::CopyResource(ID3D12Resource* dest, D3D12_RESOURCE_STATES destState, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState)
	{
		D3D12_RESOURCE_BARRIER bar[2];
		bar[0] = CD3DX12_RESOURCE_BARRIER::Transition(dest, destState, D3D12_RESOURCE_STATE_COPY_DEST);
		bar[1] = CD3DX12_RESOURCE_BARRIER::Transition(src, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_cmdList->ResourceBarrier(2, bar);

		m_cmdList->CopyResource(dest, src);

		bar[0] = CD3DX12_RESOURCE_BARRIER::Transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, destState);
		bar[1] = CD3DX12_RESOURCE_BARRIER::Transition(src, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState);

		m_cmdList->ResourceBarrier(2, bar);
	}

	void DXR::CopyResource(Res& dest, Res& src)
	{
		std::vector<D3D12_RESOURCE_BARRIER> bar;
		AddBarrier(&dest, bar, D3D12_RESOURCE_STATE_COPY_DEST);
		AddBarrier(&src, bar, D3D12_RESOURCE_STATE_COPY_SOURCE);
		if (bar.size())
			m_cmdList->ResourceBarrier(bar.size(), bar.data());

		m_cmdList->CopyResource(dest.res.Get(), src.res.Get());
	}

	void DXR::CopyResourceToBB(Tex2D& src, int index)
	{
		D3D12_RESOURCE_BARRIER barrier[2] = {};
		ID3D12Resource* backbuffer;
		if (index < 0)
			backbuffer = m_backBuffer[m_backBufferIndex].Get();
		else
			backbuffer = m_backBuffer[index].Get();

		CopyResource(backbuffer, D3D12_RESOURCE_STATE_PRESENT, src.res.Get(), src.state);
	}

	void DXR::CopyResourceFromBB(Tex2D& dest, int index)
	{
		ID3D12Resource* backbuffer;
		int prevBB = (m_backBufferIndex >= 1) ? m_backBufferIndex - 1 : m_BBcount - 1;

		if (index >= 0)
			backbuffer = m_backBuffer[index].Get();
		else
			backbuffer = m_backBuffer[prevBB].Get();

		CopyResource(dest.res.Get(), dest.state, backbuffer, D3D12_RESOURCE_STATE_PRESENT);

	}

	void DXR::Download(void* dest, Buf& src, size_t destStartInBytes, size_t srcStartInBytes, size_t countInBytes)
	{
		auto desc = src.desc();

		//範囲チェック
		if (srcStartInBytes >= desc.Width)
			return;

		//どこまでコピーする？
		size_t end = (countInBytes != std::wstring::npos) ? min(srcStartInBytes + countInBytes, desc.Width) : desc.Width;
		size_t destIdx = destStartInBytes;

		OpenCommandList();
		if (src.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
			auto bar = CD3DX12_RESOURCE_BARRIER::Transition(src.res.Get(), src.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
			src.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
			m_cmdList->ResourceBarrier(1, &bar);
		}
		ExecuteCommandList();

		void* pData;
		m_downloadBuf.res->Map(0, nullptr, &pData);
		for (int i = srcStartInBytes; i < end; i += m_UDSize) {
			size_t num = min(end - i, m_UDSize);
			
			OpenCommandList();
			m_cmdList->CopyBufferRegion(m_downloadBuf.res.Get(), 0, src.res.Get(), i, num);
			ExecuteCommandList();

			memcpy(((::byte*)dest) + destIdx, pData, num);
			destIdx += m_UDSize;
		}
		m_downloadBuf.res->Unmap(0, nullptr);
	}

	void DXR::Upload(Buf& dest, const void* src, size_t destStartInBytes, size_t srcStartInBytes, size_t countInBytes)
	{
		auto desc = dest.desc();

		//範囲チェック
		if (destStartInBytes >= desc.Width)
			return;

		//どこまでコピーする？
		size_t end = (countInBytes != std::wstring::npos) ? min(destStartInBytes + countInBytes, desc.Width) : desc.Width;
		size_t srcIdx = srcStartInBytes;

		OpenCommandList();
		if (dest.state != D3D12_RESOURCE_STATE_COPY_DEST) {
			auto bar = CD3DX12_RESOURCE_BARRIER::Transition(dest.res.Get(), dest.state, D3D12_RESOURCE_STATE_COPY_DEST);
			m_cmdList->ResourceBarrier(1, &bar);
			dest.state = D3D12_RESOURCE_STATE_COPY_DEST;
		}
		ExecuteCommandList();

		void* pData;
		m_uploadBuf.res->Map(0, nullptr, &pData);

		for (int i = destStartInBytes; i < end; i += m_UDSize) {
			size_t num = min(end - i, m_UDSize);

			auto range = D3D12_RANGE{ 0, num };
			memcpy(pData, ((::byte*)src) + srcIdx, num);

			OpenCommandList();
			m_cmdList->CopyBufferRegion(dest.res.Get(), i, m_uploadBuf.res.Get(), 0, num);
			ExecuteCommandList();

			srcIdx += m_UDSize;
		}
		m_uploadBuf.res->Unmap(0, nullptr);

	}

	void DXR::Download(void* dest, Tex2D& src, int subResource, int mip, size_t destX, size_t destY, const D3D12_BOX* srcbox)
	{
		auto desc = src.desc();
		desc.Width = desc.Width >> mip;
		desc.Height = desc.Height >> mip;

		D3D12_BOX box = (srcbox == nullptr) ? CD3DX12_BOX(0, 0, desc.Width, desc.Height) : *srcbox;
		box.right = min(box.right, desc.Width);
		box.bottom = min(box.bottom, desc.Height);

		size_t t = m_UDTileSize;
		size_t sw = box.right - box.left, sh = box.bottom - box.top;			//ソース矩形のサイズ
		size_t W = min(destX+sw,desc.Width), H = min(destY+sh, desc.Height);	//書き込まれる右下位置

		size_t bypp = DirectX::BitsPerPixel(desc.Format) >> 3;

		OpenCommandList();
		if (src.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
			auto bar = CD3DX12_RESOURCE_BARRIER::Transition(src.res.Get(), src.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
			src.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
			m_cmdList->ResourceBarrier(1, &bar);
		}
		ExecuteCommandList();

		::byte* pData;
		m_downloadBuf.res->Map(0, nullptr, (void**)&pData);
		int sx, sy = box.top;
		for (size_t y = destY; y < H; y+=t) {
			int nRow = min(H - y, t);	//縦方向に何ピクセルコピーするか
			sx = box.left;
			for (size_t x = destX; x < W; x+=t) {
				int nCol = min(W - x, t);	//横方向に何ピクセルコピーするか

				size_t rowPitch = Align(nCol * bypp, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = { 0, CD3DX12_SUBRESOURCE_FOOTPRINT(desc.Format, nCol, nRow, 1, rowPitch) };
				auto srcLoc = CD3DX12_TEXTURE_COPY_LOCATION(src.res.Get(), subResource);
				auto destLoc = CD3DX12_TEXTURE_COPY_LOCATION(m_downloadBuf.res.Get(), footprint);
				auto srcBox = CD3DX12_BOX(sx, sy, sx+nCol, sy+nRow);
				OpenCommandList();
				m_cmdList->CopyTextureRegion(&destLoc, 0, 0, 0, &srcLoc, &srcBox);
				ExecuteCommandList();

				for (int i = 0; i < nRow; i++) {
					memcpy( ((::byte*)dest) + ((y + i) * desc.Width + x) * bypp, pData + i * rowPitch, nCol * bypp);
				}

				sx += t;
			}
			sy += t;
		}
		m_downloadBuf.res->Unmap(0, nullptr);
	}


	void DXR::Upload(Tex2D& dest, const void* src, int subResource, int mip, size_t destX, size_t destY, const D3D12_BOX* srcbox)
	{
		auto desc = dest.desc();
		desc.Width = desc.Width >> mip;
		desc.Height= desc.Height>> mip;

		D3D12_BOX box = (srcbox == nullptr) ? CD3DX12_BOX(0, 0, desc.Width, desc.Height) : *srcbox;

		size_t sw = box.right - box.left, sh = box.bottom - box.top;			//ソース矩形のサイズ
		size_t t = m_UDTileSize;
		size_t W = min(destX + sw, desc.Width), H = min(destY + sh, desc.Height);	//書き込まれる右下位置

		size_t bypp = DirectX::BitsPerPixel(desc.Format) >> 3;

		OpenCommandList();
		if (dest.state != D3D12_RESOURCE_STATE_COPY_DEST) {
			auto bar = CD3DX12_RESOURCE_BARRIER::Transition(dest.res.Get(), dest.state, D3D12_RESOURCE_STATE_COPY_DEST);
			dest.state = D3D12_RESOURCE_STATE_COPY_DEST;
			m_cmdList->ResourceBarrier(1, &bar);
		}
		ExecuteCommandList();

		::byte* pData;

		m_uploadBuf.res->Map(0, nullptr, (void**)&pData);
		for (size_t y = destY; y < H; y += t) {
			int nRow = min(H - y, t);	//縦方向に何ピクセルコピーするか
			for (size_t x = destX; x < W; x += t) {
				int nCol = min(W - x, t);	//横方向に何ピクセルコピーするか

				size_t rowPitch = Align(nCol * bypp, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
				for (int i = 0; i < nRow; i++) {
					memcpy(pData + i * rowPitch, ((::byte*)src) + ((y + i) * desc.Width + x) * bypp, nCol * bypp);
				}

				D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = { 0, CD3DX12_SUBRESOURCE_FOOTPRINT(desc.Format, nCol, nRow, 1, rowPitch)};
				auto destLoc = CD3DX12_TEXTURE_COPY_LOCATION(dest.res.Get(), subResource);
				auto srcLoc = CD3DX12_TEXTURE_COPY_LOCATION(m_uploadBuf.res.Get(), footprint);
				auto srcBox = CD3DX12_BOX(0, 0, nCol, nRow);
				OpenCommandList();
				m_cmdList->CopyTextureRegion(&destLoc, x, y, 0, &srcLoc, &srcBox);
				ExecuteCommandList();

			}
		}
		m_uploadBuf.res->Unmap(0, nullptr);
	}


	void DXR::SaveTex2DToFile(Tex2D& tex, const wchar_t* filename)
	{
		std::wstring fstr = filename;
		auto idx = fstr.rfind(L".");
		std::wstring ext = fstr.substr(idx, 4);
		std::transform(ext.begin(), ext.end(), ext.begin(), tolower);

		auto texdesc = tex.desc();
		BYTE* pixels;

		DirectX::Image img = {};
		img.width = texdesc.Width;
		img.height = texdesc.Height;
		img.format = texdesc.Format;
		img.rowPitch = texdesc.Width * DirectX::BitsPerPixel(texdesc.Format) / 8;
		img.slicePitch = img.rowPitch * texdesc.Height;
		pixels = new BYTE[img.slicePitch];

		Download(pixels, tex);

		img.pixels = (uint8_t*)pixels;

		if (ext == L".dds") {
			if (texdesc.MipLevels > 1) {
				DirectX::TexMetadata meta = {};
				meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
				meta.width = texdesc.Width;
				meta.height = texdesc.Height;
				meta.depth = 1;
				meta.format = texdesc.Format;
				meta.mipLevels = texdesc.MipLevels;
				meta.arraySize = 1;
				DirectX::ScratchImage simg;
				simg.Initialize2D(meta.format, meta.width, meta.height, 1, meta.mipLevels);
				for (int i = 0; i < meta.mipLevels; i++) {
					auto im = simg.GetImage(i, 0, 0);
					if (i == 0)
						memcpy(im->pixels, pixels, im->slicePitch);
					else {
						std::vector<BYTE>buf(im->slicePitch);
						Download(buf.data(), tex, i, i);
						memcpy(im->pixels, buf.data(), im->slicePitch);
					}
				}
				ThrowIfFailed(DirectX::SaveToDDSFile(simg.GetImages(), meta.mipLevels, meta, DirectX::DDS_FLAGS_NONE, filename),
					std::format(L"SaveTex2DFile Failed {}", filename), __FILEW__, __LINE__);
			} else {
				ThrowIfFailed(SaveToDDSFile(img, DirectX::DDS_FLAGS_NONE, filename), std::format(L"SaveTex2DToFile Failed {}", filename), __FILEW__, __LINE__);
			}
		}  else if (ext == L".png")
			ThrowIfFailed(SaveToWICFile(img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), filename, nullptr),
				std::format(L"SaveTex2DToFile Failed {}", filename), __FILEW__, __LINE__);
		else if (ext == L".jpg" || ext==L".jpeg" || ext==L".jfif")
			ThrowIfFailed(SaveToWICFile(img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_JPEG), filename),
				std::format(L"SaveTex2DToFile Failed {}", filename), __FILEW__, __LINE__);
		else
			ThrowIfFailed(E_FAIL, L"DXR::SaveTex2DToFile() failed ... supports only .dds/.png/.jpg format", __FILEW__, __LINE__);

		delete[] pixels;
		return;
	}

	void DXR::Snapshot(const wchar_t* filename)
	{
		//バックバッファの中身をコピーするためのテクスチャを作る
		Tex2D SSTex = CreateTex2D(m_width, m_height, m_BBformat);

		//CopyResourceでバックバッファの中身をSSBufへコピー
		OpenCommandList();
		CopyResourceFromBB(SSTex);
		ExecuteCommandList();

		//保存
		SaveTex2DToFile(SSTex, filename);
	}

	Tex2D DXR::GenerateMipmap(Tex2D& tex, bool convertToLinear)
	{
		auto desc = tex.desc();

		//処理対象のmipレベルを入れるためのコンスタントバッファ
		auto cb = CreateCB(nullptr, sizeof(int));
		int* miplevel = (int*)cb.pData;
		
		//MIPMAP出力用
		auto miptex = CreateRT2D(desc.Width, desc.Height, desc.Format, 0);
		//作業用
		auto lowertex = CreateRT2D(desc.Width>>1, desc.Height>>1, desc.Format, 0);

		//サンプラ
		auto linearSamp = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		
		//作成用シェーダ、キャッシュにあればそれを読み込み、無ければコンパイルする
		Shader vs,psCopy,psShrink,psLinear;
		try {
			vs = m_shaderCache.at(L"MipMapGenShader_VS");
			psCopy = m_shaderCache.at(L"MipMapGenShader_PSCopy");
			psLinear = m_shaderCache.at(L"MipMapGenShader_PSLinear");
			psShrink = m_shaderCache.at(L"MipMapGenShader_PSShrink");
		} catch(std::out_of_range ex) {
			vs = CompileShaderMemory(MipmapGenShader, sizeof(MipmapGenShader), nullptr, L"MipMapGenShader", L"VS", L"vs_6_1");
			psCopy = CompileShaderMemory(MipmapGenShader, sizeof(MipmapGenShader), nullptr, L"MipMapGenShader", L"PSCopy", L"ps_6_1");
			psLinear = CompileShaderMemory(MipmapGenShader, sizeof(MipmapGenShader), nullptr, L"MipMapGenShader", L"PSLinear", L"ps_6_1");
			psShrink = CompileShaderMemory(MipmapGenShader, sizeof(MipmapGenShader), nullptr, L"MipMapGenShader", L"PSShrink", L"ps_6_1");
			m_shaderCache[L"MipMapGenShader_VS"] = vs;
			m_shaderCache[L"MipMapGenShader_PSCopy"] = psCopy;
			m_shaderCache[L"MipMapGenShader_PSLinear"] = psLinear;
			m_shaderCache[L"MipMapGenShader_PSShrink"] = psShrink;
		}

		for (int i = 0; i < miptex.desc().MipLevels; i++) {
			*miplevel = i;

			//縮小した画像をmiptexのi番目のmipレベルに格納する。レベル0の場合は原画を入れる
			Pass pcopy(this);
			pcopy.Samplers.push_back(linearSamp);
			pcopy.RTV.push_back({ &miptex, {}, i });
			pcopy.CBV.push_back(&cb);

			if (i == 0)
				pcopy.SRV[0].push_back(&tex);
			else
				pcopy.SRV[0].push_back(&lowertex);

			if (i==0 && convertToLinear)
				pcopy.PostProcessPass(vs, psLinear);
			else
				pcopy.PostProcessPass(vs, psCopy);

			OpenCommandList();
			pcopy.Render();
			ExecuteCommandList();

			//miptexに最後に格納したmipレベルを半分のサイズにしてlowertexのmipレベルに入れる
			Pass pshrink(this);
			if (i < miptex.desc().MipLevels - 1) {
				pshrink.Samplers.push_back(linearSamp);
				pshrink.CBV.push_back(&cb);
				pshrink.SRV[0].push_back(&miptex);
				pshrink.RTV.push_back({ &lowertex, {}, i });
				pshrink.PostProcessPass(vs, psShrink);

				OpenCommandList();
				pshrink.Render(tex.desc().Width >> (i + 1), tex.desc().Height >> (i + 1));
				ExecuteCommandList();
			}

		}

		miptex.SetName(tex.Name().c_str());
		return miptex;
	}

	Tex2D DXR::ToLinear(Tex2D& src)
	{
		auto desc = src.desc();
		Tex2D ret = CreateRT2D(desc.Width, desc.Height, desc.Format, desc.MipLevels);

		auto cb = CreateCB(nullptr, sizeof(int));
		int& miplevel = *(int*)cb.pData;
		auto pointSamp = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		//作成用シェーダ、キャッシュにあればそれを読み込み、無ければコンパイルする
		Shader vs, psLinear;
		try {
			vs = m_shaderCache.at(L"ToLinearShader_VS");
			psLinear = m_shaderCache.at(L"ToLinearShader_PS");
		} catch (std::out_of_range ex) {
			vs = CompileShaderMemory(ToLinearShader, sizeof(ToLinearShader), nullptr, L"ToLinearShader", L"VS", L"vs_6_1");
			psLinear = CompileShaderMemory(ToLinearShader, sizeof(ToLinearShader), nullptr, L"ToLinearShader", L"PSLinear", L"ps_6_1");
			m_shaderCache[L"ToLinearShader_VS"] = vs;
			m_shaderCache[L"ToLinearShader_PS"] = psLinear;
		}

		for (int i = 0; i < desc.MipLevels; i++) {
			Pass pLinear(this);
			pLinear.Samplers.push_back(pointSamp);
			pLinear.CBV.push_back(&cb);
			pLinear.SRV[0].push_back(&src);
			pLinear.RTV.push_back({ &ret, {}, i });
			pLinear.PostProcessPass(vs, psLinear);

			OpenCommandList();
			pLinear.Render();
			ExecuteCommandList();
			miplevel++;
		}
		return ret;
	}


	LOGFONTW LogFont(const wchar_t* fontname, int heightInPixels, int weight, bool italic, bool underline, bool strike)
	{
		LOGFONTW lf = {0};
		wcscpy_s(lf.lfFaceName, fontname);
		lf.lfHeight = -heightInPixels;
		lf.lfWeight = weight;
		lf.lfItalic = italic;
		lf.lfUnderline = underline;
		lf.lfStrikeOut = strike;
		return lf;
	}

	//strに格納された各文字を収めたグリフデータを作る
	//返り値はテキスト全体のレンダリングされるサイズ
	//glyphsがnullptrの時はサイズの取得だけやる
	POINT GetTextGlyphs(const wchar_t* str, const LOGFONTW& logfont, std::vector<Glyph>*glyphs)
	{
		int totalWidth = 0, totalHeight = 0;
		int length = wcslen(str);
		HDC hdc = CreateCompatibleDC(NULL);
		HFONT hFont = CreateFontIndirectW(&logfont);
		SelectObject(hdc, hFont);
		OUTLINETEXTMETRIC tm;
		GetOutlineTextMetrics(hdc, sizeof(tm), &tm);
		totalHeight = tm.otmTextMetrics.tmHeight;

		// 各文字のビットマップデータを取得
		for (size_t i = 0; i < length; i++)
		{
			Glyph g;
			GLYPHMETRICS gm;
			MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
			DWORD size = GetGlyphOutlineW(hdc, str[i], GGO_GRAY8_BITMAP, &gm, 0, NULL, &mat);

			g.leastHeight = g.height = gm.gmBlackBoxY;
			g.leastWidth = gm.gmBlackBoxX;
			g.glyph.resize(size);
			g.width = (int)g.glyph.size() / g.height;	//widthそのものが奇数でも得られるラスタイメージは2とか4とかの倍数になるっぽいのでラスタイメージのサイズにする
			g.left = gm.gmptGlyphOrigin.x;
			g.top = tm.otmTextMetrics.tmAscent - gm.gmptGlyphOrigin.y;
			g.cellWidth = gm.gmCellIncX;
			totalWidth += g.cellWidth;

			if (size && glyphs != nullptr) {
				GetGlyphOutlineW(hdc, str[i], GGO_GRAY8_BITMAP, &gm, size, g.glyph.data(), &mat);
				//65段階グレースケールなので0～255の範囲に直す
				for (int j = 0; j < (int)size; j++) {
					g.glyph[j] = (DWORD)max(min(g.glyph[j] * 255 / 65, 255),0);
				}
			}

			if (glyphs!=nullptr) 
				glyphs->push_back(g);
		}

		DeleteObject(hFont);
		DeleteDC(hdc);

		return POINT{ totalWidth, totalHeight };
	}

	//テキストレンダリング
	void RenderTextToMemory(::byte* dest, int width, int height, const wchar_t* str, const LOGFONTW& logfont, HA halign, VA valign, bool clear, int ofsX, int ofsY)
	{
		std::vector<Glyph> glyphs;
		POINT size = GetTextGlyphs(str, logfont, &glyphs);
		int totalWidth = size.x;
		int totalHeight = size.y;
		int length = glyphs.size();

		int W = width;
		int H = height;

		//一塊のビットマップにつなげる
		//文字ぞろえに伴って変わる左上余白量
		int padX = 0, padY = 0;
		if (halign == HA::center)
			padX = (W - totalWidth) / 2;
		else if (halign == HA::right)
			padX = W - totalWidth;
		if (valign == VA::middle)
			padY = (H - totalHeight) / 2;
		else if (valign == VA::bottom)
			padY = H - totalHeight;

		padX += ofsX;
		padY += ofsY;
		
		//クリア
		if (clear)
			ZeroMemory(dest, W * H);

		int left = padX;

		for (int i = 0; i < length; i++) {
			if (glyphs[i].glyph.size()) {
				for (int y = 0; y < glyphs[i].height; y++) {
					int dy = y + glyphs[i].top + padY;
					if (dy < 0) continue;
					if (dy >= H) break;
					for (int x = 0; x < glyphs[i].width; x++) {
						int dx = x + left + glyphs[i].left;
						if (dx < 0) continue;
						if (dx >= W) break;
						int sp = y * glyphs[i].width + x;
						int dp = dy * W + dx;
						::byte c = glyphs[i].glyph[sp];
						int p = (dest[dp] * (255 - c) + 255 * c) / 255;
						dest[dp] = (BYTE)max(0, min(p, 255));
					}
				}
			}
			left += glyphs[i].cellWidth;
			if (left >= W)
				break;
		}
	}

	POINT RenderTextToMemoryMultiLine(::byte* dest, int width, int height, const wchar_t* str, const LOGFONTW& logfont, HA halign, VA valign, bool clear, int ofsX, int ofsY)
	{
		int len = wcslen(str);
		std::wstring line = L"";	//注目している一行
		std::vector<std::wstring>lines;	//全体
		int iLine = 0;

		//まず、linesに文字列を行ごとに格納
		for (int i = 0; i < len; i++) {
			if (str[i] == '\r')
				continue;
			if (str[i] == '\n') {
				if (line == L"") {
					line = L" ";	//空行の場合、改行分は空けたいのでスペース1つと変換する
				}
				lines.push_back(line);
				line = L"";
				iLine++;
				continue;
			}
			line += str[i];
		}

		if (line != L"")
			lines.push_back(line);

		//各行の文字列の幅と高さを得る
		int nLine = iLine + 1;
		int maxW = 0;	//最大幅
		int totalH = 0;	//全体の高さ
		std::vector<POINT>exts(nLine);	//各行の幅と高さ
		for (int i = 0; i < nLine; i++) {
			exts[i] = GetTextGlyphs(lines[i].c_str(), logfont, nullptr);
			maxW = max(maxW, exts[i].x);
			totalH += exts[i].y;
		}

		POINT ret;
		ret.x = maxW;
		ret.y = totalH;

		if (dest == nullptr) {
			return ret;
		}

		//各行の描画位置を決定
		std::vector<POINT>ofs(nLine);	//各行の位置
		int ox = 0, oy = 0;
		if (valign == VA::middle)
			oy = (height - totalH) / 2;
		else if (valign == VA::bottom)
			oy = height - totalH;

		for (int i = 0; i < nLine; i++) {
			if (halign == HA::center)
				ox = (width - exts[i].x) / 2;
			else if (halign == HA::right)
				ox = width - exts[i].x;
			ofs[i].x = ox;
			ofs[i].y = oy;
			oy += exts[i].y;
		}

		//描画
		if (clear)
			ZeroMemory(dest, width * height);

		for (int i = 0; i < nLine; i++) {
			RenderTextToMemory(dest, width, height, lines[i].c_str(), logfont, HA::left, VA::top, false, ofs[i].x, ofs[i].y);
		}

		return ret;
	}

	/*
	Tex2D YorozuDXR::CreateTextTexture(int width, int height, HA halign, VA valign, const wchar_t* str, const LOGFONTW& logfont, const wchar_t* name)
	{
		Tex2D ret;

		POINT size = { width, height };
		int pixelCount = size.x * size.y;
		byte* buf = new byte[pixelCount];

		//RenderTextToMemory(buf, size.x, size.y, str, logfont, halign, valign);
		RenderTextToMemoryMultiLine(buf, size.x, size.y, str, logfont, halign, valign);

		ret = CreateTex2D(size.x, size.y, DXGI_FORMAT_R8_UNORM, name);

		Upload(ret, buf);

		delete[] buf;

		return ret;
	}

	Tex2D YorozuDXR::CreateTextTexture(const wchar_t* str, const LOGFONTW& logfont, HA halign, const wchar_t* name)
	{
		//POINT size = GetTextGlyphs(str, logfont, nullptr);
		//return CreateTextTexture(size.x, size.y, taLeft, taTop, str, logfont, name);
		POINT size = RenderTextToMemoryMultiLine(nullptr, 0, 0, str, logfont, halign, VA::top);
		return CreateTextTexture(size.x, size.y, halign, VA::top, str, logfont, name);
	}
	*/

	void DXR::Resize(int width, int height)
	{
		m_width = width;
		m_height = height;
		CreateWindowSizeDependentResources();
	}

	DXR::DXR(HWND hWnd, bool raytracing, D3D_FEATURE_LEVEL featureLevel)
	{
		LOG(L"★★★ Welcome to Yorozu DXR! ★★★");
		m_raytracingSupport = raytracing;

		m_hWnd = hWnd;
		RECT rc = {};
		GetClientRect(hWnd, &rc);
		m_width = rc.right - rc.left;
		m_height = rc.bottom - rc.top;

		//必須デバイス作成
		CreateDevice(featureLevel);
		CreateWindowSizeDependentResources();

		//アップロード・ダウンロードバッファ
		m_uploadBuf = CreateBufCPU(nullptr, m_UDSize, 1, true, false);
		m_uploadBuf.SetName(L"Yorozu upload buffer");
		m_downloadBuf = CreateBufCPU(nullptr, m_UDSize, 1, false, true);
		m_downloadBuf.SetName(L"Yorozu download buffer");

		//ポストプロセス枠
		PostProcessVertex ppv[4] = { {{-1,-1,0.1}, {0,1}}, {{ -1,1,0.1}, {0,0}}, {{1,-1,0.1}, {1,1}}, {{1,1,0.1}, {1,0}} };
		m_PostProcessVB = CreateBuf(ppv, sizeof(PostProcessVertex), 4);
		m_PostProcessVB.SetName(L"Yorozu PostProcessVB");

		//ステート変更
		std::vector<D3D12_RESOURCE_BARRIER> bar;
		AddBarrier(&m_PostProcessVB, bar, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		AddBarrier(&m_uploadBuf, bar, D3D12_RESOURCE_STATE_GENERIC_READ);	//23夜 COPY_SOURCEから変更
		AddBarrier(&m_downloadBuf, bar, D3D12_RESOURCE_STATE_COPY_DEST);
		OpenCommandList();
		m_cmdList->ResourceBarrier(bar.size(), bar.data());
		ExecuteCommandList();
	}

	DXR::~DXR()
	{
		LOG(L"★★★ Yorozu DXR closed ★★★");
	}


	/**************************************************************************************
	/ App 
	**************************************************************************************/

	//シングルトン、ですから
	bool WMSizeInvoked = false;
	MSG WMSize = {};
	bool WMDropFilesInvoked = false;
	MSG WMDropFiles = {};

	LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// ボーダレスウィンドウのメッセージハンドラ
		if (msg == WM_NCCREATE) {
			auto userdata = reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams;
			// store window instance pointer in window user data
			::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
		}
		if (auto window_ptr = reinterpret_cast<App*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA)))
		{
			auto& window = *window_ptr;

			switch (msg) {
				case WM_NCCALCSIZE: {
					if (wparam == TRUE && window.borderless) {
						auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
						adjust_maximized_client_rect(hWnd, params.rgrc[0]);
						return 0;
					}
					break;
				}
				case WM_NCHITTEST: {
					// When we have no border or title bar, we need to perform our
					// own hit testing to allow resizing and moving.
					if (window.borderless) {
					    return window.hit_test(POINT{
					        GET_X_LPARAM(lparam),
					        GET_Y_LPARAM(lparam)
					    });
					}
					break;
				}
	            case WM_NCACTIVATE: {
					if (!composition_enabled()) {
					    // Prevents window frame reappearing on window activation
					    // in "basic" theme, where no aero shadow is present.
					    return 1;
					}
					break;
				}
				case WM_KEYDOWN:
				case WM_SYSKEYDOWN: {
				    switch (wparam) {
						case VK_F8 : { window.borderless_drag = !window.borderless_drag;        return 0; }
						//case VK_F9 : { window.borderless_resize = !window.borderless_resize;    return 0; }	// ボーダレス中は基本的に動画レンダリング中なので, 少なくともリサイズは認めない.
						case VK_F10: { window.set_borderless(!window.borderless);               return 0; }
						case VK_F11: { window.set_borderless_shadow(!window.borderless_shadow); return 0; }
				    }
				    break;
				}
			}
		}
		// imguiのメッセージハンドラ
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wparam, lparam))
			return true;

		//その他
		if (msg == WM_CLOSE) {
			DestroyWindow(hWnd);
        	return 0;
		}
		if (msg == WM_DESTROY) {
			PostQuitMessage(0);
			return 0;
		}
		if (msg == WM_SIZE) {
			WMSizeInvoked = true;
			WMSize.message = msg;
			WMSize.wParam = wparam;
			WMSize.lParam = lparam;
		}
		if (msg == WM_DROPFILES) {
			WMDropFilesInvoked = true;
			WMDropFiles.message = msg;
			WMDropFiles.wParam = wparam;
			WMDropFiles.lParam = lparam;
		}
		return DefWindowProc(hWnd, msg, wparam, lparam);
	}

	App::App(const wchar_t* title, int width, int height, HINSTANCE hInstance, DWORD dwStyle)
	{
		ThrowIfFailed(CoInitializeEx(0, COINITBASE_MULTITHREADED), L"CoInitializeEx failed", __FILEW__, __LINE__);

		WNDCLASSEX wc = {};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = (WNDPROC)WndProc;
		wc.lpszClassName = title;
		wc.hInstance = GetModuleHandle(nullptr);
		RegisterClassEx(&wc);
		m_windowClass = wc;

		RECT rc = { 0,0, width,height };
		dwStyle |= static_cast<DWORD>(Style::aero_borderless);
		//ボーダレスウィンドウにするので、AdjustWindowRectは実施しない
		//AdjustWindowRect(&rc, dwStyle, false);
		m_hWnd = CreateWindowExW(0, wc.lpszClassName, title, dwStyle,
			CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
			nullptr, nullptr, wc.hInstance,this);
		
		// アイコンの読み込み
		HICON hIcon = static_cast<HICON>(::LoadImage(hInstance,
					MAKEINTRESOURCE(IDI_ICON1),
					IMAGE_ICON,
					48, 48,    // icon size to load
					LR_DEFAULTCOLOR));

	    // ウィンドウにアイコンを設定
	    SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
	    SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

		// ボーダレスウィンドウの設定
		set_borderless(borderless);
		set_borderless_shadow(borderless_shadow);
		
		ShowWindow(m_hWnd, SW_SHOW);
	}
	
	void App::set_borderless(bool enabled)
	{
		Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
		Style old_style = static_cast<Style>(::GetWindowLongPtrW(m_hWnd, GWL_STYLE));

		if (new_style != old_style) {
			borderless = enabled;

			::SetWindowLongPtrW(m_hWnd, GWL_STYLE, static_cast<LONG>(new_style));

			// when switching between borderless and windowed, restore appropriate shadow state
			set_shadow(m_hWnd, borderless_shadow && (new_style != Style::windowed));

			// redraw frame
			::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
			::ShowWindow(m_hWnd, SW_SHOW);
	    }
	}

	void App::set_borderless_shadow(bool enabled)
	{
		if (borderless) {
			borderless_shadow = enabled;
			set_shadow(m_hWnd, enabled);
		}
	}
	
	auto App::hit_test(POINT cursor) const -> LRESULT
	{
		// identify borders and corners to allow resizing the window.
		// Note: On Windows 10, windows behave differently and
		// allow resizing outside the visible window frame.
		// This implementation does not replicate that behavior.
		const POINT border{
		    ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
		    ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
		};
		RECT window;
		if (!::GetWindowRect(m_hWnd, &window)) {
		    return HTNOWHERE;
		}

		const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

		enum region_mask {
		    client = 0b0000,
		    left   = 0b0001,
		    right  = 0b0010,
		    top    = 0b0100,
		    bottom = 0b1000,
		};

		const auto result =
		    left    * (cursor.x <  (window.left   + border.x)) |
		    right   * (cursor.x >= (window.right  - border.x)) |
		    top     * (cursor.y <  (window.top    + border.y)) |
		    bottom  * (cursor.y >= (window.bottom - border.y));

		switch (result) {
		    case left          : return borderless_resize ? HTLEFT        : drag;
		    case right         : return borderless_resize ? HTRIGHT       : drag;
		    case top           : return borderless_resize ? HTTOP         : drag;
		    case bottom        : return borderless_resize ? HTBOTTOM      : drag;
		    case top | left    : return borderless_resize ? HTTOPLEFT     : drag;
		    case top | right   : return borderless_resize ? HTTOPRIGHT    : drag;
		    case bottom | left : return borderless_resize ? HTBOTTOMLEFT  : drag;
		    case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : drag;
		    case client        : return drag;
		    default            : return HTNOWHERE;
		}
	}

	bool App::MainLoop(MSG& msg, HWND hDlg) {
		bool messageAri;	//PeekMessageしてメッセージが届いてたらtrue
		msg = {};

		//DefWndProcでは消化されないメッセージ(WM_QUITとかWM_KEYDOWNとか)
		if (messageAri = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (!::IsDialogMessage(hDlg, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		if (msg.message == WM_QUIT) {
			return false;
		}

		//DefWndProcで消化されてしまうメッセージ
		if (!messageAri) {
			if (WMSizeInvoked) {
				msg = WMSize;
				WMSizeInvoked = false;
			} else if (WMDropFilesInvoked) {
				msg = WMDropFiles;
				WMDropFilesInvoked = false;
			}
		}

		return true;
	}


	App::~App()
	{
		UnregisterClass(m_windowClass.lpszClassName, m_windowClass.hInstance);
		CoUninitialize();
	}




	/**************************************************************************************
	/ Pass
	**************************************************************************************/
	Pass::Pass(DXR* yorozu)
	{
		m_dxr = yorozu;
		SRV.resize(8);		//デフォルトではspacesのサイズは8を指定されるので、増やしたくなったらてきとうにリサイズしてヨシ
		BBClearSetting();	//デフォルトのクリア設定をしとく
	}

	Pass::~Pass()
	{
		if (m_savepoint != nullptr)
			delete m_savepoint;
	}

	void Pass::Flush(bool keepSaver)
	{
		CBV.clear();
		UAV.clear();

		RTV.clear();
		DSV.clear();
		Samplers.clear();

		for (int i = 0; i < SRV.size(); i++)
			SRV[i].clear();
		
		if (!keepSaver) {
			m_type = PassType::none;
			if (m_savepoint != nullptr)
				delete m_savepoint;
			m_savepoint = nullptr;
		}
	}


	void Pass::Update(bool bAlphaBlendAdd)
	{
		//ハマった話…CallableShaderに対応した際、PassType::raytracingのUpdateを書き換え忘れていてアプリケーションが落ちる事になっていた

		switch (m_type) {
		case PassType::none: {
			ThrowIfFailed(E_FAIL, L"Pass::Update() failed ... not intialized pass", __FILEW__, __LINE__);
			break;
		}
		case PassType::raytracing: {
			auto s = (PassSaverRT*)m_savepoint;
			RaytracingPass(s->raygen, s->miss, s->hitgroup, s->callable, s->maxPayloadSize, s->maxAttributeSize, s->maxRecursionDepth);
			break;
		}
		case PassType::postprocess: {
			auto s = (PassSaverPP*)m_savepoint;
			PostProcessPass(s->vs, s->ps, bAlphaBlendAdd);
			break;
		}
		case PassType::rasterizer: {
			auto s = (PassSaverRaster*)m_savepoint;
			RasterizerPass(s->vs, s->ps, s->vbs, s->ibs, s->inputElementDescs, s->blendDesc, s->rasterizerDesc, s->depthStencilDesc, s->primitiveTopologyType, s->sampleMask);
			break;
		}
		}
	}


	//ルートシグネチャいっこつくる
	ComPtr<ID3D12RootSignature> Create1RootSignature(DXR* dxr, const D3D12_ROOT_SIGNATURE_DESC& desc)
	{
		ComPtr<ID3DBlob> sig;
		ComPtr<ID3DBlob> error;
		HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error);
		if (hr != S_OK) {
			if (error) {
				OutputDebugStringA((char*)error->GetBufferPointer());
			}
		}
		ThrowIfFailed(hr, L"D3D12SerializeRootSignature failed", __FILEW__, __LINE__);

		ComPtr<ID3D12RootSignature> pRootSig;
		hr = dxr->Device()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
		ThrowIfFailed(hr, L"CreateRootSignature failed", __FILEW__, __LINE__);

		return pRootSig;
	}


	//シェーダ関連を含まない部分まで作成
	void Pass::CreateRootSignature()
	{
		m_RPIdx.clear();
		UINT count_CBV_SRV_UAV = 0;	//総ディスクリプタ数(ディスクリプタヒープ作成時に使う)
		std::vector<D3D12_DESCRIPTOR_RANGE> dr;

		//SRVの各スペース
		for (int i = 0; i < SRV.size(); i++) {
			if (SRV[i].size()) {
				D3D12_DESCRIPTOR_RANGE descRangeSRV = {};
				descRangeSRV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				descRangeSRV.NumDescriptors = (UINT)SRV[i].size();
				descRangeSRV.RegisterSpace = i;
				m_RPIdx.push_back(count_CBV_SRV_UAV);
				count_CBV_SRV_UAV += descRangeSRV.NumDescriptors;
				dr.push_back(descRangeSRV);
			}
		}

		// 出力バッファ(UAV) を u0～ 
		if (UAV.size()) {
			D3D12_DESCRIPTOR_RANGE descRangeOutput = {};
			descRangeOutput.NumDescriptors = (UINT)UAV.size();
			descRangeOutput.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			descRangeOutput.OffsetInDescriptorsFromTableStart = 0;
			m_RPIdx.push_back(count_CBV_SRV_UAV);
			count_CBV_SRV_UAV += (UINT)UAV.size();
			dr.push_back(descRangeOutput);
		}
		//定数バッファをb0～
		if (CBV.size()) {
			D3D12_DESCRIPTOR_RANGE descRangeCB = {};
			descRangeCB.NumDescriptors = (UINT)CBV.size();
			descRangeCB.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			descRangeCB.OffsetInDescriptorsFromTableStart = 0;
			m_RPIdx.push_back(count_CBV_SRV_UAV);
			count_CBV_SRV_UAV += (UINT)CBV.size();
			dr.push_back(descRangeCB);
		}

		//ルートパラメータにディスクリプタレンジをまとめる
		std::vector<D3D12_ROOT_PARAMETER>rp(dr.size());
		for (int i = 0; i < (int)dr.size(); i++) {
			rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rp[i].DescriptorTable.NumDescriptorRanges = 1;
			rp[i].DescriptorTable.pDescriptorRanges = &dr[i];
		}


		//グローバルルートシグネチャ作成
		{
			D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
			rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			rootDesc.NumParameters = (UINT)dr.size();
			rootDesc.pParameters = rp.data();
			rootDesc.NumStaticSamplers = Samplers.size();
			rootDesc.pStaticSamplers = Samplers.data();
			m_rootSig = Create1RootSignature(m_dxr, rootDesc);
			m_rootSig->SetName(L"RootSignature");
		}

		if (m_dxr->RaytracingSupport()) {
			//ローカルルートシグネチャ(空)、空なので全シェーダから同じグローバルルートシグネチャ由来のパラメータのみ参照される
			{
				D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
				rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
				rootDesc.NumParameters = 0;
				rootDesc.pParameters = nullptr;
				m_localRootSig = Create1RootSignature(m_dxr, rootDesc);
				m_localRootSig->SetName(L"LocalRootSignature");
			}
		}


		//ディスクリプタヒープの作成
		D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
		dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dhd.NumDescriptors = count_CBV_SRV_UAV;
		dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		if (dhd.NumDescriptors != 0) {
			ThrowIfFailed(m_dxr->Device()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(m_descHeap.ReleaseAndGetAddressOf())), L"CreateDescriptorHeap Failed", __FILEW__, __LINE__);
			auto handle = m_descHeap->GetCPUDescriptorHandleForHeapStart();
			auto increment = m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			//ビューを放り込む

			for (int i = 0; i < SRV.size(); i++) {
				for (int j = 0; j < SRV[i].size(); j++) {
					D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
					switch (SRV[i][j]->type) {
					case ResType::tlas:
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
						srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srvDesc.RaytracingAccelerationStructure.Location = SRV[i][j]->res->GetGPUVirtualAddress();
						m_dxr->Device()->CreateShaderResourceView(nullptr, &srvDesc, handle);	//なんかTLASのpResourceはnullでいいっぽい
						handle.ptr += increment;
						break;
					case ResType::buf:
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						srvDesc.Format = DXGI_FORMAT_UNKNOWN;
						srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srvDesc.Buffer.FirstElement = 0;
						srvDesc.Buffer.NumElements = SRV[i][j]->desc().Width / (static_cast<Buf*>(SRV[i][j]))->elemSize;
						srvDesc.Buffer.StructureByteStride = (static_cast<Buf*>(SRV[i][j]))->elemSize;
						m_dxr->Device()->CreateShaderResourceView(SRV[i][j]->res.Get(), &srvDesc, handle);
						handle.ptr += increment;
						break;
					case ResType::tex2D:
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
						srvDesc.Format = SRV[i][j]->desc().Format;
						srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srvDesc.Texture2D.MipLevels = SRV[i][j]->desc().MipLevels;
						srvDesc.Texture2D.MostDetailedMip = 0;
						m_dxr->Device()->CreateShaderResourceView(SRV[i][j]->res.Get(), &srvDesc, handle);
						handle.ptr += increment;
						break;
					}
				}
			}

			for (int i = 0; i < UAV.size(); i++) {
				D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				switch (UAV[i].res->type) {
				case ResType::buf:
					uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					uavDesc.Buffer.NumElements = UAV[i].res->desc().Width / (static_cast<Buf*>(UAV[i].res))->elemSize;
					uavDesc.Format = DXGI_FORMAT_UNKNOWN;
					uavDesc.Buffer.StructureByteStride = (static_cast<Buf*>(UAV[i].res))->elemSize;
					m_dxr->Device()->CreateUnorderedAccessView(UAV[i].res->res.Get(), nullptr, &uavDesc, handle);
					handle.ptr += increment;
					break;
				case ResType::tex2D:
					uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
					uavDesc.Texture2D.MipSlice = UAV[i].mipSlice;
					m_dxr->Device()->CreateUnorderedAccessView(UAV[i].res->res.Get(), nullptr, &uavDesc, handle);
					handle.ptr += increment;
					break;
				}
			}

			for (int i = 0; i < CBV.size(); i++) {
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
				cbvDesc.BufferLocation = CBV[i]->res->GetGPUVirtualAddress();
				cbvDesc.SizeInBytes = CBV[i]->desc().Width;
				m_dxr->Device()->CreateConstantBufferView(&cbvDesc, handle);
				handle.ptr += increment;
			}
		}

		//DSVのディスクリプタヒープ
		{
			if (DSV.size() > 0) {
				D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
				dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
				dhd.NumDescriptors = DSV.size();
				dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				dhd.NodeMask = 0;
				ThrowIfFailed(m_dxr->Device()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(m_descHeapDSV.ReleaseAndGetAddressOf())), L"CreateDescriptorHeap(DSV) Failed", __FILEW__, __LINE__);
				auto handle = m_descHeapDSV->GetCPUDescriptorHandleForHeapStart();
				auto increment = m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
				//DSV作成
				for (int i = 0; i < DSV.size(); i++) {
					D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
					dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
					auto& p = DSV[i];
					dsvDesc.Format = p.tex->desc().Format;
					dsvDesc.Texture2D.MipSlice = p.mipSlice;
					m_dxr->Device()->CreateDepthStencilView(p.tex->res.Get(), &dsvDesc, handle);
					handle.ptr += increment;
				}
			}
		}

		//RTVのディスクリプタヒープ
		{
			if (RTV.size() > 0) {
				D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
				dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				dhd.NumDescriptors = RTV.size();
				dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				dhd.NodeMask = 0;
				ThrowIfFailed(m_dxr->Device()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(m_descHeapRTV.ReleaseAndGetAddressOf())), L"CreateDescriptorHeap(RTV) Failed", __FILEW__, __LINE__);
				auto handle = m_descHeapRTV->GetCPUDescriptorHandleForHeapStart();
				auto increment = m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

				//RTV作成
				for (int i = 0; i < RTV.size(); i++) {
					auto& p = RTV[i];
					D3D12_RENDER_TARGET_VIEW_DESC rtvd = {};
					rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
					rtvd.Format = p.tex->desc().Format;
					rtvd.Texture2D.MipSlice = p.mipSlice;
					m_dxr->Device()->CreateRenderTargetView(p.tex->res.Get(), &rtvd, handle);
					handle.ptr += increment;
				}
			}
		}
	}

	D3D12_STATE_SUBOBJECT AllocDXILSO(IDxcBlob* blob, const wchar_t* entrypoint, const wchar_t* uniquename)
	{
		D3D12_DXIL_LIBRARY_DESC *dld = new D3D12_DXIL_LIBRARY_DESC;
		dld->DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
		dld->DXILLibrary.BytecodeLength = blob->GetBufferSize();
		dld->NumExports = 1;
		D3D12_EXPORT_DESC* ed = new D3D12_EXPORT_DESC;
		ed->Name = uniquename;				//raygenshader以外は↓そのままだと違うHLSLファイルから読まれた同名の関数とカブる恐れがあるので、一意な名前でエクスポートする
		ed->ExportToRename = entrypoint;	//コンパイラの知ってる関数のエントリポイント名
		ed->Flags = D3D12_EXPORT_FLAG_NONE;
		dld->pExports = ed;

		return { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY , dld };
	}

	void DeallocDXILSO(D3D12_STATE_SUBOBJECT so)
	{
		if (so.Type == D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY) {
			delete ((D3D12_DXIL_LIBRARY_DESC*)(so.pDesc))->pExports;
			delete so.pDesc;
		}
	}
	
	/*
	void Pass::RaytracingPass(const Shader& raygen, const wchar_t* miss, const wchar_t* closesthit, const wchar_t* anyhit, const wchar_t* intersection, const std::vector<std::wstring>& callable, UINT maxPayloadSize, UINT maxAttributeSize, UINT maxRecursionDepth)
	{
		std::vector<std::wstring>misses(1, miss);
		std::vector<HItGroup>hgs(1);
		if (intersection != nullptr)
			if (intersection[0] == L'0')
				hgs[0] = { D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,  closesthit, anyhit, intersection};
		else
			hgs[0] = { D3D12_HIT_GROUP_TYPE_TRIANGLES,  closesthit, anyhit, intersection };

		RaytracingPass(raygen, misses, hgs, callable, maxPayloadSize, maxAttributeSize, maxRecursionDepth);
	}
	*/


	void Pass::RaytracingPass(const Shader& raygen, const std::vector<std::wstring>& miss, const std::vector<HitGroup>& hitgroup, const std::vector<std::wstring>& callable, UINT maxPayloadSize, UINT maxAttributeSize, UINT maxRecursionDepth)
	{
		m_type = PassType::raytracing;
		
		//作った時の情報を保存
		m_savepoint = new PassSaverRT();
		auto save = (PassSaverRT*) m_savepoint;
		save->raygen = raygen;
		save->miss = miss;
		save->hitgroup = hitgroup;
		save->callable = callable;
		save->maxPayloadSize = maxPayloadSize;
		save->maxAttributeSize = maxAttributeSize;
		save->maxRecursionDepth = maxRecursionDepth;

		CreateRootSignature();

		std::vector<std::wstring> missExport = miss;
		std::vector<std::wstring> callableExport = callable;
		std::vector<std::wstring> hitgroupExport(hitgroup.size());

		for (int i = 0; auto & s : hitgroup) {
			hitgroupExport[i] = std::format(L"hitgroup{}",i);
			i++;
		}

		CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
		
		// DXIL library
		// This contains the shaders and their entrypoints for the state object.
		// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
		auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE libdxil = { raygen.blob->GetBufferPointer(), raygen.blob->GetBufferSize() };
		lib->SetDXILLibrary(&libdxil);

		// Define which shader exports to surface from the library.
		// If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
		// ↑この理由により、shader exportsは一切サブオブジェクトに含めない事にする 27朝修正
		/*
		{
			lib->DefineExport(raygen.entrypoint.c_str());
			for (const auto& s : miss)
				lib->DefineExport(s.c_str());
			for (const auto& s : hitgroup) {
				if (s.closesthit != L"")
					lib->DefineExport(s.closesthit.c_str());
				if (s.anyhit != L"")
					lib->DefineExport(s.anyhit.c_str());
				if (s.intersection != L"")
					lib->DefineExport(s.intersection.c_str());
			}
			for (const auto& s : callable)
				lib->DefineExport(s.c_str());
		}
		*/

		// hit group
		std::vector<CD3DX12_HIT_GROUP_SUBOBJECT*>hitGroupSO;
		for (int i = 0; const auto & h : hitgroup) {
			auto hgso = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
			if (h.closesthit != L"")
				hgso->SetClosestHitShaderImport(h.closesthit.c_str());
			if (h.anyhit!= L"")
				hgso->SetAnyHitShaderImport(h.anyhit.c_str());
			if (h.intersection != L"")
				hgso->SetIntersectionShaderImport(h.intersection.c_str());
			hgso->SetHitGroupExport(hitgroupExport[i].c_str());
			hgso->SetHitGroupType(h.type);
			hitGroupSO.push_back(hgso);
			i++;
		}

		// Shader config
		// Defines the maximum sizes in bytes for the ray payload and attribute structure.
		auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
		shaderConfig->Config(maxPayloadSize, maxAttributeSize);

		// Local root signature and shader association 未対応 27朝修正
		/*
		auto localRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		localRootSignature->SetRootSignature(m_localRootSig.Get());
		// Shader association
		auto rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
		rootSignatureAssociation->AddExport(raygen.entrypoint.c_str());	//ここ謎
		*/
		// This is a root signature that enables a shader to have unique arguments that come from shader tables.

		// Global root signature
		// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
		auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
		globalRootSignature->SetRootSignature(m_rootSig.Get());

		// Pipeline config
		// Defines the maximum TraceRay() recursion depth.
		auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
		// PERFOMANCE TIP: Set max recursion depth as low as needed 
		// as drivers may apply optimization strategies for low recursion depths. 
		pipelineConfig->Config(maxRecursionDepth);

		PrintStateObjectDesc(raytracingPipeline);

		// Create the state object.
		ThrowIfFailed(m_dxr->Device()->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_PSO)), L"Couldn't create DirectX Raytracing state object.\n", __FILEW__, __LINE__);

		HRESULT hr = m_PSO->QueryInterface(IID_PPV_ARGS(m_PSOprop.ReleaseAndGetAddressOf()));
		ThrowIfFailed(hr, L"Error: failed to get RTPSO info object!", __FILEW__, __LINE__);



#if 0
		D3D12_STATE_SUBOBJECT* so = new D3D12_STATE_SUBOBJECT[10 + miss.size() + hitgroup.size()*3 + callable.size()];	//雑に確保する
		int idxSO = 0;
		int idxLocalAssoc = 0;


		//エクスポートする時の別名
		std::vector<std::wstring>missExport(miss.size());
		std::vector<std::wstring>hitgroupExport(hitgroup.size());
		std::vector<std::wstring>closesthitExport(hitgroup.size()), anyhitExport(hitgroup.size()), intersectionExport(hitgroup.size());
		std::vector<std::wstring>callableExport(callable.size());

		//各シェーダのDXIL library subobject
		so[idxSO++] = (AllocDXILSO(raygen.blob.Get(), nullptr, raygen.entrypoint.c_str()));

		for (int i = 0; auto& p : miss) {
			missExport[i] = std::format(L"Miss{}", i);
			so[idxSO++] = (AllocDXILSO(p.blob.Get(), p.entrypoint.c_str(), missExport[i].c_str()));
			i++;
		}

		for (int i = 0;  auto& p : hitgroup) {
			if (p.closesthit.blob != nullptr) {
				closesthitExport[i] = std::format(L"ClosestHit{}", i);
				so[idxSO++] = (AllocDXILSO(p.closesthit.blob.Get(), p.closesthit.entrypoint.c_str(), closesthitExport[i].c_str()));
			}

			if (p.anyhit.blob != nullptr) {
				anyhitExport[i] = std::format(L"AnyHit{}", i);
				so[idxSO++] = (AllocDXILSO(p.anyhit.blob.Get(), p.anyhit.entrypoint.c_str(), anyhitExport[i].c_str()));
			}

			if (p.intersection.blob != nullptr) {
				intersectionExport[i] = std::format(L"Intersection{}", i);
				so[idxSO++] = (AllocDXILSO(p.intersection.blob.Get(), p.intersection.entrypoint.c_str(), intersectionExport[i].c_str()));
			}
			i++;
		}

		for (int i = 0; auto& p : callable) {
			callableExport[i] = std::format(L"Callable{}", i);
			so[idxSO++] = (AllocDXILSO(p.blob.Get(), p.entrypoint.c_str(), callableExport[i].c_str()));
			i++;
		}

		// hit group subobject
		for (int i = 0; auto& p:hitgroup) {
			hitgroupExport[i] = std::format(L"HitGroup{}", i);
			auto *hitGroupDesc = new D3D12_HIT_GROUP_DESC;
			*hitGroupDesc = {};
			hitGroupDesc->HitGroupExport = hitgroupExport[i].c_str();
			hitGroupDesc->Type = p.type;
			if (p.closesthit.blob != nullptr)
				hitGroupDesc->ClosestHitShaderImport = closesthitExport[i].c_str();
			if (p.anyhit.blob != nullptr)
				hitGroupDesc->AnyHitShaderImport = anyhitExport[i].c_str();
			if (p.intersection.blob != nullptr)
				hitGroupDesc->IntersectionShaderImport = intersectionExport[i].c_str();
			so[idxSO++] = { D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, hitGroupDesc };
			i++;
		}


		//shader config subobject
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
		shaderConfig.MaxPayloadSizeInBytes = maxPayloadSize;
		shaderConfig.MaxAttributeSizeInBytes = maxAttributeSize;
		so[idxSO++] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig };

		//pipeline config subobject
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
		pipelineConfig.MaxTraceRecursionDepth = maxRecursionDepth;
		so[idxSO++] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig };

		//global root signature subobject
		ComPtr<ID3D12RootSignature> pGlobalRootSignature = m_rootSig;
		D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignature = {};
		globalRootSignature.pGlobalRootSignature = pGlobalRootSignature.Get();
		so[idxSO++] = { D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRootSignature };

		//local root signature subobject
		ComPtr<ID3D12RootSignature> pLocalRootSignature = m_localRootSig;
		D3D12_LOCAL_ROOT_SIGNATURE localRootSignature = {};
		localRootSignature.pLocalRootSignature = pLocalRootSignature.Get();
		idxLocalAssoc = idxSO;
		so[idxSO++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &localRootSignature };

		//exports association subobject
		std::vector<const WCHAR*>pExports(hitgroup.size());
		for (int i = 0; i < hitgroup.size(); i++)
			pExports[i] = hitgroupExport[i].c_str();
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION localRootSignatureAssociation = {};
		localRootSignatureAssociation.pSubobjectToAssociate = &so[idxLocalAssoc];
		localRootSignatureAssociation.NumExports = pExports.size();
		localRootSignatureAssociation.pExports = pExports.data();
		so[idxSO++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &localRootSignatureAssociation };

		//PSO desc
		D3D12_STATE_OBJECT_DESC pipelineDesc = {};
		pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		pipelineDesc.NumSubobjects = idxSO;
		pipelineDesc.pSubobjects = so;

		//PSO作成
		ThrowIfFailed(m_dxr->Device()->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(m_PSO.GetAddressOf())), L"CreateStateObject Failed", __FILEW__, __LINE__);

		//PSO property
		HRESULT hr = m_PSO->QueryInterface(IID_PPV_ARGS(m_PSOprop.ReleaseAndGetAddressOf()));
		ThrowIfFailed(hr, L"Error: failed to get RTPSO info object!", __FILEW__, __LINE__);

		//解放
		for (int i = 0; i < idxSO; i++) {
			DeallocDXILSO(so[i]);
			if (so[i].Type == D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP)
				delete so[i].pDesc;
		}
		delete[] so;
#endif


		/* Shader Table関係 */

		//Shader Table作成
		int numShaderRecords = 1 + miss.size() + hitgroup.size() + callable.size();

		UINT shaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8;	//ディスクリプタテーブルへのポインタ分
		shaderTableEntrySize = Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		UINT shaderTableSize = shaderTableEntrySize * numShaderRecords;

		CD3DX12_RESOURCE_DESC shaderTableDesc = CD3DX12_RESOURCE_DESC::Buffer(shaderTableSize);
		D3D12_HEAP_PROPERTIES hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		ThrowIfFailed(m_dxr->Device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&shaderTableDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(m_shaderTable.GetAddressOf())),L"Create ShaderTableBuffer Failed", __FILEW__, __LINE__);

		//Mapして読み書きできるようにする
		uint8_t* pData = nullptr;
		ThrowIfFailed(m_shaderTable->Map(0, nullptr, (void**)&pData), L"shaderTable->Map Failed", __FILEW__, __LINE__);
		ZeroMemory(pData, shaderTableSize);	//念のため

		//shader identifierの取得
		void* pRayGenShaderIdentifier = m_PSOprop->GetShaderIdentifier(raygen.entrypoint.c_str());

		//shader tableにshader identifierを書き込む
		memcpy(pData, pRayGenShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		//ルートパラメータをセットし、ディスクリプタテーブルへのポインタを得る
		*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = m_descHeap->GetGPUDescriptorHandleForHeapStart();	//ディスクリプタテーブルへのポインタ
		pData += shaderTableEntrySize;
		
		for (auto& p : missExport) {
			void* pMissShaderIdentifier = m_PSOprop->GetShaderIdentifier(p.c_str());
			memcpy(pData, pMissShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = m_descHeap->GetGPUDescriptorHandleForHeapStart();
			pData += shaderTableEntrySize;
		}

		for (auto& p : hitgroupExport) {
			void* pHitGroupShaderIdentifier = m_PSOprop->GetShaderIdentifier(p.c_str());
			memcpy(pData, pHitGroupShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = m_descHeap->GetGPUDescriptorHandleForHeapStart();
			pData += shaderTableEntrySize;
		}

		for (auto & p : callableExport) {
			void* pCallableShaderIdentifier = m_PSOprop->GetShaderIdentifier(p.c_str());
			memcpy(pData, pCallableShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = m_descHeap->GetGPUDescriptorHandleForHeapStart();
			pData += shaderTableEntrySize;
		}

		//Unmapしとく
		m_shaderTable->Unmap(0, nullptr);

		
		//DispatchRaysDescを大体作っとく。仕上(出力サイズの指定とか)はRender時に
		m_dispatchRaysDesc = {};

		m_dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = m_shaderTable->GetGPUVirtualAddress();
		m_dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = shaderTableSize;

		m_dispatchRaysDesc.MissShaderTable.StartAddress = m_shaderTable->GetGPUVirtualAddress() + shaderTableEntrySize;
		m_dispatchRaysDesc.MissShaderTable.SizeInBytes = miss.size() * shaderTableEntrySize;
		m_dispatchRaysDesc.MissShaderTable.StrideInBytes = shaderTableEntrySize;

		m_dispatchRaysDesc.HitGroupTable.StartAddress = m_shaderTable->GetGPUVirtualAddress() + (shaderTableEntrySize * (miss.size() + 1));
		m_dispatchRaysDesc.HitGroupTable.SizeInBytes = hitgroup.size() * shaderTableEntrySize;
		m_dispatchRaysDesc.HitGroupTable.StrideInBytes = shaderTableEntrySize;

		if (callable.size() > 0) {
			m_dispatchRaysDesc.CallableShaderTable.StartAddress = m_shaderTable->GetGPUVirtualAddress() + shaderTableEntrySize * (1 + miss.size() + hitgroup.size());
			m_dispatchRaysDesc.CallableShaderTable.SizeInBytes = callable.size() * shaderTableEntrySize;
			m_dispatchRaysDesc.CallableShaderTable.StrideInBytes = shaderTableEntrySize;
		}

	}

	//PushRTVで放り込まれたレンダーターゲット(最大8枚)に対してレンダリングする
	//RTVに何も入ってなければバックバッファへのレンダリングとして扱う
	//つまり、よろずDXRではバックバッファとそれ以外のバッファへ同時にMRT出来ない
	void Pass::PostProcessPass(const Shader& vs, const Shader& ps, bool bAlphaBlendAdd)
	{
		m_type = PassType::postprocess;

		//作った時の情報を保存
		m_savepoint = new PassSaverPP();
		auto save = (PassSaverPP*)m_savepoint;
		save->ps = ps;
		save->vs = vs;

		CreateRootSignature();

		D3D12_INPUT_ELEMENT_DESC layout[2] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
		psd.InputLayout.NumElements = _countof(layout);
		psd.InputLayout.pInputElementDescs = layout;
		psd.VS.pShaderBytecode = vs.blob->GetBufferPointer();
		psd.VS.BytecodeLength = vs.blob->GetBufferSize();
		psd.PS.pShaderBytecode = ps.blob->GetBufferPointer();
		psd.PS.BytecodeLength = ps.blob->GetBufferSize();
		if (RTV.size() == 0) {
			psd.NumRenderTargets = 1;
			psd.RTVFormats[0] = m_dxr->BackBufferFormat();
		}
		else {
			psd.NumRenderTargets = RTV.size();
			for (int i = 0; i < RTV.size(); i++)
				psd.RTVFormats[i] =RTV[i].tex->desc().Format;
		}
		psd.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		if(bAlphaBlendAdd == true) {
            //<加算合成>
			// ブレンドを有効にする
            psd.BlendState.RenderTarget[0].BlendEnable = true;
			psd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			// ピクセルシェーダーが出力するRGB値に対してαを乗算する(SRCrgb ＊ SRCα) 
            psd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			// レンダーターゲットの現在のRGB値に対して1-αを乗算する(DESTrgb ＊ (1 ー SRCα))
            psd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		}
		psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psd.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psd.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psd.SampleDesc.Count = 1;
		psd.SampleDesc.Quality = 0;
		psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psd.NodeMask = 0;
		psd.pRootSignature = m_rootSig.Get();
		m_dxr->Device()->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(m_PSOPP.ReleaseAndGetAddressOf()));
	}

	void Pass::RasterizerPass(const Shader& vs, const Shader& ps, const std::vector<Buf*>& vbs, const std::vector<Buf*>& ibs,
		const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElementDescs,
		const D3D12_BLEND_DESC& blenddesc, const D3D12_RASTERIZER_DESC& rasterizerDesc, const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc,
		const D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType,
		const UINT sampleMask)
	{
		if (vbs.size() != ibs.size())
			ThrowIfFailed(E_INVALIDARG, std::format(L"number of VertexBuffer({}) and IndexBuffer({}) are not match", vbs.size(), ibs.size()), __FILEW__, __LINE__);

		m_type = PassType::rasterizer;

		//作った時の情報を保存
		m_savepoint = new PassSaverRaster();
		auto save = (PassSaverRaster*)m_savepoint;
		*save = { vs,ps,vbs,ibs, inputElementDescs, blenddesc, rasterizerDesc, depthStencilDesc, primitiveTopologyType, sampleMask };

		CreateRootSignature();

		m_rasterVBs = vbs;
		m_rasterIBs = ibs;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
		psd.InputLayout.NumElements = inputElementDescs.size();
		psd.InputLayout.pInputElementDescs = inputElementDescs.data();
		psd.VS.pShaderBytecode = vs.blob->GetBufferPointer();
		psd.VS.BytecodeLength = vs.blob->GetBufferSize();
		psd.PS.pShaderBytecode = ps.blob->GetBufferPointer();
		psd.PS.BytecodeLength = ps.blob->GetBufferSize();
		if (RTV.size() == 0) {
			psd.NumRenderTargets = 1;
			psd.RTVFormats[0] = m_dxr->BackBufferFormat();
		} else {
			psd.NumRenderTargets = RTV.size();
			for (int i = 0; i < RTV.size(); i++)
				psd.RTVFormats[i] = RTV[i].tex->desc().Format;
		}
		psd.BlendState = blenddesc;
		psd.PrimitiveTopologyType = primitiveTopologyType;
		psd.RasterizerState = rasterizerDesc;
		psd.SampleMask = sampleMask;
		psd.SampleDesc.Count = 1;
		psd.SampleDesc.Quality = 0;
		
		if (DSV.size() > 0) {
			psd.DepthStencilState = depthStencilDesc;
			psd.DSVFormat = DSV[0].tex->desc().Format;
		}

		psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psd.NodeMask = 0;
		psd.pRootSignature = m_rootSig.Get();
		m_dxr->Device()->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(m_PSOPP.ReleaseAndGetAddressOf()));
	}


	void Pass::ComputePass(const Shader& cs)
	{
		m_type = PassType::compute;

		//作った時の情報を保存
		m_savepoint = new PassSaverCS();
		auto save = (PassSaverCS*)m_savepoint;
		*save = { cs };

		CreateRootSignature();

		//パイプライン
		D3D12_COMPUTE_PIPELINE_STATE_DESC psd = {};
		psd.CS.pShaderBytecode = cs.blob->GetBufferPointer();
		psd.CS.BytecodeLength = cs.blob->GetBufferSize();
		psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psd.NodeMask = 0;
		psd.pRootSignature = m_rootSig.Get();
		m_dxr->Device()->CreateComputePipelineState(&psd, IID_PPV_ARGS(m_CSPSO.ReleaseAndGetAddressOf()));
	}

	//CSは実行する都度待つ
	void Pass::Compute(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
	{
		//コンピュートパスとして初期化されてないなら例外です
		if (m_type != PassType::compute)
			ThrowIfFailed(E_FAIL, L"Pass:Compute() failed ... not ComputePass. before Compute(), must call ComputePass()", __FILEW__, __LINE__);

		std::vector<D3D12_RESOURCE_BARRIER> bar;
		//バリア
		for (auto& p : SRV)
			for (auto& o : p)
				m_dxr->AddBarrier(o, bar, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		for (auto& o : UAV)
			m_dxr->AddBarrier(o.res, bar, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		
		if (bar.size())
			m_dxr->CommandListCS()->ResourceBarrier(bar.size(), bar.data());

		m_dxr->CommandListCS()->SetComputeRootSignature(m_rootSig.Get());

		m_dxr->CommandListCS()->SetPipelineState(m_CSPSO.Get());

		ID3D12DescriptorHeap* heaps[] = { m_descHeap.Get() };
		m_dxr->CommandListCS()->SetDescriptorHeaps(1, heaps);

		auto handle = m_descHeap->GetGPUDescriptorHandleForHeapStart();
		auto increment = m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (int i = 0; i < m_RPIdx.size(); i++) {
			auto handlePlus = handle;
			handlePlus.ptr += (UINT64)increment * m_RPIdx[i];
			m_dxr->CommandListCS()->SetComputeRootDescriptorTable(i, handlePlus);
		}

		m_dxr->CommandListCS()->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

	}

	void Pass::BBClearSetting(bool clear, const DirectX::XMFLOAT4& clearValue)
	{
		float cc[4];
		cc[0] = clearValue.x;
		cc[1] = clearValue.y;
		cc[2] = clearValue.z;
		cc[3] = clearValue.w;

		CV c = {};
		c.clear = clear;
		c.value = CD3DX12_CLEAR_VALUE(m_dxr->BackBufferFormat(), cc);
		m_BBcv = c;
	}

	void Pass::Render()
	{
		//RTVに何か入ってればそのサイズ
		if (RTV.size() > 0) {
			auto desc = RTV[0].tex->desc();
			Render(max(1, desc.Width >> RTV[0].mipSlice), max(1, desc.Height >> RTV[0].mipSlice));
		} else {
			//RTVが空ならバックバッファのサイズ
			Render(m_dxr->Width(), m_dxr->Height());
		}
	}

	void Pass::Render(int width, int height, int depth)
	{
		if (m_type == PassType::none) {
			ThrowIfFailed(E_FAIL, L"Pass::Render() failed ... before Render(), must call RaytracingPass() or RasterizerPass() or PostProcessPass()", __FILEW__, __LINE__);
		}
		if (m_type == PassType::compute) {
			ThrowIfFailed(E_FAIL, L"Pass::Render() failed ... ComputePass can't Render()", __FILEW__, __LINE__);
		}

		std::vector<D3D12_RESOURCE_BARRIER> bar;

		D3D12_RESOURCE_BARRIER barrier = {};
		ID3D12Resource* backbuffer = m_dxr->BackBuffers(m_dxr->BackBufferIndex()).Get();

		if (m_type == PassType::raytracing) {
			/*** レイトレパス ***/

			for (auto& p : SRV)
				for (auto& o : p)
					if (o->type != ResType::tlas)
						m_dxr->AddBarrier(o, bar, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			for (auto& o : UAV)
				m_dxr->AddBarrier(o.res, bar, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			if (bar.size())
				m_dxr->CommandList()->ResourceBarrier(bar.size(), bar.data());

			//UAV/SRV/CBVとsamplerヒープ
			ID3D12DescriptorHeap* ppHeaps[] = { m_descHeap.Get() };
			m_dxr->CommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

			m_dxr->CommandList()->SetComputeRootSignature(m_rootSig.Get());
			auto handle = m_descHeap->GetGPUDescriptorHandleForHeapStart();
			auto increment = m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			for (int i = 0; auto& p: m_RPIdx) {
				auto handlePlus = handle;
				handlePlus.ptr += (UINT64)increment * p;
				m_dxr->CommandList()->SetComputeRootDescriptorTable(i, handlePlus);
				i++;
			}

			// DispatchRays
			D3D12_DISPATCH_RAYS_DESC desc = m_dispatchRaysDesc;

			desc.Width = width;
			desc.Height = height;
			desc.Depth = depth;

			m_dxr->CommandList()->SetPipelineState1(m_PSO.Get());
			m_dxr->CommandList()->DispatchRays(&desc);

		} else if (m_type == PassType::postprocess || m_type == PassType::rasterizer) {
			/*** ポストプロセス or ラスタライザパス ***/
			//PushRTVされたバッファがある場合はそのバッファに出力
			//空の場合はバックバッファへ出力
			//バックバッファとそれ以外のRTVへのMRTは不可

			for (int i=0; i<SRV.size(); i++)
				for (auto& o : SRV[i])
					m_dxr->AddBarrier(o, bar, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			for (auto& o : UAV)
				m_dxr->AddBarrier(o.res, bar, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			for (auto& o : RTV)
				m_dxr->AddBarrier(o.tex, bar, D3D12_RESOURCE_STATE_RENDER_TARGET);
			for (auto& o : DSV)
				m_dxr->AddBarrier(o.tex, bar, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			for (auto& o : m_rasterVBs)
				m_dxr->AddBarrier(o, bar, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			for (auto& o : m_rasterIBs)
				m_dxr->AddBarrier(o, bar, D3D12_RESOURCE_STATE_INDEX_BUFFER);

			if (bar.size())
				m_dxr->CommandList()->ResourceBarrier(bar.size(), bar.data());

			D3D12_VIEWPORT vp = {};
			vp.Width = (float)width;	vp.Height = (float)height;
			vp.TopLeftX = 0; vp.TopLeftY = 0; vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;

			D3D12_RECT scissor = {};
			scissor.left = 0; scissor.top = 0; scissor.right = width; scissor.bottom = height;


			m_dxr->CommandList()->SetGraphicsRootSignature(m_rootSig.Get());
			if (m_descHeap != nullptr) {
				ID3D12DescriptorHeap* ppHeaps[] = { m_descHeap.Get() };
				m_dxr->CommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			}
			if (m_descHeap != nullptr) {
				auto handle = m_descHeap->GetGPUDescriptorHandleForHeapStart();
				auto increment = m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				for (int i = 0; i < m_RPIdx.size(); i++) {
					auto handlePlus = handle;
					handlePlus.ptr += (UINT64)increment * m_RPIdx[i];
					m_dxr->CommandList()->SetGraphicsRootDescriptorTable(i, handlePlus);
				}
			}
			m_dxr->CommandList()->SetPipelineState(m_PSOPP.Get());

			if (RTV.size() == 0) {
				//バックバッファのレンダリング準備
				barrier = CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
				m_dxr->CommandList()->ResourceBarrier(1, &barrier);
				auto rtvH = m_dxr->BackBufferCPUHandle(m_dxr->BackBufferIndex()); // m_backBufferCPUHandle[m_dxr->m_backBufferIndex];
				if (DSV.size() == 0)
					m_dxr->CommandList()->OMSetRenderTargets(1, &rtvH, true, nullptr);
				else {
					auto dsvH = m_descHeapDSV->GetCPUDescriptorHandleForHeapStart();
					m_dxr->CommandList()->OMSetRenderTargets(1, &rtvH, true, &dsvH);
				}
				
				//クリア
				if (m_BBcv.clear)
					m_dxr->CommandList()->ClearRenderTargetView(rtvH, m_BBcv.value.Color, 0, nullptr);

				//Zバッファの登録とクリア
				if (DSV.size() > 0) {
					auto handle = m_descHeapDSV->GetCPUDescriptorHandleForHeapStart();
					for (int i = 0; i < DSV.size(); i++) {
						auto d = DSV[i].cv;
						if (d.clear)
							m_dxr->CommandList()->ClearDepthStencilView(handle, d.flags,
								d.value.DepthStencil.Depth, d.value.DepthStencil.Stencil, 0, nullptr);
						handle.ptr += m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
					}
				}
			} else {
				//レンダーターゲットの指定
				auto rtvH = m_descHeapRTV->GetCPUDescriptorHandleForHeapStart();
				if (DSV.size() == 0)
					m_dxr->CommandList()->OMSetRenderTargets(RTV.size(), &rtvH, true, nullptr);
				else {
					auto dsvH = m_descHeapDSV->GetCPUDescriptorHandleForHeapStart();
					m_dxr->CommandList()->OMSetRenderTargets(RTV.size(), &rtvH, true, &dsvH);
				}
				//クリア
				for (auto& p : RTV) {
					auto handle = rtvH;
					auto cv = p.cv;
					if (cv.clear)
						m_dxr->CommandList()->ClearRenderTargetView(handle, cv.value.Color, 0, nullptr);
					handle.ptr += m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
				}
				//Zバッファの登録とクリア
				if (DSV.size() > 0) {
					auto handle = m_descHeapDSV->GetCPUDescriptorHandleForHeapStart();
					for (auto& p : DSV) {
						auto d = p.cv;
						if (d.clear)
							m_dxr->CommandList()->ClearDepthStencilView(handle, d.flags,
								d.value.DepthStencil.Depth, d.value.DepthStencil.Stencil, 0, nullptr);
						handle.ptr += m_dxr->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
					}
				}
			}

			//ビューポート指定
			m_dxr->CommandList()->RSSetViewports(1, &vp);
			m_dxr->CommandList()->RSSetScissorRects(1, &scissor);

			//レンダー
			if (m_type == PassType::postprocess) {
				//ポストプロセス
				m_dxr->CommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				D3D12_VERTEX_BUFFER_VIEW vbv = {};
				vbv.BufferLocation = m_dxr->PostProcessVB().res->GetGPUVirtualAddress();
				vbv.SizeInBytes = m_dxr->PostProcessVB().desc().Width;
				vbv.StrideInBytes = sizeof(PostProcessVertex);
				m_dxr->CommandList()->IASetVertexBuffers(0, 1, &vbv);
				m_dxr->CommandList()->DrawInstanced(4, 1, 0, 0);
			} else {
				//ラスタライザ
				for (int i = 0; i < m_rasterVBs.size(); i++) {
					m_dxr->CommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					D3D12_VERTEX_BUFFER_VIEW vbv = {};
					vbv.BufferLocation = m_rasterVBs[i]->res->GetGPUVirtualAddress();
					vbv.SizeInBytes = m_rasterVBs[i]->desc().Width;
					vbv.StrideInBytes = m_rasterVBs[i]->elemSize;
					D3D12_INDEX_BUFFER_VIEW ibv = {};
					ibv.BufferLocation = m_rasterIBs[i]->res->GetGPUVirtualAddress();
					ibv.SizeInBytes = m_rasterIBs[i]->desc().Width;
					ibv.Format = IndexFormatFromSize(m_rasterIBs[i]->elemSize);
					m_dxr->CommandList()->IASetVertexBuffers(0, 1, &vbv);
					m_dxr->CommandList()->IASetIndexBuffer(&ibv);
					m_dxr->CommandList()->DrawIndexedInstanced(m_rasterIBs[i]->desc().Width/m_rasterIBs[i]->elemSize, 1, 0, 0, 0);
				}
			}

			if (RTV.size() == 0) {
				//バックバッファの状態を戻す
				barrier = CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
				m_dxr->CommandList()->ResourceBarrier(1, &barrier);
			}
		}

	}

	std::wstring DescToStr(const wchar_t* name, const D3D12_RESOURCE_DESC & desc)
	{
		std::wstring ret = name;

		switch (desc.Dimension) {
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			ret += L"(Buf)\t\t";
			ret += std::format(L"width:{}, format:", desc.Width);
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			ret += L"(Tex2D)\t\t";
			ret += std::format(L"width:{}, height:{}, mip:{}, format:", desc.Width, desc.Height, desc.MipLevels);
			break;
		}
		ret += DXGIFormatToString(desc.Format);

		return ret;
	}

	std::wstring Pass::Check(std::wstring& resdesc, const std::wstring& okstr)
	{
		std::wstring err = L"";
	
		if (m_type == PassType::none) {
			err += L"not initialized pass ... need to call ComputePass() or RaytracingPass() or RasterizerPass() or PostProcessPass()\n";
		}

		for (size_t i = 0; auto & q : SRV) {
			for (size_t j = 0; auto & p : q) {
				if (p == nullptr) {
					err += std::format(L"SRV[{}][{}] is nullptr\n", i, j);
				} else {
					if ((p->caps & ResCaps::srv) == ResCaps::none) {
						err += std::format(L"{} in SRV[{}][{}]->caps is not match\n", p->Name().c_str(), i, j);
					}
					if (p->res == nullptr) {
						err += std::format(L"{} in SRV[{}][{}]->res is nullptr\n", p->Name().c_str(), i, j);
					} else {
						resdesc += std::format(L"SRV[{}][{}]|", i, j);
						resdesc += DescToStr(p->Name().c_str(), p->res->GetDesc());
						resdesc += L"\n";
					}
				}
				j++;
			}
			i++;
		}

		for (size_t i = 0; auto & p:RTV) {
			if (p.tex == nullptr) {
				err += std::format(L"RTV[{}].tex is nullptr\n", i);
			} else {
				if ((p.tex->caps & ResCaps::rtv) == ResCaps::none) {
					err += std::format(L"{} in RTV[{}].tex->caps is not match\n", p.tex->Name().c_str(), i);
				}
				if (p.tex->res == nullptr) {
					err += std::format(L"{} in RTV[{}].tex->res is nullptr\n", p.tex->Name().c_str(), i);
				} else {
					resdesc += std::format(L"RTV[{}]|", i);
					resdesc += DescToStr(p.tex->Name().c_str(), p.tex->res->GetDesc());
					resdesc += L"\n";
				}
			}
			i++;
		}

		for (size_t i = 0; auto & p:DSV) {
			if (p.tex == nullptr) {
				err += std::format(L"DSV[{}].tex is nullptr\n", i);
			} else {
				if ((p.tex->caps & ResCaps::dsv) == ResCaps::none) {
					err += std::format(L"{} in DSV[{}].tex->caps is not match\n", p.tex->Name().c_str(), i);
				}
				if (p.tex->res == nullptr) {
					err += std::format(L"{} in DSV[{}].tex->res is nullptr\n", p.tex->Name().c_str(), i);
				} else {
					resdesc += std::format(L"DSV[{}]|", i);
					resdesc += DescToStr(p.tex->Name().c_str(), p.tex->res->GetDesc());
					resdesc += L"\n";
				}
			}
			i++;
		}

		for (size_t i = 0; auto & p:UAV) {
			if (p.res == nullptr) {
				err += std::format(L"UAV[{}].res is nullptr\n", i);
			} else {
				if ((p.res->caps & ResCaps::uav) == ResCaps::none) {
					err += std::format(L"{} in UAV[{}].res->caps is not match\n", p.res->Name().c_str(), i);
				}
				if (p.res->res == nullptr) {
					err += std::format(L"{} in UAV[{}].res->res is nullptr\n", p.res->Name().c_str(), i);
				} else {
					resdesc += std::format(L"UAV[{}]|", i);
					resdesc += DescToStr(p.res->Name().c_str(), p.res->res->GetDesc());
					resdesc += L"\n";
				}
			}
			i++;
		}

		for (size_t i = 0; auto & p:CBV) {
			if (p == nullptr) {
				err += std::format(L"CBV[{}] is nullptr\n", i);
			} else {
				if ((p->caps & ResCaps::cbv) == ResCaps::none) {
					err += std::format(L"{} in CBV[{}]->caps is not match\n", p->Name().c_str(), i);
				}
				if (p->res == nullptr) {
					err += std::format(L"{} in CBV[{}]->res is nullptr\n", p->Name().c_str(), i);
				} else {
					resdesc += std::format(L"CBV[{}]|", i);
					resdesc += DescToStr(p->Name().c_str(), p->res->GetDesc());
					resdesc += L"\n";
				}
			}
			i++;
		}

		if (err == L"")
			return okstr;
		else
			return err;
	}

	/*
	void Pass::OutputResourceBinding(const wchar_t* filename)
	{
		std::wofstream ofs(filename);

		if (ofs.bad()) {
			Validate(E_FAIL, L"failed to write %s", filename);
			return;
		}

		int r=0, spc = 0;

		for (auto& p : SRV) {
			r = 0;
			for (auto& o : p) {
				switch (o->type) {
				case ResType::buf:
					ofs << Format(L"StructuredBuffer<> %s : register(t%d, space%d)\n", o->name.c_str(), r, spc);
					break;
				case ResType::tex2D:
					ofs << Format(L"Texture2D<float4> %s : register(t%d, space%d)\n", o->name.c_str(), r, spc);
					break;
				case ResType::tlas:
					ofs << Format(L"RaytracingAccelerationStructure %s : register(t%d, space%d)\n", o->name.c_str(), r,spc);
				}
				r++;
			}
			spc++;
		}

		r = 0;
		for (auto& o : UAV) {
			switch (o->type) {
			case ResType::buf:
				ofs << Format(L"RWStructuredBuffer<> %s : register(u%d)\n", o->name.c_str(), r);
				break;
			case ResType::tex2D:
				ofs << Format(L"RWTexture2D<float4> %s : register(u%d)\n", o->name.c_str(), r);
				break;
			}
			r++;
		}


		ofs.close();
	}
	*/

	// DescriptorHeaps(SRV,CBV,UAVのディクスリプタヒープ)へのポインタ
	//ID3D12DescriptorHeap* Pass::GetSrvHeap()
	//{
	//	return m_descHeap.Get();
	//}

	/**************************************************************************************
	/ Direct2D
	**************************************************************************************/

	//コンストラクタ、D3D11on12とD2Dの初期化
	D2D::D2D(DXR* _dxr)
	{
		DefaultLocale = L"ja-JP";
		dxr = _dxr;

		for (int i = 0; i < dxr->BackBufferCount(); i++)
			m_wrappers.push_back(RTWrapper(dxr,this));

		ComPtr<ID3D11Device> d3d11Device;
		ThrowIfFailed(D3D11On12CreateDevice(dxr->Device().Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,
			reinterpret_cast<IUnknown**>(dxr->CommandQueue().GetAddressOf()), 1, 0, &d3d11Device, &m_d3d11DeviceContext, nullptr ), L"D3D11On12CreateDevice failed", __FILEW__, __LINE__);
		ThrowIfFailed(d3d11Device.As(&m_d3d11On12Device), L"d3d11Device.As failed", __FILEW__, __LINE__);

		D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
		D2D1_FACTORY_OPTIONS d2dFactoryOptions = {};

		ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, &m_d2dFactory), L"D2D1CreateFactory failed", __FILEW__, __LINE__);
		ComPtr<IDXGIDevice> dxgiDevice;
		ThrowIfFailed(m_d3d11On12Device.As(&dxgiDevice), L"m_d3d11On12Device.As failed", __FILEW__, __LINE__);
		ThrowIfFailed(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice), L"m_d2dFactory->CreateDevice failed", __FILEW__, __LINE__);
		ThrowIfFailed(m_d2dDevice->CreateDeviceContext(deviceOptions, &m_d2dDeviceContext), L"m_d2dDevice->CreateDeviceContext failed", __FILEW__, __LINE__);
		ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dWriteFactory), L"DWriteCreateFactory failed", __FILEW__, __LINE__);
	}

	D2D::~D2D()
	{
		UnwrapBackBuffers();
	}

	void D2D::WrapBackBuffers()
	{
		for (int i = 0; i < dxr->BackBufferCount(); i++)
			m_wrappers[i].Wrap(dxr->BackBuffers(i));
	}

	void D2D::UnwrapBackBuffers()
	{
		for (int i = 0; i < dxr->BackBufferCount(); i++)
			m_wrappers[i].Unwrap();
	}

	ComPtr<IDWriteTextFormat> D2D::TextFormat(const wchar_t* fontFamily, float size, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, const wchar_t* locale)
	{
		const wchar_t* loc = (locale == nullptr) ? DefaultLocale.c_str() : locale;
		ComPtr<IDWriteTextFormat> tf;
		ThrowIfFailed(DWFactory()->CreateTextFormat(fontFamily, nullptr, weight, style, stretch, size, loc, tf.ReleaseAndGetAddressOf()), L"DWFactory()->CreateTextFormat failed", __FILEW__, __LINE__);
		return tf;
	}

	ComPtr<IDWriteTextLayout> D2D::TextLayout(const wchar_t* str, ComPtr<IDWriteTextFormat> format, float maxWidth, float maxHeight)
	{
		ComPtr<IDWriteTextLayout> tl;
		ThrowIfFailed(DWFactory()->CreateTextLayout(str, wcslen(str), format.Get(), maxWidth, maxHeight, tl.ReleaseAndGetAddressOf()),L"DWFactory()->CreateTextLayout failed", __FILEW__, __LINE__);
		return tl;
	}

	ComPtr<ID2D1SolidColorBrush>D2D::SolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES& props)
	{
		ComPtr<ID2D1SolidColorBrush>brush;
		ThrowIfFailed(DC()->CreateSolidColorBrush(color, props, brush.ReleaseAndGetAddressOf()),L"SolidColorBrush failed", __FILEW__, __LINE__);
		return brush;
	}

	ComPtr<ID2D1SolidColorBrush>D2D::SolidColorBrush(const D2D1_COLOR_F& color)
	{
		ComPtr<ID2D1SolidColorBrush>brush;
		ThrowIfFailed(DC()->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()), L"SolidColorBrush failed", __FILEW__, __LINE__);
		return brush;
	}

	ComPtr<ID2D1LinearGradientBrush> D2D::LinearGradientBrush(float ax, float ay, float bx, float by, const std::vector<D2D1_GRADIENT_STOP>& stops)
	{
		ComPtr<ID2D1GradientStopCollection> stopCollection;
		DC()->CreateGradientStopCollection(stops.data(), stops.size(), stopCollection.ReleaseAndGetAddressOf());

		ComPtr<ID2D1LinearGradientBrush>brush;
		ThrowIfFailed(DC()->CreateLinearGradientBrush(
			D2D1::LinearGradientBrushProperties( D2D1::Point2F(ax, ay), D2D1::Point2F(bx, by)),
			stopCollection.Get(), brush.ReleaseAndGetAddressOf()), L"CreateLinearGradientBrush failed", __FILEW__, __LINE__);
		
		return brush;
	}

	ComPtr<ID2D1RadialGradientBrush> D2D::RadialGradientBrush(float centerx, float centery, float offsetx, float offsety, float radiusx, float radiusy, const std::vector<D2D1_GRADIENT_STOP>& stops)
	{
		ComPtr<ID2D1GradientStopCollection> stopCollection;
		DC()->CreateGradientStopCollection(stops.data(), stops.size(), stopCollection.ReleaseAndGetAddressOf());

		ComPtr<ID2D1RadialGradientBrush>brush;
		ThrowIfFailed(DC()->CreateRadialGradientBrush(
			D2D1::RadialGradientBrushProperties(D2D1::Point2F(centerx, centery), D2D1::Point2F(offsetx, offsety), radiusx, radiusy),
			stopCollection.Get(), brush.ReleaseAndGetAddressOf()), L"CreateLinearGradientBrush failed", __FILEW__, __LINE__);

		return brush;
	}

	ComPtr<ID2D1BitmapBrush1> D2D::BitmapBrush(ComPtr<ID2D1Bitmap1> bmp)
	{
		ComPtr<ID2D1BitmapBrush1> brush;
		ThrowIfFailed(DC()->CreateBitmapBrush(bmp.Get(), brush.ReleaseAndGetAddressOf()), L"CreateBitmapBrush failed", __FILEW__, __LINE__);
		return brush;
	}

	ComPtr<ID2D1BitmapBrush1> D2D::BitmapBrush(ComPtr<ID2D1Bitmap1> bmp, const D2D1_BITMAP_BRUSH_PROPERTIES1& props)
	{
		ComPtr<ID2D1BitmapBrush1> brush;
		ThrowIfFailed(DC()->CreateBitmapBrush(bmp.Get(), props, brush.ReleaseAndGetAddressOf()), L"CreateBitmapBrush failed", __FILEW__, __LINE__);
		return brush;
	}


	ComPtr<ID2D1Bitmap1> D2D::CreateBitmap(const wchar_t* filename)
	{
		ComPtr<ID2D1Bitmap1>ppBitmap;

		ComPtr<IWICBitmapDecoder> pDecoder;
		ComPtr<IWICBitmapFrameDecode> pSource;
		ComPtr<IWICStream> pStream;
		ComPtr<IWICFormatConverter> pConverter;
		ComPtr<IWICBitmapScaler> pScaler;
		IWICImagingFactory *pFactory;

		ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)&pFactory), L"CoCreateInstance CLSID_WICImagingFactory failed", __FILEW__, __LINE__);

		ThrowIfFailed(pFactory->CreateDecoderFromFilename(filename,NULL,GENERIC_READ,WICDecodeMetadataCacheOnLoad, pDecoder.ReleaseAndGetAddressOf()), L"pFactory->CreateDecoderFromFilename failed", __FILEW__, __LINE__);
		ThrowIfFailed(pDecoder->GetFrame(0, &pSource), L"pDecoder->GetFrame failed", __FILEW__, __LINE__);
		ThrowIfFailed(pFactory->CreateFormatConverter(pConverter.ReleaseAndGetAddressOf()), L"pFactory->CreateFormatConverter failed", __FILEW__, __LINE__);
		ThrowIfFailed(pConverter->Initialize(pSource.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,NULL,0.f,WICBitmapPaletteTypeMedianCut),L"pConverter->Initialize failed", __FILEW__, __LINE__);
		ThrowIfFailed(DC()->CreateBitmapFromWicBitmap(pConverter.Get(),NULL,ppBitmap.ReleaseAndGetAddressOf()),L"CreateBitmapFromWicBitmap failed", __FILEW__, __LINE__);
		pFactory->Release();
		return ppBitmap;
	}


	Tex2D D2D::CreateCompatibleRT(int width, int height, DXGI_FORMAT fmt)
	{
		Tex2D tex = {};

		auto rd = CD3DX12_RESOURCE_DESC::Tex2D(fmt, width, height, 1, 1);
		rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
		auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		float cc[4] = { 0,0,0,1 };
		D3D12_CLEAR_VALUE cv = CD3DX12_CLEAR_VALUE(fmt, cc);
		ThrowIfFailed(dxr->Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, tex.state, &cv,
			IID_PPV_ARGS(tex.res.ReleaseAndGetAddressOf())), std::format(L"CreateCommittedResource failed"), __FILEW__, __LINE__);

		tex.caps = ResCaps::rtv | ResCaps::srv | ResCaps::d2d;
		tex.type = ResType::tex2D;

		return tex;
	}


	Tex2D D2D::CreateTextTexture(const wchar_t* str, const wchar_t* fontFamily, float fontSize, float maxWidth, float maxHeight, const wchar_t* locale, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch, D2D1_DRAW_TEXT_OPTIONS options)
	{

		auto tf = TextFormat(fontFamily, fontSize, fontWeight, fontStyle, fontStretch, locale);
		auto tl = TextLayout(str, tf.Get(), maxWidth, maxHeight);
		ComPtr<ID2D1SolidColorBrush> brush = SolidColorBrush(D2D1::ColorF(1, 1, 1, 1));
		
		//tf->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
		//tf->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);

		return CreateTextTexture(str, tf, tl, brush, options);
	}

	Tex2D D2D::CreateTextTexture(const wchar_t* str, ComPtr<IDWriteTextFormat>format, ComPtr<IDWriteTextLayout>layout, ComPtr<ID2D1Brush>brush, D2D1_DRAW_TEXT_OPTIONS options)
	{
		DWRITE_TEXT_METRICS tm;
		layout->GetMetrics(&tm);
		float W = min(tm.width, layout->GetMaxWidth());
		float H = min(tm.height, layout->GetMaxHeight());
		D2D1_RECT_F textRect = D2D1::RectF(0, 0, W, H);

		auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
		auto tex = CreateCompatibleRT(W, H, fmt);

		RTWrapper wrapper(dxr, this);
		wrapper.Wrap(tex);

		wrapper.Aquire();

		DC()->SetTarget(wrapper.bmp().Get());
		DC()->BeginDraw();
		DC()->Clear({ 0,0,0,0 });
		DC()->SetTransform(D2D1::Matrix3x2F::Identity());
		DC()->DrawTextW(str, wcslen(str), format.Get(), &textRect, brush.Get(), options);
		ThrowIfFailed(DC()->EndDraw(), L"EndDraw Failed", __FILEW__, __LINE__);
		
		wrapper.Release();

		D3DDC()->Flush();
		dxr->WaitForGPU();

		return tex;
	}

	void D2D::BeginDraw(RTWrapper& target)
	{
		m_RTisBB = false;
		m_targetWrapper = &target;
		target.Aquire();
		DC()->SetTarget(target.bmp().Get());
		DC()->BeginDraw();
	}

	void D2D::BeginDraw()
	{
		m_RTisBB = true;
		m_targetWrapper = nullptr;
		D3DDevice()->AcquireWrappedResources(CurrentBackBuffer().GetAddressOf(), 1);
		DC()->SetTarget(CurrentBackBufferBitmap().Get());
		DC()->BeginDraw();
	}

	void D2D::EndDraw()
	{
		ThrowIfFailed(DC()->EndDraw(), L"EndDraw Failed", __FILEW__, __LINE__);

		if (m_RTisBB) {
			m_wrappers[dxr->BackBufferIndex()].Release();
		} else {
			m_targetWrapper->Release();
		}
		D3DDC()->Flush();
		dxr->WaitForGPU();

	}


	void D2D::Print(const wchar_t* str, float x, float y, const wchar_t* fontFamily, float fontSize, const D2D_COLOR_F& color, HA halign, VA valign)
	{
		auto format = TextFormat(fontFamily, fontSize);
		auto brush = SolidColorBrush(color);
		auto rect = D2D1::RectF(x, y, dxr->Width(), dxr->Height());
		switch (halign) {
		case HA::left:
			break;
		case HA::center:
			format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			break;
		case HA::right:
			format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
		}

		switch (valign) {
		case VA::top:
			break;
		case VA::middle:
			format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			break;
		case VA::bottom:
			format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
			break;
		}

		//ハマった話 … 当初はBackBufferWrapperも公開していたが無くした経緯
		//DC()->SetTarget(CurrentBackBufferBitmap().Get());	//OK
		//DC()->SetTarget(CurrentBackBufferWrapper().bmp().Get());	//NG
		//2つのGet()で返ってくるアドレス自体は全く一緒
		//おそらく、SetTargetや一連の描画関数はCurrentBackBufferBitmap()で返るID2DBitmapにフラグか何かの書き込みも行っている
		//～Wrapper().bmp()ではダメなのは、何かの情報がSetTargetが終わった直後に消されてDrawTextまで残ってない？
		//こういうバグの温床になるのでBackBufferWrapperは隠すことにした
		
		BeginDraw();
		DC()->DrawTextW(str, wcslen(str), format.Get(), &rect, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
		EndDraw();
	}

	void D2D::Stamp(ComPtr<ID2D1Bitmap1> bmp, float x, float y)
	{
		BeginDraw();
		DC()->DrawBitmap(bmp.Get(), {x,y,x + bmp->GetPixelSize().width, y + bmp->GetPixelSize().height});
		EndDraw();
	}

	void D2D::Clear(const D2D_COLOR_F& color)
	{
		BeginDraw();
		DC()->Clear(color);
		EndDraw();
	}


	RTWrapper::~RTWrapper()
	{
		Unwrap();
	}

	void RTWrapper::Wrap(ComPtr<ID3D12Resource> d12res, bool renderTarget)
	{
		Unwrap();

		float dpi = GetDpiForWindow(dxr->hWnd());
		
		D2D1_BITMAP_PROPERTIES1 bitmapProperties;
		D3D11_RESOURCE_FLAGS d3d11Flags = {};

		//今分かっている範囲では
		//・D3D12のrenderTargetを対象にしたWrapのみできる
		//  非RenderTargetだとERRORが出るがGeForceでは一応普通に使えるっぽい多分Radeonだとダメだろう
		//・BirmapPropertyが↓の2つの組み合わせしか受けつけない
		//  DXGI_FORMATだけはUNKNOWNではなく他の値でもいいがメリットはなさそう
		if (renderTarget) {
			//D2DのレンダーターゲットにできるがD2Dから読み込めない
			bitmapProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
			d3d11Flags = { D3D11_BIND_RENDER_TARGET };
			d2d->D3DDevice()->CreateWrappedResource(d12res.Get(), &d3d11Flags, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(m_res.ReleaseAndGetAddressOf()));
		} else {
			//D2Dから読み込めるがD2Dのレンダーターゲットに出来ない
			bitmapProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_NONE,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
			d3d11Flags = { D3D11_BIND_SHADER_RESOURCE };
			d2d->D3DDevice()->CreateWrappedResource(d12res.Get(), &d3d11Flags, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(m_res.ReleaseAndGetAddressOf()));
		}

		ComPtr<IDXGISurface> surface;
		ThrowIfFailed(m_res.As(&surface), L"WrappedRT::res.As failed", __FILEW__, __LINE__);
		ThrowIfFailed(d2d->DC()->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties, m_bmp.ReleaseAndGetAddressOf()), L"WrappedRT::CreateBitmapFromDxgiSurface failed", __FILEW__, __LINE__);

		m_wrapping = true;
	}

	void RTWrapper::Wrap(Tex2D& rt, bool renderTarget)
	{
		m_wrappedRT = &rt;

		if ((rt.caps & (ResCaps::rtv | ResCaps::d2d)) != (ResCaps::rtv | ResCaps::d2d))
			ThrowIfFailed(E_FAIL, std::format(L"RTWrapper::Wrap() failed... Tex2D {} can't be wrapped, should be created by D2D::CreateCompatibleRT", rt.Name().c_str()), __FILEW__, __LINE__);

		Wrap(rt.res, renderTarget);
	}

	void RTWrapper::Unwrap()
	{
		if (!m_wrapping)
			return;

		m_wrappedRT = nullptr;
		d2d->DC()->SetTarget(nullptr);
		m_res.Reset();
		m_bmp.Reset();
		d2d->D3DDC()->Flush();
		dxr->WaitForGPU();		//Flushした後にGPUの完了待ちした方が無難らしい(リソースが掴まれたままの状態でオブジェクトが解放されたりする)
		m_wrapping = false;
	}

	void RTWrapper::Aquire()
	{
		if (!m_wrapping)
			return;
		
		//AcquireWrappedResourcesするとリソースステートはPRESENTにされてしまう(CreateWrappedResourceの指定通り)ので
		//リソースを直接ラップしている場合を除き、ラップしてるリソース構造体のメンバをそれに合わせる
		d2d->D3DDevice()->AcquireWrappedResources(m_res.GetAddressOf(), 1);
		
		if (m_wrappedRT) {
			//PRESENTでもCOMMONでも値自体は0で一緒
			m_wrappedRT->state = D3D12_RESOURCE_STATE_PRESENT;
		}
	}

	void RTWrapper::Release()
	{
		if (!m_wrapping)
			return;

		d2d->D3DDevice()->ReleaseWrappedResources(m_res.GetAddressOf(), 1);
	}


	/**************************************************************************************
	/ Poly Maker
	**************************************************************************************/

	namespace PM {
		using namespace YRZ::PM::Math;

		//移動
		void Translate(std::vector<Vertex>& vs, const DirectX::XMFLOAT3& V)
		{
			for (int i = 0; i < vs.size(); i++)
				vs[i].pos = vs[i].pos + V;
		}

		//回転
		void Rotate(std::vector<Vertex>& vs, const DirectX::XMFLOAT3& axis, float t, const DirectX::XMFLOAT3& O)
		{
			for (int i = 0; i < vs.size(); i++) {
				vs[i].pos = RotAxis(vs[i].pos-O, axis, t)+O;
				vs[i].normal = RotAxis(vs[i].normal, axis, t);
			}
		}

		void Scale(std::vector<Vertex>& vs, const DirectX::XMFLOAT3& scale, const DirectX::XMFLOAT3& O)
		{
			for (int i = 0; i < vs.size(); i++) {
				vs[i].pos = (vs[i].pos - O) * scale + O;
				vs[i].normal = Normalize(vs[i].normal / scale);
			}
		}

		void Transform(std::vector<Vertex>& vs, const DirectX::XMMATRIX& mtrx)
		{
			for (int i = 0; i < vs.size(); i++) {
				XMStoreFloat3(&vs[i].pos, XMVector3Transform(XMV(vs[i].pos), mtrx));
				XMStoreFloat3(&vs[i].normal, XMVector3TransformNormal(XMV(vs[i].normal), mtrx));
			}
		}

		//反転(planeに対する鏡像にする)
		void Flip(std::vector<Vertex>& vs, std::vector<UINT>& is, const DirectX::XMFLOAT4& plane)
		{
			for (int i = 0; i < vs.size(); i++) {
				DirectX::XMFLOAT3 P = vs[i].pos;
				DirectX::XMFLOAT3 N = xyz(plane);	//まず、法線の正規化
				float nf = Length(N);
				N = N / nf;
				float d = plane.w / nf;		//(X・N)+d = 0になる点の集合がplane上の点
				float u = -2 * (d + Dot(P, N));
				vs[i].pos = P + u * N;
				DirectX::XMFLOAT3 I = vs[i].normal;
				vs[i].normal = I - 2.0 * Dot(N, I) * N;
				vs[i].uv = vs[i].uv;
			}

			//面の右回り・左回りをキープする
			/*
			for (int i = 0; i < is.size()/3; i++) {
				int p = i * 3;
				is[p] = is[p];
				UINT tmp = is[p + 1];
				is[p+1] = is[p+2];
				is[p+2] = tmp;
			}
			*/
		}

		/*** UV の操作 ***/
		void TranslateUV(std::vector<Vertex>& vs, const DirectX::XMFLOAT2& V)
		{
			for (int i = 0; i < vs.size(); i++)
				vs[i].uv = vs[i].uv + V;
		}

		//回転(Oを中心としてaxis周りにtラジアン回転)
		void RotateUV(std::vector<Vertex>& vs, float t, const DirectX::XMFLOAT2& O)
		{
			for (int i = 0; i < vs.size(); i++)
				vs[i].uv = Rot(vs[i].uv - O, t) + O;
		}

		//スケール(Oを中心として各軸scale倍)
		void ScaleUV(std::vector<Vertex>& vs, const DirectX::XMFLOAT2& scale, const DirectX::XMFLOAT2& O)
		{
			for (int i = 0; i < vs.size(); i++)
				vs[i].uv = (vs[i].uv -O) * scale + O;
		}
		//一次変換
		void TransformUV(std::vector<Vertex>& vs, const DirectX::XMMATRIX& mtrx)
		{
			for (int i = 0; i < vs.size(); i++)
				XMStoreFloat2(&vs[i].uv, XMVector2Transform(XMV(vs[i].uv,0,0), mtrx));
		}

		//Nの方向から見て表向きの面をisに加える
		//twosidedがtrueの場合は両方加える
		void ParametricAddFace(std::vector<Vertex>& vs, std::vector<UINT>& is, UINT v0, UINT v1, UINT v2, DirectX::XMFLOAT3 N, bool twosided )
		{
			if (twosided) {
				is.push_back(v0);
				is.push_back(v1);
				is.push_back(v2);
				is.push_back(v0);
				is.push_back(v2);
				is.push_back(v1);
			} else {
				auto p0 = vs[v0].pos;
				auto p1 = vs[v1].pos;
				auto p2 = vs[v2].pos;
				if (Dot(Cross(p1 - p0, p2 - p0), N) >= 0) {
					is.push_back(v0);
					is.push_back(v1);
					is.push_back(v2);
				} else {
					is.push_back(v0);
					is.push_back(v2);
					is.push_back(v1);
				}
			}
		}

		//パラメトリック曲面をつくる
		void ParametricSurface(std::vector<Vertex>& vs, std::vector<UINT>& is, Vertex(uv2vertex(float u, float v)), int ucount, int vcount, bool smooth, bool alternate, bool twosided)
		{
			vs.clear();
			is.clear();

			if (smooth) {
				vs.resize((ucount + 1) * (vcount + 1));
				for (int y = 0; y <= vcount; y++) {
					float v = y / (float)vcount;
					for (int x = 0; x <= ucount; x++) {
						int idx = y * (ucount + 1) + x;
						float u = x / (float)ucount;
						vs[idx] = uv2vertex(u, v);
					}
				}

				if (twosided) 
					is.reserve(ucount * vcount * 12);
				else
					is.reserve(ucount * vcount * 6);

				for (int y = 0; y < vcount; y++) {
					for (int x = 0; x < ucount; x++) {
						int p = (y * ucount + x) * 6;
						int vp = y * (ucount + 1) + x;
						if (alternate && ((y + x) & 1)) {
							ParametricAddFace(vs, is, vp, vp + 1, vp + ucount + 1, vs[vp].normal, twosided);
							ParametricAddFace(vs, is, vp+1, vp+ucount+1, vp+ucount+2, vs[vp+ucount+2].normal, twosided);
						} else {
							ParametricAddFace(vs, is, vp, vp + 1, vp + ucount + 2, vs[vp+1].normal, twosided);
							ParametricAddFace(vs, is, vp, vp + ucount + 1, vp + ucount + 2, vs[vp + ucount + 1].normal, twosided);
						}
					}
				}
			} else {
				vs.resize(ucount * vcount * 4);
				for (int y = 0; y < vcount; y++) {
					float v0 = y / (float)vcount;
					float v1 = (y+1) / (float)vcount;
					float vc = (y + 0.5) / vcount;
					for (int x = 0; x < ucount; x++) {
						int p = (y * ucount + x) * 4;
						float u0 = x / (float)ucount;
						float u1 = (x+1) / (float)ucount;
						float uc = (x + 0.5) / ucount;

						DirectX::XMFLOAT3 N = uv2vertex(uc, vc).normal;
						vs[p] = uv2vertex(u0, v0);
						vs[p + 1] = uv2vertex(u1, v0);
						vs[p + 2] = uv2vertex(u0, v1);
						vs[p + 3] = uv2vertex(u1, v1);
						for (int i = 0; i < 4; i++) {
							vs[p + i].normal = N;
						}
					}
				}

				if (twosided)
					is.reserve(ucount * vcount * 12);
				else
					is.reserve(ucount * vcount * 6);

				for (int y = 0; y < vcount; y++) {
					for (int x = 0; x < ucount; x++) {
						int p = (y * ucount + x) * 6;
						int vp = (y * ucount + x) * 4;
						if (alternate && ((y + x) & 1)) {
							ParametricAddFace(vs, is, vp, vp + 1, vp + 2, vs[vp].normal, twosided);
							ParametricAddFace(vs, is, vp + 1, vp + 3, vp + 2, vs[vp + 3].normal, twosided);
						} else {
							ParametricAddFace(vs, is, vp, vp + 1, vp + 3, vs[vp + 1].normal, twosided);
							ParametricAddFace(vs, is, vp + 3, vp + 2, vp, vs[vp + 2].normal, twosided);
						}
					}
				}
			}
		}


		//-1～+1の範囲でxcount,ycount分割された四角ポリゴンを作る
		//面の向きはZ-とする
		void Plane(std::vector<Vertex>& vs, std::vector<UINT>& is, int xcount, int ycount, bool smooth, bool alternate, bool twosided)
		{
			ParametricSurface(vs, is,
				[](float u, float v) {
					Vertex V;
					V.pos = DirectX::XMFLOAT3(2*u-1, -2*v+1, 0);
					V.normal = DirectX::XMFLOAT3(0, 0, -1);
					V.uv = DirectX::XMFLOAT2(u,v);
					return V;
				}
			, xcount, ycount, smooth, alternate, twosided);
		}


		//半径1の正N角形
		void RegNGon(std::vector<Vertex>& vs, std::vector<UINT>& is, int n)
		{
			int C = n + 1;
			vs.resize(n + 2);
			vs[C].normal = { 0,0,-1 };
			vs[C].uv.x = 0.5;
			vs[C].uv.y = 0.5;
			vs[C].pos = { 0,0,0 };
			for (int i = 0; i <= n; i++) {
				float t = i * 2 * PI / n;
				float s = sin(t);
				float c = cos(t);
				vs[i].pos = { s, c, 0 };
				vs[i].normal = vs[C].normal;
				vs[i].uv.x = s / 2 + .5;
				vs[i].uv.y = 1 - (c / 2 + .5);
			}

			is.resize(n * 3);
			for (int i = 0; i < n; i++) {
				int idx = i * 3;
				is[idx] = i;
				is[idx + 1] = i+1;
				is[idx + 2] = C;
			}
		}


		void StarNGon(std::vector<Vertex>& vs, std::vector<UINT>& is, int n, float rOdd)
		{
			int n2 = n * 2 + 2;
			vs.resize(n2 + 1);
			DirectX::XMFLOAT3 N = { 0,0,-1 };
			vs[n2].normal = N;
			vs[n2].uv.x = 0.5;
			vs[n2].uv.y = 0.5;
			vs[n2].pos = { 0,0,0 };
			for (int i = 1; i < n2; i++) {
				float t = i * PI / n;
				float s = sin(t);
				float c = cos(t);
				float uvl = 1.0f;
				vs[i].pos = { s, c, 0 };
				if (i & 1) {
					uvl = rOdd;
					vs[i].pos = vs[i].pos * rOdd;
				}
				vs[i].normal = N;
				vs[i].uv.x = (s / 2 * uvl + .5);
				vs[i].uv.y = 1.0 - (c / 2 * uvl + .5);
			}

			is.resize(n2 * 3);
			for (int i = 0; i < n2; i++) {
				int idx = i * 3;
				is[idx] = i;
				is[idx + 1] = i + 1;
				is[idx + 2] = n2;
			}
		}


		void SweepNGon(std::vector<Vertex>& vs, std::vector<UINT>& is, DirectX::XMFLOAT3 V, bool smooth)
		{
			//元の頂点・インデクスの数
			int nVsrc = vs.size();
			int nIsrc = is.size();
			int gon = nVsrc - 2;	//元の扇形は何角形か

			//まず、底面の複製と反転
			vs.resize(nVsrc * 2 + gon * 4);
			is.resize(nIsrc * 2 + gon * 6);

			for (int i = 0; i < nVsrc; i++) {
				int p = i + nVsrc;
				vs[p] = vs[i];
				vs[p].normal = -vs[i].normal;	//法線反転
				vs[p].pos = vs[i].pos + V;		//掃引
			}

			for (int i = 0; i < nIsrc; i++) {
				is[i + nIsrc] = is[nIsrc - i - 1] + nVsrc;
			}

			//側面
			for (int i = 0; i < gon; i++) {
				int p = nVsrc * 2 + i * 4;

				vs[p].pos = vs[i].pos;
				vs[p + 2].pos = vs[i + nVsrc].pos;
				vs[p + 1].pos = vs[i + 1].pos;
				vs[p + 3].pos = vs[i + nVsrc + 1].pos;

				if (smooth) {
					vs[p + 2].normal = vs[p].normal = Normalize(vs[i].pos - vs[gon+1].pos);
					vs[p + 3].normal = vs[p + 1].normal = Normalize(vs[i + 1].pos - vs[gon+1].pos);
				} else {
					DirectX::XMFLOAT3 E = vs[p + 1].pos - vs[p].pos;
					vs[p].normal = Normalize(Cross(E, -V));
					vs[p + 3].normal = vs[p + 2].normal = vs[p + 1].normal = vs[p].normal;
				}

				vs[p + 2].uv.x = vs[p].uv.x = 1 - i / (float)gon;
				vs[p + 3].uv.x = vs[p + 1].uv.x = 1 - (i + 1) / (float)gon;
				vs[p + 1].uv.y = vs[p].uv.y = 0;
				vs[p + 3].uv.y = vs[p + 2].uv.y = 1;
			}

			for (int i = 0; i < gon; i++) {
				int p = nIsrc * 2 + i * 6;
				int pv = nVsrc * 2 + i * 4;
				is[p] = pv;
				is[p + 1] = pv + 2;
				is[p + 2] = pv + 1;
				is[p + 3] = pv + 2;
				is[p + 4] = pv + 3;
				is[p + 5] = pv + 1;
			}
		}

		void Prism(std::vector<Vertex>& vs, std::vector<UINT>& is, int n, bool smooth)
		{
			RegNGon(vs, is, n);
			SweepNGon(vs, is, { 0,0,1 });
			Rotate(vs, { 1,0,0 }, PI / 2);
			Translate(vs, { 0,1,0 });
		}

		void Sphere(std::vector<Vertex>& vs, std::vector<UINT>& is, int ucount, int vcount, bool smooth, bool alternate)
		{
			ParametricSurface(vs, is,
				[](float u, float v) {
					Vertex V;
					float t = v * PI;
					float p = u * 2 * PI;
					float st = sin(t), ct = cos(t), sp = sin(p), cp = cos(p);
					V.pos = { st * cp, ct, st * sp};
					V.normal = Normalize(V.pos);
					V.uv = { u,v };
					return V;
				}
			, ucount, vcount, smooth, alternate);
		}

		void Cone(std::vector<Vertex>& vs, std::vector<UINT>& is, int ucount, int vcount, bool smooth, bool alternate)
		{
			ParametricSurface(vs, is,
				[](float u, float v) {
					Vertex V;
					float t = u * 2 * PI;
					float st = sin(t), ct = cos(t);
					V.pos = DirectX::XMFLOAT3( ct*v, v, -st*v );
					DirectX::XMFLOAT3 T = { -st, 0, -ct };
					DirectX::XMFLOAT3 B = {  ct, 1, -st};
					V.normal = Normalize(Cross(T,B));
					V.uv = { u,v };
					return V;
				}
			, ucount, vcount, smooth, alternate);
			//逆コーン型を作ってからひっくり返す
			Rotate(vs, { 1,0,0 }, PI, { 0,0.5,0 });

			//底面
			std::vector<Vertex> v2;
			std::vector<UINT> i2;
			RegNGon(v2, i2, ucount);
			//Y-の面を向くようにひっくり返す(V方向にテクスチャも反転する)
			Rotate(v2, { 1,0,0 }, -PI/2);

			Join(vs, is, v2, i2);

		}


		//右回り(左手系の場合)になっていて、かつ同一平面上に並んでいると仮定して点群psから面を生成して追加する
		void AddFace(std::vector<Vertex>& vs, std::vector<UINT>& is, const DirectX::XMFLOAT3* ps, const std::vector<UINT>& pi)
		{
			int psize = pi.size();
			if (psize < 3)
				return;
			
			std::vector<Vertex> vx(psize);
			DirectX::XMFLOAT3 N = Normalize(Cross(ps[pi[0]] - ps[pi[1]], ps[pi[0]] - ps[pi[2]]));
			for (int i = 0; i < psize; i++) {
				Vertex V;
				V.pos = ps[pi[i]];
				V.normal = N;
				if (psize != 4) {
					float t = 2 * PI * i / psize;
					V.uv.x = sin(t)*0.5+0.5;
					V.uv.y = 1-(cos(t)*0.5+0.5);
				}
				vx[i] = V;
			}
			if (psize == 4) {
				vx[0].uv = { 0,0 };
				vx[1].uv = { 1,0 };
				vx[2].uv = { 1,1 };
				vx[3].uv = { 0,1 };
			}

			std::vector<UINT> ix((psize - 2) * 3);
			for (int i = 0; i < psize - 2; i++) {
				int p = i * 3;
				ix[p] = 0;
				ix[p + 1] = i + 1;
				ix[p + 2] = i + 2;
			}

			Join(vs, is, vx, ix);
		}

		void Tetrahedron(std::vector<Vertex>& vs, std::vector<UINT>& is)
		{
			DirectX::XMFLOAT3 p[4];
			const float a = sqrtf(8);
			const float b = sqrtf(3);
			//辺の長さを2にする
			float ratio = 2 * b / a;
			p[0] = DirectX::XMFLOAT3(0,1,0) * ratio;
			p[1] = DirectX::XMFLOAT3(0, -1 / 3., a/3) *ratio;
			p[2] = DirectX::XMFLOAT3(a / (2 * b), -1 / 3. , -a / 6) *ratio;
			p[3] = DirectX::XMFLOAT3(-p[2].x, -1 / 3., p[2].y) *ratio;

			vs.clear();
			is.clear();
			AddFace(vs, is, p, { 0,1,2 });
			AddFace(vs, is, p, { 0,3,1 });
			AddFace(vs, is, p, { 0,2,3 });
			AddFace(vs, is, p, { 1,3,2 });
		}

		void Cube(std::vector<Vertex>& vs, std::vector<UINT>& is)
		{
			DirectX::XMFLOAT3 p[8];
			p[0] = {  1, 1, 1 };
			p[1] = { -1, 1, 1 };
			p[2] = {  1,-1, 1 };
			p[3] = { -1,-1, 1 };
			p[4] = {  1, 1,-1 };
			p[5] = { -1, 1,-1 };
			p[6] = {  1,-1,-1 };
			p[7] = { -1,-1,-1 };

			vs.clear();
			is.clear();
			AddFace(vs, is, p, { 5,4,6,7 });
			AddFace(vs, is, p, { 4,0,2,6 });
			AddFace(vs, is, p, { 0,1,3,2 });
			AddFace(vs, is, p, { 1,5,7,3 });
			AddFace(vs, is, p, { 1,0,4,5 });
			AddFace(vs, is, p, { 7,6,2,3 });
		}

		void Octahedron(std::vector<Vertex>& vs, std::vector<UINT>& is)
		{
			DirectX::XMFLOAT3 p[6];
			p[0] = { 1, 0, 0 };
			p[1] = { -1, 0, 0 };
			p[2] = { 0, 1, 0 };
			p[3] = { 0,-1, 0 };
			p[4] = { 0, 0, 1 };
			p[5] = { 0, 0,-1 };

			vs.clear();
			is.clear();
			AddFace(vs, is, p, { 2,5,1 });
			AddFace(vs, is, p, { 2,0,5 });
			AddFace(vs, is, p, { 2,4,0 });
			AddFace(vs, is, p, { 2,1,4 });
			AddFace(vs, is, p, { 3,1,5 });
			AddFace(vs, is, p, { 3,5,0 });
			AddFace(vs, is, p, { 3,0,4 });
			AddFace(vs, is, p, { 3,4,1 });
		}

		//Daniel Sieger氏
		//https://www.danielsieger.com/blog/2021/01/03/generating-platonic-solids.html
		//を参考にさせていただきました
		void Icosahedron(std::vector<Vertex>& vs, std::vector<UINT>& is)
		{
			float phi = (1 + sqrtf(5)) /2; //黄金比
			float a = 1;
			float b = 1 / phi;
			DirectX::XMFLOAT3 p[13];

			p[1] = { 0, b, -a };
			p[2] = { b, a, 0 };
			p[3] = { -b, a, 0 };
			p[4] = { 0, b, a };
			p[5] = { 0, -b, a };
			p[6] = { -a, 0, b };
			p[7] = { 0, -b, -a };
			p[8] = { a, 0, -b };
			p[9] = { a, 0, b };
			p[10] = { -a, 0, -b };
			p[11] = { b, -a, 0 };
			p[12] = { -b, -a, 0 };

			vs.clear();
			is.clear();
			AddFace(vs, is, p, { 3, 2, 1 });
			AddFace(vs, is, p, { 2, 3, 4 });
			AddFace(vs, is, p, { 6, 5, 4 });
			AddFace(vs, is, p, { 5, 9, 4 });
			AddFace(vs, is, p, { 8, 7, 1 });
			AddFace(vs, is, p, { 7, 10,1 });
			AddFace(vs, is, p, { 12,11,5 });
			AddFace(vs, is, p, { 11,12,7 });
			AddFace(vs, is, p, { 10,6, 3 });
			AddFace(vs, is, p, { 6, 10,12 });
			AddFace(vs, is, p, { 9, 8, 2 });
			AddFace(vs, is, p, { 8, 9, 11 });
			AddFace(vs, is, p, { 3, 6, 4 });
			AddFace(vs, is, p, { 9, 2, 4 });
			AddFace(vs, is, p, { 10,3, 1 });
			AddFace(vs, is, p, { 2, 8, 1 });
			AddFace(vs, is, p, { 12,10,7 });
			AddFace(vs, is, p, { 8, 11,7 });
			AddFace(vs, is, p, { 6, 12,5 });
			AddFace(vs, is, p, { 11,9, 5 });
		}


		//https://qiita.com/ikiuo/items/f5905c353858fc43e597
		//@ikiuo氏"正多面体のデータを作る" を参考にさせていただきました
		void Dodecahedron(std::vector<Vertex>& vs, std::vector<UINT>& is)
		{
			vs.clear();
			is.clear();
			DirectX::XMFLOAT3 p[20] = {
				{ +0.000000, +0.000000, +1.000000},
				{ +0.666667, +0.000000, +0.745356},
				{ -0.333333, +0.577350, +0.745356},
				{ +0.745356, +0.577350, +0.333333},
				{+0.127322, +0.934172, +0.333333},
				{-0.333333, -0.577350, +0.745356},
				{-0.872678, +0.356822, +0.333333},
				{-0.872678, -0.356822, +0.333333},
				{+0.745356, -0.577350, +0.333333},
				{+0.127322, -0.934172, +0.333333},
				{+0.872678, +0.356822, -0.333333},
				{+0.872678, -0.356822, -0.333333},
				{-0.127322, +0.934172, -0.333333},
				{+0.333333, +0.577350, -0.745356},
				{-0.745356, +0.577350, -0.333333},
				{-0.745356, -0.577350, -0.333333},
				{-0.666667, +0.000000, -0.745356},
				{-0.127322, -0.934172, -0.333333},
				{+0.333333, -0.577350, -0.745356},
				{+0.000000, -0.000000, -1.000000}
			};
			AddFace(vs, is, p, { 2, 0, 1, 3, 4 });
			AddFace(vs, is, p, { 6, 7, 5, 0, 2 });
			AddFace(vs, is, p, { 5, 9, 8, 1, 0 });
			AddFace(vs, is, p, { 8, 11, 10, 3, 1 });
			AddFace(vs, is, p, { 10, 13, 12, 4, 3 });
			AddFace(vs, is, p, { 12, 14, 6, 2, 4 });
			AddFace(vs, is, p, { 14, 16, 15, 7, 6 });
			AddFace(vs, is, p, { 15, 17, 9, 5, 7 });
			AddFace(vs, is, p, { 17, 18, 11, 8, 9 });
			AddFace(vs, is, p, { 18, 19, 13, 10, 11 });
			AddFace(vs, is, p, { 19, 16, 14, 12, 13 });
			AddFace(vs, is, p, { 19, 18, 17, 15, 16 });
		}



		//結合
		void Join(std::vector<Vertex>& destvs, std::vector<UINT>& destis, const std::vector<Vertex>& srcvs, const std::vector<UINT>& srcis)
		{
			int dvcount = destvs.size();	//元々の追加先の頂点数
			int dicount = destis.size();
			int svcount = srcvs.size();
			int sicount = srcis.size();

			destvs.resize(dvcount + svcount);
			for (int i = 0; i < svcount; i++)
				destvs[dvcount + i] = srcvs[i];

			destis.resize(dicount + sicount);
			for (int i = 0; i < sicount; i++)
				destis[dicount + i] = srcis[i] + dvcount;
		}

		//鏡像
		void Fold(std::vector<Vertex>& vs, std::vector<UINT>& is, const DirectX::XMFLOAT4& plane)
		{
			std::vector<Vertex>vsf = vs;
			std::vector<UINT>isf = is;

			Flip(vsf, isf, plane);
			Join(vs, is, vsf, isf);
		}

	} // namespace PM

}//namespace YRZ

