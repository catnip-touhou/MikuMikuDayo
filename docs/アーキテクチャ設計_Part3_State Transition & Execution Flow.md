# MikuMikuDayo VR アーキテクチャ設計書 (Part 3: State Transition & Execution Flow)

## 1. ドキュメントの目的
本ドキュメントは、**MikuMikuDayo VR（MMDayo）**（`Ver1.4.0-r261122` 時点）の動的な振る舞い（起動時のGPUモード判定・アプリケーション状態遷移、1フレームのメインループ実行順序、3ステップのボーン/IK/物理ソルブフロー、7ステージレンダリングパイプラインと `Subayai-S` 縮退PBR構造、VR180 6パネル合成、非同期録画、およびAI/外部プロセス連携フロー）を定義する。

---

## 2. アプリケーション状態遷移と起動モード決定（State Transition & GPU Mode）

MMDayoの実行状態は、起動時に一度だけ決定される起動モード（**通常モード `Normal Mode`** または **縮退モード `S Mode` (`g_SMode`)**）のもとで、`NowLoading`（非同期シーンロード中）、`Playing`（プレビュー再生中）、`Recording`（連番画像またはFFmpeg動画書き出し中）、および通常の `Editing`（編集・静止画収束待機モード）の4状態と、サブモードである `VR180 Mode`（`vrParam.enable`）および `AI Link Mode`（`g_cfg.aiLinkEnabled`）の組み合わせで制御される。

```mermaid
stateDiagram-v2
    [*] --> Startup : WinMain / Dayo2 起動
    Startup --> GpuModeDecide : config.json 読込 ＆ Dayo：：GpuMode：：Decide()
    GpuModeDecide --> Editing : DXR初期化 (!g_SMode) ＆ 初期プロジェクト構築完了\n(Normal Mode または S Mode [S mode] 確定)

    state "通常・再生・録画ステート" as MainStates {
        Editing : Editing (編集・静止画蓄積)\nAnimation = false, Recording = false
        Loading : NowLoading (段階的シーンロード)\nLoadDayoContext ステートマシン実行\n(Sモード時はDXRレンダラをSubayai-S/Previewへ自動代替)
        Playing : Playing (リアルタイム再生)\nAnimation = true, Recording = false
        RecordingState : Recording (連番PNG / FFmpeg動画出力)\nAnimation = true, Recording = true

        Editing --> Loading : .dayo ファイルを開く
        Loading --> Editing : 全モデル・エフェクト読込完了

        Editing --> Playing : Playボタン / Spaceキー
        Playing --> Editing : Stopボタン / Spaceキー

        Editing --> RecordingState : Recordボタン押下\n(AsyncSaver初期化 & フレーム開始位置へ移動)
        RecordingState --> Editing : 最終フレーム到達 / ESC中断\n(AsyncSaver.Finish & FFmpegクローズ)
    end

    state "並列稼働サブシステム" as SubSystems {
        AILinkSub : AI Link Server (TCP 127.0.0.1:9191)\nフレーム末尾で ProcessMCPCommands() 実行
        FXWatchSub : FXWatcher & FXShaderCache ホットリロード監視\n.fxdayo 本体および #include 先 (.deps記録) の更新検知時に自動再コンパイル
    end
```

### 2.1 起動時のGPU能力調査と起動モード決定フロー（`gpuModeDayo.h`）

HWレイトレーシング（DXR）ユニットを持たないGPU（GeForce GTX 10/16系、Radeon RX 5000以前、内蔵GPU等）でも単一バイナリで安全に起動できるよう、`Dayo2()` の初期化フェーズで `Dayo::GpuMode::Decide()` が実行される。

