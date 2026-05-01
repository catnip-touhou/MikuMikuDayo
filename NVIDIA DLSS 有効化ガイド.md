# MikuMikuDayo VR - NVIDIA DLSS 有効化ガイド

本ソフトウェアは、NVIDIA DLSS (Deep Learning Super Sampling) 3.5 に対応しています。

本ソフトウェア本体およびソースコードはMITライセンスで公開されていますが、NVIDIA DLSSの機能を利用するためのコアファイル（`nvngx_dlss.dll`）はNVIDIAのプロプライエタリ（独自の利用規約が適用される）ソフトウェアです。

そのため、ライセンスのクリーンさを保つ目的で、リリースアーカイブにはDLSSのDLLファイルを同梱しておりません。DLSS機能をご利用になる場合は、お手数ですが以下の手順に従ってご自身でDLLをダウンロードし、配置してください。

## 📥 1. DLLファイルのダウンロード

以下のいずれかのサイトから、DLSSのDLLファイルをダウンロードしてください。

**TechPowerUp DLSS Database（簡単）**

GPU-Zを開発・配布しているサイトであり、ドライバなども配布しているデータベースです。
1. 以下のURLにアクセスします。

   https://www.techpowerup.com/download/nvidia-dlss-dll/
3. 左側のリストから「NVIDIA DLSS DLL」の最新バージョンをダウンロードします。
4. ダウンロードしたZIPファイルを解凍すると `nvngx_dlss.dll` が入っています。

**公式：NVIDIA GitHub リポジトリ（少し複雑）**

開発者向けにNVIDIAが直接公開しているリポジトリです。
1. 以下のURLにアクセスします。

   https://github.com/NVIDIA/DLSS/releases
3. 最新のDLSS SDKのリリースを開き、ZIPファイル（`ngx_dlss_demo_windows.zip` 等）をダウンロードします。
4. ZIPファイルを解凍し、`/DLSS_Sample_App/bin/ngx_dlss_demo/` または `bin` フォルダ内にある `nvngx_dlss.dll` を見つけます。

## ⚙️ 2. DLLファイルの配置

取得した `nvngx_dlss.dll` を、MikuMikuDayoの実行ファイル（`.exe`）と同じフォルダにコピーしてください。

[フォルダ構成のイメージ]

 MikuMikuDayoVR/
 
　├─ MikuMikuDayo.exe  <-- 実行ファイル
  
　├─ nvngx_dlss.dll    <-- ここにコピーする！
  
　└─ その他のファイル...

---
**⚠️ 免責事項・ライセンスについて**
* `nvngx_dlss.dll` の著作権およびライセンスは NVIDIA Corporation に帰属します。同ファイルの使用にあたってはNVIDIAのソフトウェア使用許諾契約（EULA）が適用されます。
* DLSSを利用するには、対応するNVIDIA GeForce RTXシリーズのグラフィックボードが必要です。
