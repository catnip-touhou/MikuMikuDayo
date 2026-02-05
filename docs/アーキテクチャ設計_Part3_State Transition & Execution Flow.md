# MikuMikuDayo Architecture Document
## Part 3: State Transition & Execution Flow

**Target**: Application Lifecycle, Main Loop, and Update Pipeline

---
このパートでは、アプリケーションがどのように時間を管理し、入力を受け取り、毎フレームの描画へ至るかの「動的」な振る舞いを可視化します。特に、MMD互換ソフトとして重要な 「モーション更新と物理演算の順序」 や 「非同期保存の仕組み」 に焦点を当てています。

## 1. Application State Machine
MikuMikuDayoは、主に「編集(Edit)」「再生(Play)」「出力(Export)」の3つの主要な状態を持つ。
これらは `dayo.cpp` 内のフラグ（`Animation`や`Recording` 等）や `AsyncSaver` の状態によって管理される。

```mermaid
stateDiagram-v2
    [*] --> EditMode : Launch

    state EditMode {
        [*] --> Idle
        Idle --> GizmoDrag : User Input (Gizmo)
        GizmoDrag --> Idle : Release
        Idle --> Scrubbing : Timeline Seek
        Scrubbing --> Idle : Release
    }

    state PlayMode {
        [*] --> Simulating
        Simulating --> PhysicsCalc : Step Simulation
        PhysicsCalc --> Simulating
    }

    state ExportMode {
        [*] --> Rendering
        Rendering --> EnqueueFrame : GPU Readback
        EnqueueFrame --> Rendering : Next Frame
    }

    %% Transitions
    EditMode --> PlayMode : Play Button / P Key
    PlayMode --> EditMode : Pause / Stop
    
    EditMode --> ExportMode : Video/Image Export
    ExportMode --> EditMode : Finish / Cancel
```

## 2. Main Loop Execution Flow (dayo.cpp)
Windowsメッセージ処理から描画までの1フレームの処理フロー。 ImGui のImmediate Mode GUIパラダイムに基づき、ロジック更新とUI構築が密接に関係している。

```mermaid
flowchart TD
    Start((Frame Start)) --> WinMsg[Win32 Message Pump]
    WinMsg --> ImGui_New[ImGui::NewFrame]
    
    subgraph Update_Phase [Update Phase]
        ImGui_New --> Input[Handle Inputs<br/>（Keyboard/Mouse/Gizmo）]
        Input --> AppLogic[App Logic<br/>（Seek/Undo/Edit）]
        
        AppLogic --> IsPlaying{Is Playing?}
        IsPlaying -- Yes --> TimeStep[Advance Frame Time]
        IsPlaying -- No --> StaticTime[Keep Current Frame]
        
        TimeStep --> ModelUpd[Model::Update<br/>（Motion & Physics）]
        StaticTime --> ModelUpd
    end

    subgraph Render_Phase [Render Phase]
        ModelUpd --> ScnRender[Ren::RenderScene<br/>（ShadowMap & MainPass）]
        ScnRender --> UI_Draw[Draw UI Windows<br/>（Timeline/Properties）]
        UI_Draw --> D3D_Pres[D3D12 Present]
    end

    subgraph Async_Export [Async Export Pipeline]
        D3D_Pres -.-> Readback[GPU Readback Heap]
        Readback --> SaverThread[AsyncSaver Thread]
        SaverThread --> FileIO[Disk Write / FFmpeg Pipe]
    end

    D3D_Pres --> Start

```

## 3. Detailed Model Update Pipeline
1フレーム内でのキャラクター姿勢計算の詳細順序。 MMD互換の挙動を実現するため、「VMD適用 → IK → 物理演算 → 物理適用」 の順序が厳密に定義されている。

```mermaid
%%{init: {
  'theme': 'base'
}}%%
sequenceDiagram
    participant App as MainLoop
    participant KC as KeyController
    participant Bone as Bone/Morph
    participant Phys as BulletPhysics
    participant Ren as Renderer

    Note over App, Ren: Frame N (Time T)

    App->>KC: UpdatePose(Frame N)

    rect rgb(200, 220, 240)
        Note right of KC: 1. Animation Interpolation
        KC->>KC: Search Keyframes (Lower/Upper bounds)
        KC->>KC: Calculate Interpolation (Bezier)
        KC->>Bone: Apply Local Position/Rotation (VMD)
        KC->>Bone: Apply Morph Weights
    end

    rect rgb(240, 220, 200)
        Note right of KC: 2. Pre-Physics Alignment
        KC->>Bone: Update Global Matrix (Parent->Child)
        KC->>Bone: Solve IK (Inverse Kinematics)
        KC->>Bone: Update Global Matrix (Post-IK)
    end

    rect rgb(220, 240, 200)
        Note right of KC: 3. Physics Simulation
        KC->>Phys: Synchronize RigidBody to Bone (Kinematic)
        KC->>Phys: stepSimulation(deltaTime)
        Phys->>Phys: Collision & Constraint Solve
        Phys->>KC: Apply RigidBody Transform to Bone (Dynamic)
    end

    rect rgb(240, 240, 240)
        Note right of KC: 4. Final Transform
        KC->>Bone: Update Global Matrix (Post-Physics)
        KC->>Bone: Append Accessory/Camera Matrices
    end

    App->>Ren: Render(Models)

```

## 4. Execution Components Description

### 4.1 Update Logic (`dayo.cpp`, `PMXLoader.ixx`)
* **Time Management**: `g_player` が再生時間を管理し、FPS（通常30または60）に基づいたデルタタイムを供給する。
* **Input Handling**: ImGuiの `IsItemActive()` や `IsMouseClicked()` を判定し、3Dビューポート上の操作（ギズモ操作）か、UI上の操作（スライダー操作）かを区別してイベントを発行する。
* **Physics Sync**:
    * **Kinematic (ボーン追従)**: アニメーションで動くボーンの位置を、剛体（RigidBody）に反映させる。
    * **Dynamic (物理演算)**: Bullet Physicsの計算結果を、ボーンの位置に書き戻す。

### 4.2 Rendering Pipeline (`renDayo.h`, `YRZFx.ixx`)
1.  **Shadow Pass**: 光源からの深度マップを生成（ShadowMap）。
2.  **Z-PrePass**: (Optional) 深度バッファのみを先行描画し、オーバードロードを軽減。
3.  **Main Pass**:
    * 背景 (Skybox/Grid)
    * モデル描画 (PMX): 材質ごとにマテリアル設定（`.fx` / `.fxdayo`）を切り替えて描画。
4.  **UI Pass**: ImGuiのドローリストをオーバーレイ描画。

### 4.3 Async Export System (`asyncSaverDayo.h`)
メインスレッド（UI/描画）を停止させずに高解像度出力を行う仕組み。

* **GPU Readback**: `ID3D12Resource` の `CopyTextureRegion` を使用し、レンダーターゲットの内容をReadbackヒープ（CPUから読めるメモリ）へ転送。
* **Worker Thread**: メインスレッドとは別のスレッド(`std::thread worker`)が待機。
* **Pipeline**:
    1.  Main Thread: `jobs` キューに読込タスクをPushし、GPUコマンドを発行。
    2.  Worker Thread: `cv_push` で起床し、GPU完了済みリソースからデータを読み出す。
    3.  Output: PNG保存、または `_popen` で開いたFFmpegパイプへ `fwrite` する。