```mermaid
flowchart TB
    Start["Dayo2 起動: config.json 読込\n(renderMode: auto / dxr / s, targetGpuName)"] --> Enum["Dayo::GpuMode::EnumerateAdapters()\nDXGIハードウェアアダプタ列挙 & D3D12 FL12_0 確認"]
    Enum --> Classify["Classify() によるRTClass 3段階分類\n- HW: RaytracingTier >= 1.1 (かつGTX除外)\n- Emulated: Tier 1.0 または NVIDIA GTX (ドライバDXR)\n- None: DXR非対応\n+ D3D12_OPTIONS3 BarycentricsSupported 調査"]
    Classify --> Decide{"起動モード & 使用GPU決定\n(targetGpuName優先 / なければnormalCapable優先 + 最大VRAM)"}
    Decide -->|"Normal条件成立\n(auto時はHWのみ / dxr時はHW+Emulated)"| ModeNormal["通常モード (Mode::Normal, g_SMode = false)\n- タイトル: 'MikuMikuDayo VR'\n- YRZ::DXR(hWnd, true, gpuName)\n- 全レンダラ (BDPT, Subayai, Subayai-L, Subayai-S, Preview等) 列挙"]
    Decide -->|"Normal条件不成立 または renderMode == 's'"| ModeS["Sモード / 縮退モード (Mode::S, g_SMode = true)\n- タイトル: 'MikuMikuDayo VR [S mode]'\n- YRZ::DXR(hWnd, false, gpuName) + '-D DAYO_SMODE'\n- SModeRenderers (Preview, Subayai-S, colorbar) のみ列挙"]
    ModeNormal --> Bary{"adapter.barycentrics?"}
    ModeS --> Bary
    Bary -->|No| NoBary["CompileOption に '-D NO_BARYCENTRICS' を追加"]
    Bary -->|Yes| InitRen["Renderer::Setup() & MainLoop 開始"]
    NoBary --> InitRen
```

### 2.2 各状態におけるレンダリング・時間進行の違い
1. **`Editing`（編集モード）**:
   - カメラやボーン、ライトが動いていない間は `cb.iSample` が毎フレームインクリメントされ、パストレーシング（`BDPT` 等）やマルチサンプル蓄積型レンダラ（`Subayai` / `Subayai-L` / `Subayai-S` のソフトシャドウ・SSR・AO）がフレームを跨いで蓄積・収束する。
   - 視線変更やギズモ操作が行われると `cb.iSample = 0` にリセットされ、再蓄積が始まる。
2. **`Playing`（プレビュー再生モード）**:
   - 実時間タイマー（`fpsWatch`）および音声再生位置（`g_player`）に同期して `AnimFrame`（浮動小数点フレーム）が進行し、毎フレーム1サンプル（`iSample = 0`）でリアルタイム描画を行う。
3. **`Recording`（録画モード）**:
   - 実時間とは切り離された固定タイムステップで1フレームずつ進行する。
   - 指定された目標サンプル数（SPP: Samples Per Pixel）に達するまで同一フレームで `cb.iSample` を回して収束させた後、デノイズ・ポストプロセスを経て `AsyncSaver` へフレームを送出し、次のフレームへ進む。

---

## 3. メインループ（1フレーム）の全体実行フロー

`dayo.cpp` の `MainLoop` ラムダ関数は、毎フレーム以下の順序で処理を実行する。

```mermaid
sequenceDiagram
    participant App as YRZ::App / MainLoop (dayo.cpp)
    participant Phys as Physics / PoseSolver
    participant UI as ImGui Windows / Gizmo
    participant Ren as Renderer (g_dayo)
    participant Saver as AsyncSaver (g_saver)
    participant MCP as AILink / MCPHandler

    App->>App: 1. プロジェクト設定変更チェック (UpdateProjectSettings / NewProjectSettings)
    App->>App: 2. 非同期ロード進行 (NowLoading時は LoadDayo を1ステップ実行)
    App->>Ren: 3. FXWatcher::Poll() (.fxdayo 本体・#include依存 .deps のホットリロード監視)
    App->>App: 4. 再生・録画時のフレーム進行計算 (Frame / AnimFrame 更新)
    App->>Phys: 5. 3ステップ姿勢・物理更新 (Pre-Physics ➔ Bullet Step ➔ Post-Physics)
    App->>UI: 6. メインメニュー・各種ImGuiウィンドウ・タイムライン・3Dギズモ更新
    alt 通常レンダリング (vrParam.enable == false)
        App->>Ren: 7a. RenderEye(0, 0) ➔ g_dayo->Render() (1視点描画)
    else VR180 パノラマレンダリング (vrParam.enable == true)
        loop 左右の目 (eye = 0..1) × 6パネル (panel = 0..5) = 計12回
            App->>Ren: 7b. RenderEye(eye, panel) ➔ g_dayo->Render()
        end
    end
    opt Recording中 かつ 目標サンプル数(SPP)到達時
        App->>Saver: 8. g_saver.RequestSave() (非同期GPUリードバック & PNG/FFmpeg出力)
    end
    App->>MCP: 9. Dayo::AILink::ProcessFrame() ➔ ProcessMCPCommands() (AI/MCP要求処理)
    App->>App: 10. ImGui描画コマンド発行 & SwapChain Present
```

---

## 4. 3ステップ姿勢・IK・物理演算ソルブフロー

MMD互換のボーン・モーフ・IK・物理演算・付与（回転/移動付与）を破綻なく解決するため、MMDayoでは1フレームの姿勢更新を**3つのフェーズ**に分割して実行する。

```mermaid
flowchart LR
    subgraph Step1["Step 1: Pre-Physics (物理前ボーン・モーフ解決)"]
        S1A["SyncPhysicsSettings\n拡張物理・風・重力同期"]
        S1B["CameraSolver::Solve\nカメラ・照明・環境光補間"]
        S1C["PoseSolver::Solve (Pre)\nモーフ適用 ➔ 物理前ボーンFK/IK\n➔ キネマティック剛体をボーンへ追従"]
        S1A --> S1B --> S1C
    end

    subgraph Step2["Step 2: Bullet Physics Step"]
        S2A["UpdatePhysics (風力印加)\n& g_physics->Update(dt)\nBullet剛体シミュレーション実行"]
    end

    subgraph Step3["Step 3: Post-Physics (物理後ボーン・外部親解決)"]
        S3A["ダイナミック剛体結果を\n物理ボーンへ書き戻し"]
        S3B["物理後ボーンのFK/IK・付与解決\n＆ 外部親(Exp)トランスフォーム適用"]
        S3C["最終ボーン行列配列 (boneMat)\n＆ モーフ頂点オフセット確定"]
        S3A --> S3B --> S3C
    end

    Step1 --> Step2 --> Step3
```

### 物理ベイク処理（`BakePhysicsToKeys`）
- タイムラインツールやMCPツール `bake_physics` から物理ベイクが実行された場合、指定フレーム範囲（`start_frame` 〜 `end_frame`）を先頭から順番に上記3ステップでシミュレーションし、ダイナミック剛体によって動かされた物理ボーンのワールド/ローカル変換を逆算して、確定ボーンキーフレーム（`boneKeys`）へ書き込む。

---

## 5. 7ステージ レンダリングパイプライン（`Renderer::Render`）

`renDayo.h` の `Renderer::Render` は、通常モード（DXR有効）でもSモード（非DXR縮退モード）でも統一された**7ステージ**で実行される。

```mermaid
flowchart TB
    subgraph Stg1["Stage 1: コントローラ & 材質記述子解決"]
        C1["UpdateController & ResolveMatDescController\nFXControllerの現在値をシェーダ定数・材質パラメータへ反映"]
    end

    subgraph Stg2["Stage 2: スキニング・デフォーム & BLAS/TLAS更新"]
        C2A["skinPass (既定CS) または Deform .fxdayo 実行\n前フレーム頂点 vb[1-flip] 保持 & 現フレーム頂点 vb[flip] 計算"]
        C2B["UpdateBLAS() & UpdateTLAS()\n- 通常モード: 全モデルBLAS更新 & シーンTLAS構築\n- Sモード: BLAS/TLAS構築スキップ (ダミーTLAS dmyTlas 束縛)\n+ 統合StructuredBuffer転送 & peekabooPass (モデル/材質ピッキング)"]
        C2A --> C2B
    end

    subgraph Stg3["Stage 3: メインレンダラパス (DXR / ハイブリッド / 非DXR縮退PBR)"]
        C3["メインレンダラ .fxdayo 実行\n- 通常モード: BDPT / Subayai / Subayai-L / Subayai-S / Preview\n- Sモード: Preview / Subayai-S / colorbar\n出力: dxrout(生カラー), oidnBuf(Albedo/Normal), normalDepth,\ngbuffer1(含effectBits), gbuffer2, velocityMap, highResDepth"]
    end

    subgraph Stg4["Stage 4: AI デノイズステージ (Intel OIDN 2.5.0)"]
        C4A{"デノイズ有効判定\n(蓄積完了時 or 毎フレーム設定)"}
        C4B["OIDNIO 実行\n- Win32 Shared (同一GPU LUIDゼロコピー: CUDA/HIP/SYCL)\n- Linux Bridge (/dev/shm + TCP 47522)\n- CPU Fallback"]
        C4C["oidnPass 実行\nデノイズ結果をポストプロセス入力 ppOut へ転送"]
        C4A -->|有効| C4B --> C4C
        C4A -->|スキップ| C4C
    end

    subgraph Stg5["Stage 5: ポストプロセス & 超解像アップスケーラチェーン"]
        C5A["Low-Res ポストプロセス群 (ppIn ⇔ ppOut)\nアップスケーラより前に配置されたPPエフェクトを低解像度で実行"]
        C5B["Upscaler トリガー (m_upscalerModelPtr 到達時)\nSResolution::DispatchFSR2 (13コンテキスト) または DispatchDLSS3\n+ CS_UpScaleNormalDepth ➔ swapLowresToHighres フラグON"]
        C5C["High-Res ポストプロセス群 (ppIn_HighRes ⇔ ppOut_HighRes)\nアップスケーラより後に配置されたPP (FXLinker合成等) を最終解像度で実行"]
        C5A --> C5B --> C5C
    end

    subgraph Stg6["Stage 6: VR180 パノラマ合成 (vrParam.enable時のみ)"]
        C6["EquirectangularX_Inline.fxdayo 実行\nPanel 0 (Centor): Overwrite描画\nPanel 1..5 (Left/Right/Top/Buttom/ReCentor): ブレンド加算\n左目・右目を finalVRScreen / finalVRScreenImage にSide-by-Side配置"]
    end

    subgraph Stg7["Stage 7: 背景合成 & ビューポート/ファイル出力"]
        C7["backgroundPass ➔ dayoOut (UI表示用) & imageOut (保存用)\nscreenPass ➔ screenBMP (最終画面出力)"]
    end

    Stg1 --> Stg2 --> Stg3 --> Stg4 --> Stg5 --> Stg6 --> Stg7
```

### 5.1 Subayai（DXRハイブリッド）と Subayai-S（非DXR縮退PBR）のバッファ契約アーキテクチャ

Stage 3 で用いられる主力PBRレンダラ `Subayai` / `Subayai-L` と、Sモード対応の縮退版 `Subayai-S` は、**`ShadowMapR`（平行光源可視率）・`RTGI`（鏡面反射GI）・`EnvSpecular`（差し引くべきIBL鏡面反射成分）の3つの内部バッファ契約を共通化**することで、`MMD`（直接光＋IBL＋拡散SH）、`SSSSS`、`Compo`、`Acc`、`DENOISE` といった大部分のシェーダパスと材質定義（`subayairencon.pmx`, `subalight.pmx`, `m/*.txt`）をそのまま共有する設計となっている。

```mermaid
flowchart LR
    subgraph CommonPre["共通前段パス (Raster)"]
        S0["Shadow0..15 (スポットライト影)\n➔ NormalDepth1 / CopyNDG / MotionVec / ZPre"]
    end

    subgraph ShadowBranch["平行光源シャドウ生成 (ShadowMapR 出力)"]
        direction TB
        SR_DXR["Subayai / Subayai-L:\nShadowR パス (DXR Raytracing)\n円錐ソフトシャドウ & 半透明透過レイ"]
        SR_S["Subayai-S (非DXR):\nDirShadow0..2 + DirShadowTrans (3段CSM)\n➔ ShadowR_S パス (PCSS/PCF + 半透明着色/淡影)"]
    end

    subgraph CommonMid["共通中段パス (Raster / PostProcess)"]
        M1["AO (GTAO) ➔ MMD パス\n(DiffuseOut, SpecularOut[含IBL], EnvSpecular[IBL成分])\n➔ SSSSS_X / SSSSS_Y"]
    end

    subgraph ReflBranch["鏡面反射GI生成 (RTGI 出力)"]
        direction TB
        GI_DXR["Subayai / Subayai-L:\nRTGI パス (DXR Raytracing 1ray/px)\n+ VR_HOOK_HIT_* (反射先法線マップ / Ray Cones /\nSubayai-L 反射内 SS-VPL [REFLECT_VPL])"]
        GI_S["Subayai-S (非DXR):\n- 床平面反射: FloorReduce ➔ PlanarND/PlanarAO ➔ PlanarRefl\n  (WorkingFloor材質の床面でIBLを鏡像ラスタ結果に完全置換)\n- 画面空間反射: HiZ ➔ ReflectS パス\n  (SSRResidual による IBL反射との差分加算 + Skybox IBL)"]
    end

    subgraph CommonPost["共通後段パス (Compo / Acc / Denoise)"]
        P1["Compo パス: (Diffuse + Specular) + (RTGI - EnvSpecular)\n➔ Acc (フレーム蓄積) ➔ DENOISE ➔ PreVP"]
    end

    CommonPre --> ShadowBranch --> CommonMid --> ReflBranch --> CommonPost
```

- **VR独自拡張フックの分離（`SubayaiExtraHelper.hlsli`, `Subayai_L_Helper.hlsli`, `Subayai_L.libsonnet`）**:
  - 本家Subayaiの更新を取り込みやすくするため、VR版で拡張した処理（反射ヒット先での法線マップ適用・Ray Conesによるミップ選択、`Subayai-L` のSS-VPL・反射内VPL、FSR2/DLSS用 `VelocityMap` 生成、VRパノラマ面別 `PrevViewProjectionMatrix` 参照）は `VR_HOOK_*` マクロとして別ファイルに分離されている。

### 5.2 パイプライン上の重要設計ポイント
1. **ポストプロセス順序とアップスケーラの位置連動（Stage 5）**:
   - `g_edi.postprocessOrder` リスト内に配置されたアップスケーラモデル（`FSR2_Upscaler.pmx` または `DLSS3_5_Upscaler.pmx`）の位置が、そのまま**低解像度ポストプロセス領域**と**高解像度ポストプロセス領域**の境界線となる。
   - アップスケーラ実行時に `swapLowresToHighres` が切り替わり、後続のポストプロセスには自動的に高解像度バッファ（`ppIn_HighRes` / `ppOut_HighRes`）および拡大済み法線深度（`normalDepth_HighRes`）がバインドされる。
2. **FXLinkerによる32bitエフェクトマスク（`effectBits`）の伝搬**:
   - 各材質・各モデルの `effectBits` は Stage 3 のレンダリング時に `gbuffer1.z`（および `highResDepth`）へ書き込まれ、Stage 5 の `FXLinker` 自動生成コンポジットシェーダがビット演算によって「どのピクセルにどのポストプロセスを適用するか」を1パスで判定・合成する。
3. **VR180 6パネル・マルチFOV合成（Stage 6）**:
   - VR180モードでは、1眼あたり `Centor`（正面広角）、`Left`、`Right`、`Top`、`Buttom`、および正面中心部の解像度密度を高める `ReCentor`（狭角高精細）の計6パネルを透視投影でレンダリングし、`EquirectangularX_Inline.fxdayo` で正距円筒図法（Equirectangular 180°）へ重み付き合成する。
   - この際、FSR2のテンポラル履歴がパネル間で破綻しないよう、`fsr_viewidx = 1 + eye * 6 + panel`（1〜12）の独立したコンテキストが使用される。

---

## 6. 非同期録画・FFmpeg連携フロー（`AsyncSaver`）

録画（`Recording == true`）時のGPUリードバックボトルネックを解消するため、`asyncSaverDayo.h` の `AsyncSaver` は **8スロットのリングバッファ** と **ワーカースレッドプール** を用いたパイプライン並列処理を行う。

```mermaid
sequenceDiagram
    participant GPU as DX12 CommandQueue
    participant Main as メインスレッド (RequestSave)
    participant Ring as RingBuffer (DownloadSlot × 8)
    participant Worker as SaveWorker スレッド群
    participant Dest as 連番PNG / ffmpeg.exe パイプ

    Main->>Ring: 1. 空きスロット取得 (フェンス完了待ち WaitSlot)
    Main->>GPU: 2. imageOut / finalVRScreenImage ➔ ReadbackHeap へ CopyTextureRegion 発行
    Main->>GPU: 3. D3D12 Fence Signal 発行 & スロットをキューへ投入
    Note over Main: メインスレッドは即座に次フレームのレンダリングへ進む

    Worker->>Ring: 4. 完了済みスロットをポップ & ReadbackHeap を Map
    alt 連番PNG出力モード
        Worker->>Dest: 5a. マルチスレッドで stbi_write_png 実行 (フレーム番号順ファイル出力)
    else FFmpeg 動画出力モード (HAP / ProRes / AVI)
        Worker->>Dest: 5b. フレーム順序を整列し ffmpeg.exe の stdin パイプへ RAW RGBA 書き込み
    end
    Worker->>Ring: 6. Unmap & スロット解放
```

---

## 7. AI連携・外部プロセス通信フロー（MCP & AI中割り）

メニューバーの `AI Link` が `ON` の間、`mcpDayoSocket.h` のワーカースレッドが `127.0.0.1:9191` でJSON-RPC 2.0リクエストを受け付ける。DirectX 12リソースや `PoseSolver` への競合を防ぐため、**原則としてすべてのシーン読み書きはメインスレッド末尾の `ProcessMCPCommands()` で同期実行**される（ただし読み取り専用のキャッシュ照会のみ `g_fastResponder` で即時応答可能）。

```mermaid
sequenceDiagram
    participant Agent as 外部プロセス\n(mmdayo_mcp.exe / DayoLink)
    participant Sock as Socket Worker Thread\n(mcpDayoSocket.h :9191)
    participant Queue as MCPCommandQueue\n(g_mcpQueue)
    participant Main as Main Thread\n(ProcessMCPCommands)
    participant Engine as Renderer / Solver / KeyFrame

    Agent->>Sock: 1. TCP接続 & 1行JSON-RPC送信 (例: tools/call, nakawari/apply_vmd)
    alt FastResponder対象 (nakawari/get_selection キャッシュヒット時)
        Sock-->>Agent: 2a. メインスレッドを待たずスナップショットから即時JSON応答
    else 通常のMCPツール・リソース・書き込み要求
        Sock->>Queue: 2b. MCPCommand を g_mcpQueue へ Push
        Note over Main: フレーム描画完了後、Dayo::AILink::ProcessFrame() 到達
        Main->>Queue: 3. TryPop() でコマンド取り出し (1フレーム最大8件または8msバジェット)
        Main->>Main: 4. MCP::Protocol::Adapt()\n(スキーマ検証 ValidateToolArguments & ResponseGuard セット)
        Main->>Engine: 5. ツール処理実行\n(キー操作 / 物理ベイク / get_rendered_view / Nakawari VMD差分適用)
        Engine-->>Main: 6. 実行結果 JSON (+ Read系は RoundNumbers で小数4桁丸め)
        Main->>Sock: 7. ClientConnection 経由で応答送信 & 切断
        Sock-->>Agent: 8. JSON-RPC 2.0 レスポンス受信
    end
```

### 7.1 MCP（Model Context Protocol）パイプラインの安全機構
1. **1フレームあたりの実行バジェット制御**:
   - 大量のコマンドが一度に届いてもGUIがフリーズしないよう、`ProcessMCPCommands()` は1フレームあたり最大8コマンドまたは経過時間8msでループを切り上げ、残りを次フレームへ持ち越す（ただし `get_rendered_view` のような重いコマンドも最低1件は必ず処理する）。
2. **`ResponseGuard` によるデッドロック防止**:
   - ハンドラ内で例外や早期リターンが発生しても、スコープ脱出時に必ずエラーレスポンスを返してソケットを閉じるため、クライアント側（`mmdayo_mcp.exe`）が無限待ちに陥らない。
3. **`get_rendered_view` によるマルチモーダル視覚フィードバック**:
   - AIが現在のビューポート画像を要求すると、`g_dayo->dayoOut`（VR時は `finalVRScreen`）からGPUリードバックを行い、`mcpDayoImage.h` で指定解像度へ縮小＋PNG/JPEG Base64エンコードして返却する。これによりAIは自身のポーズ付けやライティング結果を視覚的に確認しながら自律修正できる。

### 7.2 AI中割り（`mcpDayoNakawari.h` / `DayoLink`）の差分VMD連携フロー
1. **選択状態の常時スナップショット配信（`PublishSnapshot`）**:
   - 毎フレーム末尾で、現在選択中のモデル名・ボーン名・選択フレーム区間・キーフレーム数がスレッドセーフなスナップショットにコピーされる。これにより外部の `MMD-AINakawari`（0.6.0 / 0.7.0 両対応）は、MMDayoが重いレンダリング中であっても `nakawari/get_selection` をミリ秒単位でポーリングできる。
2. **一時VMDエクスポート（`nakawari/request_vmd`）**:
   - 対象モデルのボーン・モーフ・IK状態をローカルの一時VMDファイルへ書き出し、そのファイルパスと選択フレーム範囲を `MMD-AINakawari` へ返す。
3. **差分のみのスマートインポートと1ステップUndo（`nakawari/apply_vmd`）**:
   - AIが推論した中割り済みVMDを受け取ると、変更があったボーントラックのみを特定し、指定フレーム区間（`[start_frame, end_frame]`）の既存キーを消去して新しいキーフレームを挿入する。
   - この際、影響を受ける全ボーントラックのUndo記録に `pack = 1`（最終トラックに `pack = 2`）を付与することで、何十個のボーンがAIによって中割りされても、ユーザーは **`Ctrl+Z` 1回でAI適用前の状態へ完全にロールバック**できる。
