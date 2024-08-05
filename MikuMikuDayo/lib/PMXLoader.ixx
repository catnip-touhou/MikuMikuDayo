module;

//【このファイルをビルドするにあたって、必要な準備】
//DirectXTk12のSimpleMathとBullet3が必要です
//
//【1】DirectXTk12
//⇒DirectXTk12のGithub
//  https://github.com/microsoft/DirectXTK12
//
//↑のつかいかた
//①https://github.com/microsoft/DirectXTK12/tree/main/SrcのSimpleMath.cppとSimpleMath.hをダウンロード
//②SimpleMath.hをインクルードファイルパスの通っている所に置く
//③ソリューションエクスプローラでSimpleMath.cppをプロジェクトのソースファイルに追加する
//
//【2】 Bullet3
//⇒Bullet3のGithub
//  https://github.com/bulletphysics/bullet3
//
//↑のつかいかた
//①git cloneで適当なフォルダ(ここではx:\bullet3とします)にclone
//②ソリューションエクスプローラでx:\bullet3\src\*.cpp, *.hをプロジェクトのソースファイルに追加する
//③プロジェクト -> プロジェクトのプロパティ -> C/C++ -> 全般 -> 追加のインクルードディレクトリ に x:\bullet3\src を追加
//④プロジェクト -> プロジェクトのプロパティ -> C/C++ -> プリプロサッサ で BT_USE_SSE_IN_API マクロを追加


//STL
#include <iostream>
#include <iterator>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

//DirectX
#include <DirectXMath.h>

//外部ライブラリ(Bullet)
#include <btBulletCollisionCommon.h>
#include <btBulletDynamicsCommon.h>

//外部ライブラリ(SimpleMath)
#include "SimpleMath.h"


// PMXLoader 
//
// 
// 参考文献
//
// PMXファイルフォーマット
// 極北P氏 PMXeditor付属 PMX仕様.txt
//
// VMDファイルフォーマット
// 針金P氏 "VMDメモ"
// https://hariganep.seesaa.net/article/201103article_1.html
// 
// Rodolphe Vaillant氏 "Cyclic Coordonate Descent Inverse Kynematic (CCD IK)"
// http://rodolphe-vaillant.fr/entry/114/cyclic-coordonate-descent-inverse-kynematic-ccd-ik 
//
// Johnathon Selstad氏 "Inverse Kinematics"
// https://zalo.github.io/blog/inverse-kinematics/
//
// benikabocha氏 "saba"
// https://github.com/benikabocha/saba
//
// edvakf氏 "MMD on WebGL カメラとライトと表情のモーションに対応（あと補間曲線について）"
// https://edvakf.hatenadiary.org/entry/20111016/1318716097


//TODO :
// 物理、IKのon/offをキーフレームで切り替えられるようにする
// 物理後変形ボーン対応
// 名前が15バイト以上のボーンにvmdのキーフレームがマッチしない問題
// 材質モーフ



export module PMXLoader;


export namespace PMX {
	using namespace DirectX;

	class PMXException : public std::runtime_error
	{
	private:
		std::string message;
	public:
		PMXException(const std::string& msg) : runtime_error(msg) {};
	};

	struct PMXVertex {
		XMFLOAT3 position = {};
		XMFLOAT3 normal = {};
		XMFLOAT2 uv = {};
		XMFLOAT4 extra_uv[4] = {};
		int weightType = 0;	//ボーン変形方式 0:BDEF, 1:BDEF2, 2:BDEF4, 3:SDEF
		int bone[4] = {};	//ボーンインデックス
		float weight[4] = {};//ボーンウェイト
		XMFLOAT3 sdef_c = {};
		XMFLOAT3 sdef_r0 = {};
		XMFLOAT3 sdef_r1 = {};
		float edge = 1.0;	//エッジ倍率
	};

	struct PMXMaterial {
		std::wstring name;
		std::wstring name_e;
		XMFLOAT4 diffuse = {};
		XMFLOAT3 specular = {};
		float shininess = 0;
		XMFLOAT3 ambient = {};
		int drawFlag = 0;	//描画ﾌﾗｸﾞ 0x01:両面, 0x02:地面影, 0x04:セルフシャドウキャスターになる, 0x08:セルフシャドウ描画, 0x10:エッジ描画
		XMFLOAT4 edgeColor = {};
		float edgeSize = 0;
		int tex = 0;	//テクスチャ番号
		int spTex = 0;
		int spmode = 0;	//スフィアモード 0:無効 1:乗算, 2:加算 3:サブテクスチャ
		int toonFlag = 0;	//共有Toonフラグ 0:継続値は個別Toon 1:継続値は共有Toon
		int toonTex = 0;	//テクスチャインデックス、または共有toonテクスチャ番号(0～9がtoon01.bmp～toon10.bmpに対応)
		int vertexCount = 0;	//頂点数(必ず3の倍数)
	};

	struct PMXIKLink {
		int bone = 0;
		int isLimit = 0;	//角度制限(bool)
		XMFLOAT3 low = {}, hi = {};	//角度制限の下限/上限
	};

	struct PMXBone {
		std::wstring name;
		std::wstring name_e;
		XMFLOAT3 position = {};
		int parent = -1;//親ボーン番号
		int level = 0;	//変形階層
		int flag = 0;	//ボーンフラグ(PMX仕様参照のこと)
		XMFLOAT3 toOffset = {};	//座標オフセット
		int toBone = -1;	//接続先ボーン
		int appendParent = -1;	//付与親ボーン
		float appendRatio = 1.0f;	//付与率
		XMFLOAT3 fixAxis = {};	//固定軸
		XMFLOAT3 localAxisX = {}, localAxisZ = {};	//ローカル軸X,Z
		int	externalKey = -1;//外部親キー
		int IKTarget = -1;	//IKターゲット
		int IKLoopCount = 0;//IKループ回数
		float IKAngle = 0;	//IKループ計算時の1回あたりの制限角度
		int IKLinkCount = 0;//IKリンク数
		std::vector<PMXIKLink>IKLinks;

		bool IsIK() const { return flag & 0x0020; }				//IKのゴール
		bool IsAppendRotation() const { return flag & 0x0100; }	//回転付与される
		bool IsAppendTranslation() const { return flag & 0x0200; }//移動付与される
		bool IsAfterPhysics() const { return flag & 0x1000; }		//物理後変形
		bool IsExternal() const { return flag & 0x2000; }			//外部親変形

		//PMXの仕様にはあるが本家MMDが対応していない属性
		bool IsLocalAppend() const { return flag & 0x0080; }		//ローカル付与

		//PMXe、MMDなどでの編集の時のみ使われる属性
		bool IsLocalFrame() const { return flag & 0x0800; }		//ローカル軸
		bool IsFixAxis() const { return flag & 0x0400; }			//軸固定
		bool IsRotation() const { return flag & 0x0002; }			//各種操作を行う事が出来るか？
		bool IsTranslation() const { return flag & 0x0004; }
		bool IsVisible() const { return flag & 0x0008; }
		bool IsControllable() const { return flag & 0x0010; }
	};

	struct PMXMorph {
		std::wstring name;
		int kind = 0;			//モーフの種類 0:グループ, 1:頂点, 2:ボーン, 3:UV, 4:追加UV1, 5:追加UV2, 6:追加UV3, 7:追加UV4, 8:材質
		int offsetCount = 0;	//オフセットの数
		void* offsets = nullptr;		//オフセットデータ,解放はPMXModel側でやる
	};

	//頂点モーフのオフセット
	struct PMXVertexMorphOffset {
		int vertex = 0;
		XMFLOAT3 offset = {};
	};

	//UVモーフのオフセット
	struct PMXUVMorphOffset {
		int vertex = 0;
		int iuv = 0;	//どのUVに対するオフセットか？0が基本UV、1～4で追加UV1～4
		XMFLOAT4 offset = {};
	};

	//ボーンモーフのオフセット
	struct PMXBoneMorphOffset {
		int bone = 0;
		XMFLOAT3 translation = {};
		XMFLOAT4 rotation = {};
	};

	//材質モーフのオフセット
	struct PMXMaterialMorphOffset {
		int material = 0;	//対象材質。-1の時は「全材質対象」
		int op = 0;	//演算方式 0:乗算 1:加算
		XMFLOAT4 diffuse = {};
		XMFLOAT3 specular = {};
		float shininess = 0;
		XMFLOAT3 ambient = {};
		XMFLOAT4 edgeColor = {};
		float edgeSize = 0;
		XMFLOAT4 tex = {};
		XMFLOAT4 spTex = {};
		XMFLOAT4 toon = {};
	};

	//グループモーフのオフセット
	struct PMXGroupMorphOffset {
		int morph = 0;
		float ratio = 0;
	};


	struct PMXNodeItem {
		bool isBone = false;	//この表示枠はボーン用
		bool isMorph = false;	//この表示枠はモーフ用
		int index = 0;		//この表示枠に割り当てられたボーン/モーフ番号
	};

	//表示枠
	struct PMXNode {
		std::wstring name;		//枠名
		std::wstring name_e;	//枠名(英語)
		int flag = 0;	//特殊枠フラグ 0:通常枠 1:特殊枠
		std::vector<PMXNodeItem> items;
	};

	//剛体
	struct PMXBody {
		std::wstring name;
		std::wstring name_e;
		int bone = -1;
		int group = 0;			//自分の所属している衝突グループ(0-15)
		unsigned short passGroup = 0;	//非衝突グループフラグ(ビットフラグ)
		int boxKind = 0;		//形状 0:球, 1:箱, 2:カプセル
		XMFLOAT3 boxSize = {};
		XMFLOAT3 position = {};
		XMFLOAT3 rotation = {};
		float mass = 1;                     // 質量
		float positionDamping = 0;          // 移動減衰
		float rotationDamping = 0;          // 回転減衰
		float restitution = 0;              // 反発力
		float friction = 0;                 // 摩擦力
		int mode = 0;						//剛体のモード 0:ボーン追従(static), 1:物理演算(dynamic), 2:ボーン追従+位置合わせ
		bool isPass(int g) const { return passGroup & (1U << g); }	//グループ番号gは被衝突グループに含まれるか？
	};

	//ジョイント
	struct PMXJoint {
		std::wstring name;
		std::wstring name_e;
		int bodyA = 0;
		int bodyB = 0;

		int kind = 0;      // Joint種類 | 0:Sp6DOF, 1:G6DOF, 2:P2P, 3:ConeTwist, 4:Slider, 5:Hinge (PMX2.0では0のみ対応)

		XMFLOAT3 position = {};   // 位置
		XMFLOAT3 rotation = {};   // 回転(ラジアン角)

		XMFLOAT3 moveLo = {};      // 移動下限 ※Sp6DOFの場合(同以下)
		XMFLOAT3 moveHi = {};     // 移動上限
		XMFLOAT3 angleLo = {};		//回転下限
		XMFLOAT3 angleHi = {};		//回転上限
		XMFLOAT3 springMove = {};	//バネ定数-移動
		XMFLOAT3 springRotate = {};	//バネ定数-回転
	};

	//文字列のエンコード(読み込み時に変換するための物。読み込んだ後は全てwchar(utf16)に変換される)
	enum class PMXEncoding { utf8, utf16 };


	class PMXModel {
	private:
		//ファイルに格納されている時のエンコード
		PMXEncoding m_encoding = PMXEncoding::utf16;	//※文字列は全部読み取り時にwchar(utf16)に変換される
		//ファイルからの読み取り時にしか使われない、各インデックスのファイル内でのサイズ
		int m_size_vi = 1;
		int m_size_ti = 1;
		int m_size_mi = 1;
		int m_size_bi = 1;
		int m_size_moi = 1;
		int m_size_ri = 1;
		//ファイル読み取り用メソッド
		PMXVertex FReadVertex(FILE* fp);
		PMXMaterial FReadMaterial(FILE* fp);
		PMXBone FReadBone(FILE* fp);
		PMXMorph FReadMorph(FILE* fp);
		PMXNodeItem FReadNodeItem(FILE* fp);
		PMXNode FReadNode(FILE* fp);
		PMXBody FReadBody(FILE* fp);
		PMXJoint FReadJoint(FILE* fp);
		std::wstring FReadString(FILE* fp) const;
		int m_extra_uv = 0;
	public:
		int extraUV() const { return m_extra_uv; }	//有効な追加UVの数
		std::vector<PMXVertex> vertices;	//頂点
		std::vector<int>indices;			//頂点インデクス
		std::vector<std::wstring>textures;	//テクスチャファイル名
		std::vector<PMXMaterial>materials;//材質
		std::vector<PMXBone>bones;		//ボーン
		std::vector<PMXMorph>morphs;	//モーフ
		std::vector<PMXNode>nodes;		//表示枠
		std::vector<PMXBody>bodies;//剛体
		std::vector<PMXJoint>joints;	//ジョイント
		std::wstring name;		//モデル名
		std::wstring name_e;	//モデル名(英語)
		std::wstring description;	//モデル説明文
		std::wstring description_e;	//モデル説明文(英語)
		PMXModel(const wchar_t* fname);
		~PMXModel();
	};

	/******************************************************************************************/
	// VMD(Camera)
	/******************************************************************************************/
	struct VMDCamera
	{
		int iFrame; // フレーム番号
		float distance; // 目標点とカメラの距離(目標点がカメラ前面でマイナス)
		XMFLOAT3 position;	//目標点の位置 (x,y,z)
		XMFLOAT3 rotation;	//カメラの回転 (rx(MMD数値入力のマイナス値),ry,rz) [rad]
		union {
			float bezierParams[24]; // 補間パラメータ(元データは0-127だけど0.0～1.0で管理する)
			struct Bezier {
				float Xax, Xbx, Xay, Xby;
				float Yax, Ybx, Yay, Yby;
				float Zax, Zbx, Zay, Zby;
				float Rax, Rbx, Ray, Rby;
				float Lax, Lbx, Lay, Lby;
				float Vax, Vbx, Vay, Vby;
			} bezier;
		};
		float viewAngle; // 視野角(deg)
		char parth; // パースペクティブ, 0:ON, 1:OFF
	};
	
	//VMD(Camera)ファイルローダ
	class VMDCam {
		UINT m_codepage;	//VMDファイルが作成されたときに使われたコードページ
		VMDCamera FreadCamera(FILE* fp);
	public:
		std::wstring modelName;
		std::vector<VMDCamera>cameraKeys;
		int lastFrame = 0;	//最終フレーム番号
		VMDCam(const wchar_t* fname, UINT codepage = CP_ACP, bool fps60enable = false);
		~VMDCam();
	};

	/******************************************************************************************/
	// VMD(Motion)
	/******************************************************************************************/
	struct VMDMotion {
		wchar_t bone[16];	//ボーン名(VMDは本来SJISなのでwcharに変換したもの)
		int iFrame;
		XMFLOAT3 position;	//位置
		XMFLOAT4 rotation;	//姿勢(クォータニオン)
		union {
			float bezierParams[16];	//補間パラメータ(元データは0-127だけど0.0～1.0で管理する)
			struct Bezier {
				//				float Xax, Xay, Xbx, Xby;
				//				float Yax, Yay, Ybx, Yby;
				//				float Zax, Zay, Zbx, Zby;
				//				float Rax, Ray, Rbx, Rby;
				float Xax, Yax, Zax, Rax;
				float Xay, Yay, Zay, Ray;
				float Xbx, Ybx, Zbx, Rbx;
				float Xby, Yby, Zby, Rby;
			} bezier;
		};
	};

	struct VMDMorph {
		wchar_t name[16] = {};	//元データだと15バイトらしいんだけど念のため末尾の\0分含めて16文字分にしとく
		int iFrame = 0;
		float value = 0;
	};

	//ボーンモーフ(ポーズソルバー用)
	struct VMDBoneMorph {
		int iMorph = 0;		//何番のモーフか
		float ratio = 0;	//効果率
		XMFLOAT4 rotation = {};	//回転
		XMFLOAT3 translation = {};	//移動
	};

	//VMD(Motion)ファイルローダ
	class VMD {
		UINT m_codepage;	//VMDファイルが作成されたときに使われたコードページ
		VMDMotion FreadMotion(FILE* fp);
		VMDMorph FreadMorph(FILE* fp);
	public:
		std::wstring modelName;
		std::vector<VMDMotion>motionKeys;
		std::vector<VMDMorph>morphKeys;
		int lastFrame = 0;	//最終フレーム番号
		VMD(const wchar_t* fname, UINT codepage = CP_ACP, bool fps60enable = false);
		~VMD();
	};


	/******************************************************************************************/
	// 物理 : benikabocha様作 sabaを 大いに参考にさせていただきました
	/******************************************************************************************/
	class PoseSolver;
	class PoseBone;
	class Physics;

	class MMDMotionState : public btMotionState {
	public:
		virtual void Reset() = 0;
		virtual void ReflectGlobalTransform() = 0;
	};


	class RigidBody {
	private:
		XMMATRIX m_offsetMat;
		Physics* m_physics;
		std::unique_ptr<btCollisionShape>	m_shape;
		std::unique_ptr<MMDMotionState>		m_activeMotionState;
		std::unique_ptr<MMDMotionState>		m_kinematicMotionState;
	public:
		PoseBone* bone;
		int mode;	//0:ボーン追従(static) 1:物理演算(dynamic) 2:物理演算 + Bone位置合わせ
		int group;
		int groupMask;
		std::unique_ptr<btRigidBody> rigidBody;
		RigidBody(const PMXModel* model, int iRB, const PoseSolver* solver);	//モデルデータと剛体番号とポーズソルバの参照を渡して物理剛体の作成
		void SetActivation(bool active);
		void ResetTransform();
		void Reset();
		void ReflectGlobalTransform();
		void CalcLocalTransform();	//親への変換を計算してm_boneにセットする
		XMMATRIX GetTransform();
	};

	class Joint {
	public:
		std::unique_ptr<btTypedConstraint> constraint;
		Joint(const PMXModel* model, int index, RigidBody* rigidBodyA, RigidBody* rigidBodyB);
	};

	//Bulletを用いた物理ソルバー
	class Physics {
	private:
		std::unique_ptr<btBroadphaseInterface>				m_broadphase;
		std::unique_ptr<btDefaultCollisionConfiguration>	m_collisionConfig;
		std::unique_ptr<btCollisionDispatcher>				m_dispatcher;
		std::unique_ptr<btSequentialImpulseConstraintSolver>m_solver;
		std::unique_ptr<btDiscreteDynamicsWorld>			m_world;
		std::unique_ptr<btCollisionShape>					m_groundShape;
		std::unique_ptr<btMotionState>						m_groundMS;
		std::unique_ptr<btRigidBody>						m_groundRB;
		std::unique_ptr<btOverlapFilterCallback>			m_filterCB;
		std::vector<PoseSolver*> m_notifees;	//なんか動きました、という事を知らされる人たち
		//剛体・ジョイントの追加・削除…PoseSolverのAttach/DetachPhysicsから呼ばれる
		void AddRigidBody(RigidBody* rb);
		void RemoveRigidBody(RigidBody* rb);
		void AddJoint(Joint* joint);
		void RemoveJoint(Joint* joint);
		//Update,Resetした時に通知を受けるソルバーの追加・削除…PoseSolverのAttach/DetachPhysicsから呼ばれる
		void AddNotifee(PoseSolver* solver);
		void DeleteNotifee(PoseSolver* solver);
	public:
		Physics();
		~Physics();
		double fps = 120;
		double maxSubsteps = 10;
		void Reset();
		void Update(double dt);
		void Prewarm(int frames, float timePerFrame = 1 / 30.0f);
		
		friend RigidBody;
		friend Joint;
		friend PoseSolver;
	};


	/******************************************************************************************/
	// PoseSolver
	/******************************************************************************************/
	//各ボーンの変換を計算するためのクラス。PoseSolverの中で使われる
	class PoseBone {
	public:
		//コンストラクタでセットされたら変わらない情報群
		int iBone = -1;		//このボーンのPMX内での番号
		XMFLOAT3 origin;	//ボーンの基準点(ワールド座標系)
		PoseBone* parent = nullptr;			//親ボーン
		std::vector<PoseBone*> children;	//子ボーン群
		
		//キーフレーム情報:モーション読み込み時に決定。キーフレームの値とボーンモーフの値を合算した物
		XMFLOAT3 keyTranslation;		//キーフレームに書いてある移動分
		XMVECTOR keyRotation;			//キーフレームに書いてある回転分
		
		//IKと付与計算用
		bool IKLinked = false;			//IKリンクとして動かされたならTrue
		XMVECTOR IKRotation;			//IKリンクとして動かされた回転量
		XMFLOAT3 appendedTranslation;	//付与された移動分
		XMVECTOR appendedRotation;		//付与された回転分
		
		//以上から計算されるローカル変換とワールド変換
		XMFLOAT3 localTranslation;	//key,appendから計算されるローカル移動分
		XMVECTOR localRotation;		//key,IK,appendから計算されるローカル回転分

		void UpdateLocalTransform();	//localTranslation/RotationとoriginからtoParentを計算し、「ワールド変換はまだ更新されていない」フラグを立てる
		XMMATRIX toParent;			//計算される、親ボーン座標系への変換行列(ローカル変形行列)

		bool transformResolved = false;	//ワールド変換が有効である、フラグ
		XMMATRIX transform;			//ワールド座標系までの変換行列

		void Propagate(PoseBone* bone);	//boneに指定されたボーンのワールド変換を子ボーンに伝える
		void PropagateUnresolved(PoseBone* bone);	//変換が更新された旨だけ子ボーンに伝える
		XMMATRIX ResolveTransform();	//ワールド変換を得る。transformが未確定の場合、解決する

		PoseBone();
		~PoseBone();
	};


	//pmxにvmdを割り当てて、あるフレームにおけるポーズ(=各ボーンの変換行列)を計算・格納するクラス
	//表示したいpmxモデル1体につき1つ生成する必要があります
	class PoseSolver {
		//Solveから呼ばれる関数
		void GetKeyFrame(float time, int iBone, XMFLOAT3* translation, XMVECTOR* rotation);	//iFrameにおけるボーン番号iBoneについての位置・向きをモーションデータから得る
		float GetKeyFrameMorph(float time, int iMorph); //↑のモーフ版
		void IK(int iBone);	//Solveから呼ばれる。1つのIKボーンを解決する
		void FinishMatrices();	//bonesをboneMatricesに反映する

		//物理対応
		void AttachPhysics();			//コンストラクタで呼ばれる
		void DetachPhysics();			//デストラクタで呼ばれる
		std::vector<RigidBody*> body;	//読み込んだpmxモデルに応じた物理演算用オブジェクト
		std::vector<Joint*> joint;

		//↓コンストラクタでセットされたら変わらないメンバ
		std::vector<int> TFOrder;	//各ボーンの変形順序テーブル。transformOrder[0]に最初に変形されるボーン番号を格納
		int bonesBeforePhys;		//物理前変形ボーンの本数
		const PMXModel* pmx;
		const VMD* vmd;
		std::vector<std::vector<VMDMotion>> motionKeys;	//ボーンごとに分類したフレームキー motions[ボーン番号][フレーム順のモーション番号]
		std::vector<std::vector<VMDMorph>> morphKeys;	//モーフごとに分類したフレームキー morphs[モーフ番号][フレーム順のモーション番号]
		
		std::vector<XMFLOAT3>m_goalT;
		std::vector<XMVECTOR>m_goalQ;
		
		//物理対応用
		//physicsから参照されるモノ
		std::vector<PoseBone*>bones;	//PoseBoneはそのままPMXのボーン番号と対応している
		PoseBone* root = nullptr;		//-1番のボーン(全ての親の親)
		Physics* physics = nullptr;
		void OnUpdatePhysicsBegin();
		void OnUpdatePhysicsEnd();
		void OnResetPhysicsBegin();
		void OnResetPhysicsEnd();
		void OnPrewarmPhysics(int frames, int totalFrames);	//framesフレーム掛けて標準状態から最後にSolveされた状態までもっていく
	public:
		//コントスラクタ、_physicsがnullptrの場合、物理演算しない
		PoseSolver(Physics* _physics, const PMXModel* _pmx, const VMD* _vmd);
		~PoseSolver();
		void Solve(float time);	//時刻timeにおけるポーズをbonesに、モーフをmorphValuesに反映する。開始フレームは0

		//↓Solveの結果を反映するメンバ。アプリケーションからはだいたいこれだけ参照すればOK
		std::vector<float>morphValues;		//モーフ値。pmxのモーフ番号がそのまま対応しているが、グループモーフは展開されている
		std::vector<XMMATRIX>boneMatrices;	//各ボーンのグローバル変形を格納した配列(シェーダに渡せるよう転置済み)

		friend Physics;
		friend RigidBody;
		friend Joint;
	};

	
	/******************************************************************************************/
	// CameraSolver
	/******************************************************************************************/
	//cameraにvmdを割り当てて、あるフレームにおけるカメラのパラメータを計算・格納するクラス
	//表示したいcamera1個につき1つ生成する必要があります
	class CameraSolver {
		//Solveから呼ばれる関数
		void GetKeyFrame(float time, int iCam, VMDCamera* vmdcamera);	//iFrameにおけるカメラ番号iCamについての位置・向きをモーションデータから得る
		//↓コンストラクタでセットされたら変わらないメンバ
		std::vector<const VMDCam*> vmdcams;
		std::vector<std::vector<VMDCamera>> cameraKeys;	//カメラごとに分類したフレームキー cameraKeys[カメラ番号][フレーム順のカメラモーション番号]
	public:
		//コントスラクタ
		CameraSolver(const VMDCam* _vmdcam);
		~CameraSolver();
		void Solve(float time, int iCam, VMDCamera* vmdcamera);	//時刻timeにおけるカメラのパラメータを反映する。開始フレームは0
		void Add(int iCam, const VMDCam* _vmdcam);
	};
}


module : private;

#pragma warning(disable: 4267 4244 4305)

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace std;



namespace {
	/***********************************************************************************/
	//数学系ユーティリティ SimpleMathにさらにちょい足し
	/***********************************************************************************/
	static const float PI = acos(-1);
	typedef Vector2 vec2;
	typedef Vector3 vec3;
	typedef Vector4 vec4;
	typedef Quaternion vecQ;

	inline Matrix operator*(const Matrix& M, const Quaternion& q) { return M * Matrix::CreateFromQuaternion(q); }
	inline Matrix operator*(const Quaternion& q, const Matrix& M) { return Matrix::CreateFromQuaternion(q) * M; }

	inline float Lerp(float a, float b, float t) { return a * (1 - t) + b * t; }
	inline vec3 Lerp(vec3 a, vec3 b, vec3 t) { return vec3(Lerp(a.x, b.x, t.x), Lerp(a.y, b.y, t.y), Lerp(a.z, b.z, t.z)); };
	inline vec4 Lerp(vec4 a, vec4 b, vec4 t) { return vec4(Lerp(a.x, b.x, t.x), Lerp(a.y, b.y, t.y), Lerp(a.z, b.z, t.z), Lerp(a.w, b.w, t.w)); };
	inline Quaternion Lerp(Quaternion a, Quaternion b, Quaternion t) { return Quaternion(Lerp(a.x, b.x, t.x), Lerp(a.y, b.y, t.y), Lerp(a.z, b.z, t.z), Lerp(a.w, b.w, t.w)); };
	inline float Clamp(float a, float mi, float ma) { return min(max(a, mi), ma); }

	inline float Sign(float x) {
		if (x > 0) return 1;
		else if (x < 0) return -1;
		else return 0;
	}

	//行ベクトル
	inline Vector3 _11_12_13(const Matrix& M) { return Vector3(M._11, M._12, M._13); }
	inline Vector3 _21_22_23(const Matrix& M) { return Vector3(M._21, M._22, M._23); }
	inline Vector3 _31_32_33(const Matrix& M) { return Vector3(M._31, M._32, M._33); }
	inline Vector3 _41_42_43(const Matrix& M) { return Vector3(M._41, M._42, M._43); }
	//列ベクトル
	inline Vector3 _11_21_31(const Matrix& M) { return Vector3(M._11, M._21, M._31); }
	inline Vector3 _12_22_32(const Matrix& M) { return Vector3(M._12, M._22, M._32); }
	inline Vector3 _13_23_33(const Matrix& M) { return Vector3(M._13, M._23, M._33); }
	//対角
	inline Vector3 _11_22_33(const Matrix& M) { return Vector3(M._11, M._22, M._33); }

	//クォータニオンとV4からxyz成分抜き出し
	inline Vector3 xyz(const Quaternion& q) { return Vector3(q.x, q.y, q.z); }
	inline Vector3 xyz(const Vector4& v) { return Vector3(v.x, v.y, v.z); }

	//なんか微妙に使いづらい関数を使いやすくする関数
	inline Vector3 Normalize(const Vector3& v) { Vector3 r; v.Normalize(r); return r; };
	inline Quaternion Normalize(const Quaternion& q) { Quaternion r; q.Normalize(r); return r; }
	inline Quaternion Conjugate(Quaternion q) { Quaternion r; q.Conjugate(r); return r; }
	inline float Length(vec3 v) { float f; f = v.Length();  return f; }
	inline float Length(vecQ v) { float f; f = v.Length();  return f; }
	inline float Dot(Vector3 a, Vector3 b) { return a.Dot(b); }
	inline float Dot(Vector4 a, Vector4 b) { return a.Dot(b); }
	inline float Dot(Quaternion a, Quaternion b) { return a.Dot(b); }
	inline Vector3 Cross(Vector3 a, Vector3 b) { return a.Cross(b); }

	inline vec3 Rotate(vec3 v, vecQ q) { return vec3::TransformNormal(v, Matrix::CreateFromQuaternion(q)); }


	//Aの回転要素を抜き出した行列を作る
	inline Matrix ExtractRot(const Matrix& A)
	{
		Matrix m;
		m._11 = A._11; m._12 = A._12; m._13 = A._13; m._14 = 0;
		m._21 = A._21; m._22 = A._22; m._23 = A._23; m._24 = 0;
		m._31 = A._31; m._32 = A._32; m._33 = A._33; m._34 = 0;
		m._41 = 0;	m._42 = 0; m._43 = 0; m._44 = 1;
		return m;
	}

	//Aの回転成分をBの回転成分で置き換える
	inline void ReplaceRot(Matrix& A, const Matrix& B)
	{
		A._11 = B._11; A._12 = B._12; A._13 = B._13;
		A._21 = B._21; A._22 = B._22; A._23 = B._23;
		A._31 = B._31; A._32 = B._32; A._33 = B._33;
	}

	//o周りに回る、回転要素がqで平行移動要素かtの行列を作る
	inline Matrix oRtMatrix(vec3 o, vecQ q, vec3 t)
	{
		//Matrix m = Matrix::CreateTranslation(-o) * q * Matrix::CreateTranslation(o + t); と同値だが早い

		XMMATRIX m = Matrix::CreateFromQuaternion(q);

		vec3 _o = -o;
		m.r[3] = XMVector4Transform(vec4(_o.x, _o.y, _o.z, 1), m);
		vec3 d = o + t;
		m.r[3] = XMVectorAdd(m.r[3], vec4(d.x, d.y, d.z, 0));
		return m;
	}

	/***********************************************************************************/
	//デバッグとかファイル操作とか文字列ユーティリティ
	/***********************************************************************************/

	string wstrTostr(wstring w) {
		int iBufferSize = WideCharToMultiByte(CP_OEMCP, 0, w.c_str(), -1, (char*)NULL, 0, NULL, NULL);
		CHAR* cpMultiByte = new CHAR[iBufferSize];
		WideCharToMultiByte(CP_OEMCP, 0, w.c_str(), -1, cpMultiByte, iBufferSize, NULL, NULL);
		std::string oRet(cpMultiByte, cpMultiByte + iBufferSize - 1);
		delete[] cpMultiByte;
		return(oRet);
	}

	wstring Format(wstring format, ...) {
		wchar_t strbuf[4096];
		va_list args;
		va_start(args, format);
		vswprintf_s(strbuf, 4096, format.c_str(), args);
		va_end(args);
		return strbuf;
	}

	void LOG(wstring format, ...) {
#ifdef _DEBUG
		wchar_t strbuf[4096];
		va_list args;
		va_start(args, format);
		vswprintf_s(strbuf, 4096, format.c_str(), args);
		va_end(args);
		OutputDebugStringW(strbuf);
		OutputDebugStringW(L"\n");
#endif
	}

	HRESULT ThrowIfFailed(HRESULT hr, wstring format, ...) {
		if (hr != S_OK) {
			wchar_t strbuf[4096];
			va_list args;
			va_start(args, format);
			vswprintf_s(strbuf, 4096, format.c_str(), args);
			va_end(args);
			wstring err = strbuf;
			err += Format(L" HRESULT=%x", hr);
			OutputDebugStringW(err.c_str());
			OutputDebugStringA(std::system_category().message(hr).c_str());
			OutputDebugStringA("\n");
			throw PMX::PMXException(wstrTostr(err).c_str());
		}
		return hr;
	}

	wstring strTowstr(string s, UINT codepage) {
		int iBufferSize = MultiByteToWideChar(codepage, 0, s.c_str(), -1, (wchar_t*)NULL, 0);
		wchar_t* cpUCS2 = new wchar_t[iBufferSize];
		MultiByteToWideChar(codepage, 0, s.c_str(), -1, cpUCS2, iBufferSize);
		std::wstring oRet(cpUCS2, cpUCS2 + iBufferSize - 1);
		delete[] cpUCS2;
		return(oRet);
	}

	wstring vec2wstr(Vector3 v) { return Format(L"%.3f,%.3f,%.3f", v.x, v.y, v.z); }
	wstring vec2wstr(Vector4 v) { return Format(L"%.3f,%.3f,%.3f,%.3f", v.x, v.y, v.z, v.w); }
	wstring quat2wstr(Vector4 v) { return Format(L"%.3f,%.3f,%.3f,%.3f", v.x, v.y, v.z, v.w); }


	void PrintBoneStat(PMX::PMXModel* pmx, vector<PMX::PoseBone*>& bones, const wstring& name, bool parents = false) {
		bool found = false;
		for (int i = 0; i < pmx->bones.size(); i++) {
			if (pmx->bones[i].name == name) {
				found = true;
				auto b = bones[i];
				LOG(L"%.3d %s : o:%s q:%s t:%s", b->iBone, pmx->bones[b->iBone].name, vec2wstr(b->origin).c_str(), quat2wstr(b->localRotation).c_str(), vec2wstr(b->localTranslation).c_str());
				if (parents) {
					while (b = b->parent) {
						if (b->iBone >= 0)
							LOG(L"%.3d %s : o:%s q:%s t:%s", b->iBone, pmx->bones[b->iBone].name, vec2wstr(b->origin).c_str(), quat2wstr(b->localRotation).c_str(), vec2wstr(b->localTranslation).c_str());
					}
				}
			}
		}
		if (!found)
			LOG(L"not found bone %s", name.c_str());
	}

	template <class T> T FRead(FILE* fp, int count = 1)
	{
		T dest = {};
		fread_s(&dest, sizeof(T), sizeof(T), count, fp);
		return dest;
	}

	int FReadBySize(FILE* fp, int size)
	{
		if (size == 1)
			return FRead<char>(fp);
		else if (size == 2)
			return FRead<short>(fp);
		else if (size == 4)
			return FRead<int>(fp);
		else
			return 0;
	}

	//頂点だけの特別版
	int FReadBySizeVertex(FILE* fp, int size)
	{
		if (size == 1)
			return FRead<::byte>(fp);
		else if (size == 2)
			return FRead<unsigned short>(fp);
		else if (size == 4)
			return FRead<int>(fp);	//4バイトの時だけ符号あり
		else
			return 0;
	}
}

namespace PMX {
	/***********************************************************************************/
	//PMX
	/***********************************************************************************/
	wstring PMXModel::FReadString(FILE* fp) const
	{
		int count = FRead<int>(fp);

		if (m_encoding == PMXEncoding::utf8) {
			char* cbuf = new char[count + 1];
			fread_s(cbuf, count, count, 1, fp);
			cbuf[count] = 0;
			int size = MultiByteToWideChar(CP_UTF8, 0, cbuf, -1, nullptr, 0);
			wchar_t* wbuf = new wchar_t[size];
			MultiByteToWideChar(CP_UTF8, 0, cbuf, -1, wbuf, size);
			delete[] cbuf;
			wstring ret = wbuf;
			delete[] wbuf;

			return ret;
		} else {
			wchar_t* buf = new wchar_t[(count / 2) + 1];
			fread_s(buf, count, count, 1, fp);
			buf[count / 2] = 0;
			wstring ret = buf;
			delete[] buf;
			return ret;
		}
	}

	PMXVertex PMXModel::FReadVertex(FILE* fp)
	{
		PMXVertex v = {};
		v.position = FRead<XMFLOAT3>(fp);
		v.normal = FRead<XMFLOAT3>(fp);

		//UVと追加UV
		v.uv = FRead<XMFLOAT2>(fp);
		for (int i = 0; i < m_extra_uv; i++) {
			v.extra_uv[i] = FRead<XMFLOAT4>(fp);
		}
		for (int i = m_extra_uv; i < 4; i++) {
			v.extra_uv[i] = vec4(0, 0, 0, 0);
		}

		v.weightType = FRead<char>(fp);

		if (v.weightType == 0) {
			//BDEF1
			v.bone[0] = FReadBySize(fp, m_size_bi);
			v.bone[1] = -1;
			v.bone[2] = -1;
			v.bone[3] = -1;
			v.weight[0] = 1;
			v.weight[1] = 0;
			v.weight[2] = 0;
			v.weight[3] = 0;
		} else if (v.weightType == 1) {
			//BDEF2
			v.bone[0] = FReadBySize(fp, m_size_bi);
			v.bone[1] = FReadBySize(fp, m_size_bi);
			v.bone[2] = -1;
			v.bone[3] = -1;
			v.weight[0] = FRead<float>(fp);
			v.weight[1] = 1 - v.weight[0];
			v.weight[2] = 0;
			v.weight[3] = 0;
		} else if (v.weightType == 2) {
			//BDEF4
			for (int i = 0; i < 4; i++) v.bone[i] = FReadBySize(fp, m_size_bi);
			for (int i = 0; i < 4; i++) v.weight[i] = FRead<float>(fp);
		} else if (v.weightType == 3) {
			//SDEF
			v.bone[0] = FReadBySize(fp, m_size_bi);
			v.bone[1] = FReadBySize(fp, m_size_bi);
			v.weight[0] = FRead<float>(fp);
			v.weight[1] = 1 - v.weight[0];
			v.sdef_c = FRead<XMFLOAT3>(fp);
			v.sdef_r0 = FRead<XMFLOAT3>(fp);
			v.sdef_r1 = FRead<XMFLOAT3>(fp);
			v.bone[2] = -1;
			v.bone[3] = -1;
			v.weight[2] = 0;
			v.weight[3] = 0;
		}
		v.edge = FRead<float>(fp);

		return v;
	}

	PMXMaterial PMXModel::FReadMaterial(FILE* fp)
	{
		PMXMaterial m;
		m.name = FReadString(fp);
		m.name_e = FReadString(fp);

		m.diffuse = FRead<XMFLOAT4>(fp);
		m.specular = FRead<XMFLOAT3>(fp);
		m.shininess = FRead<float>(fp);
		m.ambient = FRead<XMFLOAT3>(fp);
		m.drawFlag = FRead<char>(fp);
		m.edgeColor = FRead<XMFLOAT4>(fp);
		m.edgeSize = FRead<float>(fp);
		m.tex = FReadBySize(fp, m_size_ti);
		m.spTex = FReadBySize(fp, m_size_ti);
		m.spmode = FRead<char>(fp);
		m.toonFlag = FRead<char>(fp);
		if (m.toonFlag == 0)
			m.toonTex = FReadBySize(fp, m_size_ti);
		else
			m.toonTex = FReadBySize(fp, 1);

		wstring dmy = FReadString(fp);	//メモ、読み飛ばす

		m.vertexCount = FRead<int>(fp);

		return m;
	}

	PMXBone PMXModel::FReadBone(FILE* fp)
	{
		PMXBone b;

		b.name = FReadString(fp);
		b.name_e = FReadString(fp);

		b.position = FRead<XMFLOAT3>(fp);
		b.parent = FReadBySize(fp, m_size_bi);
		b.level = FRead<int>(fp);
		b.flag = FRead<unsigned short>(fp);

		if ((b.flag & 0x0001) == 0) {
			//接続先はオフセット指定
			b.toOffset = FRead<XMFLOAT3>(fp);
		} else {
			//接続先はボーン指定
			b.toBone = FReadBySize(fp, m_size_bi);
		}

		//付与
		if (b.IsAppendRotation() || b.IsAppendTranslation()) {
			b.appendParent = FReadBySize(fp, m_size_bi);
			b.appendRatio = FRead<float>(fp);
		}

		//軸固定
		if (b.IsFixAxis())
			b.fixAxis = FRead<XMFLOAT3>(fp);
		//ローカル軸
		if (b.IsLocalFrame()) {
			b.localAxisX = FRead<XMFLOAT3>(fp);
			b.localAxisZ = FRead<XMFLOAT3>(fp);
		}
		//外部親変形
		if (b.IsExternal()) {
			b.externalKey = FRead<int>(fp);
		}

		//IK
		if (b.IsIK()) {
			b.IKTarget = FReadBySize(fp, m_size_bi);
			b.IKLoopCount = FRead<int>(fp);
			b.IKAngle = FRead<float>(fp);
			b.IKLinkCount = FRead<int>(fp);
			b.IKLinks = vector<PMXIKLink>(b.IKLinkCount);
			for (int i = 0; i < b.IKLinkCount; i++) {
				b.IKLinks[i].bone = FReadBySize(fp, m_size_bi);
				b.IKLinks[i].isLimit = FRead<char>(fp);
				if (b.IKLinks[i].isLimit) {
					b.IKLinks[i].low = FRead<XMFLOAT3>(fp);
					b.IKLinks[i].hi = FRead<XMFLOAT3>(fp);
				}
			}
		}

		return b;
	}

	PMXMorph PMXModel::FReadMorph(FILE* fp)
	{
		PMXMorph mo = {};

		mo.name = FReadString(fp);
		wstring dmy = FReadString(fp);

		char panel = FRead<char>(fp);	//操作パネル(読み捨て)
		mo.kind = FRead<char>(fp);
		mo.offsetCount = FRead<int>(fp);
		if (mo.kind == 0) {
			mo.offsets = new PMXGroupMorphOffset[mo.offsetCount];
			PMXGroupMorphOffset* mof = (PMXGroupMorphOffset*)mo.offsets;
			for (int i = 0; i < mo.offsetCount; i++) {
				mof->morph = FReadBySize(fp, m_size_moi);
				mof->ratio = FRead<float>(fp);
				mof++;
			}
		} else if (mo.kind == 1) {
			PMXVertexMorphOffset* mof;
			mo.offsets = new PMXVertexMorphOffset[mo.offsetCount];
			mof = (PMXVertexMorphOffset*)mo.offsets;
			for (int i = 0; i < mo.offsetCount; i++) {
				mof->vertex = FReadBySizeVertex(fp, m_size_vi);
				mof->offset = FRead<XMFLOAT3>(fp);
				mof++;
			}
		} else if (mo.kind == 2) {
			PMXBoneMorphOffset* mof;
			mo.offsets = new PMXBoneMorphOffset[mo.offsetCount];
			mof = (PMXBoneMorphOffset*)mo.offsets;
			for (int i = 0; i < mo.offsetCount; i++) {
				mof->bone = FReadBySize(fp, m_size_bi);
				mof->translation = FRead<XMFLOAT3>(fp);
				mof->rotation = FRead<XMFLOAT4>(fp);
				mof++;
			}
		} else if ((mo.kind >= 3) && (mo.kind <= 7)) {
			PMXUVMorphOffset* mof;
			mo.offsets = new PMXUVMorphOffset[mo.offsetCount];
			mof = (PMXUVMorphOffset*)mo.offsets;
			for (int i = 0; i < mo.offsetCount; i++) {
				mof->iuv = mo.kind - 3;
				mof->vertex = FReadBySizeVertex(fp, m_size_vi);
				mof->offset = FRead<XMFLOAT4>(fp);
				mof++;
			}
		} else if (mo.kind == 8) {
			PMXMaterialMorphOffset* mof;
			mo.offsets = new PMXMaterialMorphOffset[mo.offsetCount];
			mof = (PMXMaterialMorphOffset*)mo.offsets;
			for (int i = 0; i < mo.offsetCount; i++) {
				mof->material = FReadBySize(fp, m_size_mi);
				mof->op = FRead<char>(fp);
				mof->diffuse = FRead<XMFLOAT4>(fp);
				mof->specular = FRead<XMFLOAT3>(fp);
				mof->shininess = FRead<float>(fp);
				mof->ambient = FRead<XMFLOAT3>(fp);
				mof->edgeColor = FRead<XMFLOAT4>(fp);
				mof->edgeSize = FRead<float>(fp);
				mof->tex = FRead<XMFLOAT4>(fp);
				mof->spTex = FRead<XMFLOAT4>(fp);
				mof->toon = FRead<XMFLOAT4>(fp);
				mof++;
			}
		}

		return mo;
	}

	PMXNodeItem PMXModel::FReadNodeItem(FILE* fp)
	{
		PMXNodeItem ni;

		char flag = FRead<char>(fp);
		if (flag == 0) {
			ni.isBone = true;
			ni.isMorph = false;
			ni.index = FReadBySize(fp, m_size_bi);
		} else {
			ni.isBone = false;
			ni.isMorph = true;
			ni.index = FReadBySize(fp, m_size_moi);
		}

		return ni;
	}

	PMXNode PMXModel::FReadNode(FILE* fp)
	{
		PMXNode n;

		n.name = FReadString(fp);
		n.name_e = FReadString(fp);

		//LOG(L"node:%s", n.name);
		n.flag = FRead<char>(fp);
		n.items.resize(FRead<int>(fp));
		for (int i = 0; i < n.items.size(); i++) {
			n.items[i] = FReadNodeItem(fp);
			//LOG(L"index:%d", n.items[i].index);
		}

		return n;
	}

	PMXBody PMXModel::FReadBody(FILE* fp)
	{
		PMXBody bd;
		bd.name = FReadString(fp);
		bd.name_e = FReadString(fp);

		bd.bone = FReadBySize(fp, m_size_bi);
		bd.group = FRead<char>(fp);
		bd.passGroup = FRead<USHORT>(fp);
		bd.boxKind = FRead<char>(fp);
		bd.boxSize = FRead<XMFLOAT3>(fp);
		bd.position = FRead<XMFLOAT3>(fp);
		bd.rotation = FRead<XMFLOAT3>(fp);
		bd.mass = FRead<float>(fp);
		bd.positionDamping = FRead<float>(fp);
		bd.rotationDamping = FRead<float>(fp);
		bd.restitution = FRead<float>(fp);
		bd.friction = FRead<float>(fp);
		bd.mode = FRead<char>(fp);
		//LOG(L"%s", bd.name);

		return bd;
	}

	PMXJoint PMXModel::FReadJoint(FILE* fp)
	{
		PMXJoint j;

		j.name = FReadString(fp);
		j.name_e = FReadString(fp);

		j.kind = FRead<char>(fp);

		j.bodyA = FReadBySize(fp, m_size_ri);
		j.bodyB = FReadBySize(fp, m_size_ri);
		j.position = FRead<XMFLOAT3>(fp);
		j.rotation = FRead<XMFLOAT3>(fp);
		j.moveLo = FRead<XMFLOAT3>(fp);
		j.moveHi = FRead<XMFLOAT3>(fp);
		j.angleLo = FRead<XMFLOAT3>(fp);
		j.angleHi = FRead<XMFLOAT3>(fp);
		j.springMove = FRead<XMFLOAT3>(fp);
		j.springRotate = FRead<XMFLOAT3>(fp);

		//LOG(L"%s : %s->%s", j.name, m_rs[j.bodyA].name, m_rs[j.bodyB].name);
		return j;
	}


	PMXModel::PMXModel(const wchar_t* fname)
	{
		FILE* fp;
		_wfopen_s(&fp, fname, L"rb");
		if (!fp) {
			ThrowIfFailed(E_INVALIDARG, L"can't open PMXfile!");
		}

		//マジックナンバーの確認
		char magic[5];
		fread_s(magic, 4, 4, 1, fp);
		magic[4] = 0;
		if (strcmp(magic, "PMX ") != 0)
			ThrowIfFailed(E_INVALIDARG, L"invalid PMX file!");
		float version = FRead<float>(fp);
		if (version != 2.0)
			ThrowIfFailed(E_INVALIDARG, L"unsupproted PMX version!");
		//後続データ数の確認(8で固定とのこと)
		BYTE data = FRead<BYTE>(fp);
		if (data != 8) ThrowIfFailed(E_INVALIDARG, L"invalid PMX file!");
		//8つの「後続データ」読み込み
		data = FRead<BYTE>(fp);
		if (data != 0)
			m_encoding = PMXEncoding::utf8;

		m_extra_uv = FRead<BYTE>(fp);
		m_size_vi = FRead<BYTE>(fp);
		m_size_ti = FRead<BYTE>(fp);
		m_size_mi = FRead<BYTE>(fp);
		m_size_bi = FRead<BYTE>(fp);
		m_size_moi = FRead<BYTE>(fp);
		m_size_ri = FRead<BYTE>(fp);

		//モデル名と説明文
		name = FReadString(fp);
		name_e = FReadString(fp);
		description = FReadString(fp);
		description_e = FReadString(fp);

		//頂点情報
		int vcount = FRead<int>(fp);
		vertices = vector<PMXVertex>(vcount);
		for (int i = 0; i < vcount; i++)
			vertices[i] = FReadVertex(fp);

		//頂点インデックス情報
		int icount = FRead<int>(fp);
		indices = vector<int>(icount);
		for (int i = 0; i < icount; i++)
			indices[i] = FReadBySizeVertex(fp, m_size_vi);

		//テクスチャ情報
		int tcount = FRead<int>(fp);
		textures = vector<wstring>(tcount);
		for (int i = 0; i < tcount; i++)
			textures[i] = FReadString(fp);

		//材質情報
		int mcount = FRead<int>(fp);
		materials = vector<PMXMaterial>(mcount);
		for (int i = 0; i < mcount; i++)
			materials[i] = FReadMaterial(fp);

		//ボーン情報
		int bcount = FRead<int>(fp);
		bones = vector<PMXBone>(bcount);
		for (int i = 0; i < bcount; i++)
			bones[i] = FReadBone(fp);

		//モーフ情報
		int mocount = FRead<int>(fp);
		morphs = vector<PMXMorph>(mocount);
		for (int i = 0; i < mocount; i++)
			morphs[i] = FReadMorph(fp);

		//表示枠情報
		int ncount = FRead<int>(fp);
		nodes = vector<PMXNode>(ncount);
		for (int i = 0; i < ncount; i++)
			nodes[i] = FReadNode(fp);

		//剛体
		int rcount = FRead<int>(fp);
		bodies = vector<PMXBody>(rcount);
		for (int i = 0; i < rcount; i++)
			bodies[i] = FReadBody(fp);

		//ジョイント
		int jcount = FRead<int>(fp);
		joints = vector<PMXJoint>(jcount);
		for (int i = 0; i < jcount; i++)
			joints[i] = FReadJoint(fp);

		fclose(fp);


		//後処理…「物理＋ボーン位置合わせ」剛体がジョイントで「ボーン追従」剛体以外を親としてつながっている場合は、「物理演算」剛体に変換する
		for (int i = 0; i < joints.size(); i++) {
			const auto& jt = joints[i];
			const auto& A = bodies[jt.bodyA];
			const auto& B = bodies[jt.bodyB];
			if (A.mode != 0 && B.mode == 2 && A.bone != -1 && B.bone != -1) {
				if (bones[B.bone].parent == A.bone) {
					LOG(L"%03d, %sを物理演算剛体に変換します", jt.bodyB, B.name.c_str());
					bodies[jt.bodyB].mode = 1;
				}
			}
		}


	}

	PMXModel::~PMXModel()
	{
		for (auto& mo : morphs)
			delete[] mo.offsets;
	}

	/******************************************************************************************/
	// VMD(Camera)
	/******************************************************************************************/
	VMDCamera VMDCam::FreadCamera(FILE* fp)
	{
		VMDCamera m = {};

		//32バイト一気に読む(iFrame, (x,y,z), (rx,ry,rz) )
		fread_s((void*)&m.iFrame, 32, 32, 1, fp);
		
		//補間データ読み込み
		char ip[24];
		fread_s((void*)ip, 24, 24, 1, fp);
		//先頭24個をfloatに変換してパラメータに入れる
		for (int i = 0; i < 24; i++) {
			m.bezierParams[i] = (float)ip[i] / 127.0f;
		}
		
		// 視野角
		DWORD viewangle;
		fread_s((void*)&viewangle, 4, 4, 1, fp);
		m.viewAngle = (float)viewangle;
		
		// パースペクティブ
		fread_s((void*)&m.parth, 1, 1, 1, fp);

		return m;
	}

	int qs_compareCamera(void* context, const void* arg1, const void* arg2)
	{
		int f1 = ((VMDCamera*)arg1)->iFrame;
		int f2 = ((VMDCamera*)arg2)->iFrame;
		if (f1 > f2) return 1;
		if (f1 < f2) return -1;
		return 0;
	}

	VMDCam::VMDCam(const wchar_t* fname, UINT codepage, bool fps60enable)
	{
		m_codepage = codepage;
		FILE* fp;
		_wfopen_s(&fp, fname, L"rb");
		if (!fp) {
			ThrowIfFailed(E_INVALIDARG, L"can't open VMDfile %s", fname);
			return;
		}

		//ヘッダ
		char header[30] = {};
		fread_s(header, 30, 30, 1, fp);

		if (strcmp(header, "Vocaloid Motion Data 0002") != 0) {
			ThrowIfFailed(E_INVALIDARG, L"%s is unsupported VMD file", fname);
		}

		//モデル名
		char model[21] = {};		//時々20バイト一杯までモデル名が入ってて0で終わってない事があるので
		fread_s(model, 20, 20, 1, fp);
		model[20] = 0;
		auto modelW = strTowstr(model, m_codepage);
		wchar_t* tmp = new wchar_t[modelW.size() + 1];
		wcscpy_s(tmp, modelW.size() + 1, modelW.c_str());
		modelName = tmp;
		delete[] tmp;

		if (strcmp(model, "カメラ・照明\0on Data") != 0) {
			ThrowIfFailed(E_INVALIDARG, L"%s is not camera VMD file", fname);
		}

		//モーション数(空読み)
		int mcount;
		fread_s((void*)&mcount, 4, 4, 1, fp);

		//表情モーフ数(空読み)
		int mocount;
		fread_s((void*)&mocount, 4, 4, 1, fp);

		//カメラ キーフレーム数
		int ccount;
		fread_s((void*)&ccount, 4, 4, 1, fp);
		cameraKeys = vector<VMDCamera>(ccount);

		//モーションデータ読み込み
		for (int i = 0; i < ccount; i++) {
			cameraKeys[i] = FreadCamera(fp);
			if (fps60enable) { cameraKeys[i].iFrame *= 2; }
			lastFrame = max(lastFrame, cameraKeys[i].iFrame);
		}

		//フレーム番号順にソートする
		if (ccount != 0)
			qsort_s(cameraKeys.data(), ccount, sizeof(VMDCamera), qs_compareCamera, nullptr);

		fclose(fp);
	}

	VMDCam::~VMDCam()
	{
	}

	/******************************************************************************************/
	// VMD(Motion)
	/******************************************************************************************/

	VMDMotion VMD::FreadMotion(FILE* fp)
	{
		VMDMotion m = {};

		char boneA[16] = {};
		boneA[15] = 0;	//15バイト目まで\0が無く詰まってる名前対策
		fread_s(boneA, 15, 15, 1, fp);
		wstring boneW = strTowstr(boneA, m_codepage);
		wcsncpy(m.bone, boneW.c_str(), 15);	//転送先バッファのサイズが固定の場合はwcscpy_sじゃなくてwcsncpyを使った方が良いらしい

		//続く32バイト一気に読む(iFrame, position, rotation)
		fread_s((void*)&m.iFrame, 32, 32, 1, fp);

		//補間データ読み込み
		char ip[64];
		fread_s((void*)ip, 64, 64, 1, fp);
		//先頭16個をfloatに変換してパラメータに入れる
		for (int i = 0; i < 16; i++) {
			m.bezierParams[i] = (float)ip[i] / 127.0f;
		}

		return m;
	}

	VMDMorph VMD::FreadMorph(FILE* fp)
	{
		VMDMorph mo = {};

		//モーフ名をwstrに変換
		char buf[16] = {};
		buf[15] = 0;
		fread_s(buf, 15, 15, 1, fp);
		wstring nameW = strTowstr(buf, m_codepage);
		wcsncpy(mo.name, nameW.c_str(), 15);

		//8バイト読む(iFrame, value)
		fread_s((void*)&mo.iFrame, 8, 8, 1, fp);

		return mo;
	}


	int qs_compareMotion(void* context, const void* arg1, const void* arg2)
	{
		int f1 = ((VMDMotion*)arg1)->iFrame;
		int f2 = ((VMDMotion*)arg2)->iFrame;
		if (f1 > f2) return 1;
		if (f1 < f2) return -1;
		return 0;
	}

	int qs_compareMorph(void* context, const void* arg1, const void* arg2)
	{
		int f1 = ((VMDMorph*)arg1)->iFrame;
		int f2 = ((VMDMorph*)arg2)->iFrame;
		if (f1 > f2) return 1;
		if (f1 < f2) return -1;
		return 0;
	}

	VMD::VMD(const wchar_t* fname, UINT codepage, bool fps60enable)
	{
		m_codepage = codepage;
		FILE* fp;
		_wfopen_s(&fp, fname, L"rb");
		if (!fp) {
			ThrowIfFailed(E_INVALIDARG, L"can't open VMDfile %s", fname);
			return;
		}

		//ヘッダ
		char header[30] = {};
		fread_s(header, 30, 30, 1, fp);

		if (strcmp(header, "Vocaloid Motion Data 0002") != 0) {
			ThrowIfFailed(E_INVALIDARG, L"%s is unsupported VMD file", fname);
		}

		//モデル名
		char model[21] = {};		//時々20バイト一杯までモデル名が入ってて0で終わってない事があるので
		fread_s(model, 20, 20, 1, fp);
		model[20] = 0;
		auto modelW = strTowstr(model, m_codepage);
		wchar_t* tmp = new wchar_t[modelW.size() + 1];
		wcscpy_s(tmp, modelW.size() + 1, modelW.c_str());
		modelName = tmp;
		delete[] tmp;
		
		if (strcmp(model, "カメラ・照明\0on Data") == 0) {
			ThrowIfFailed(E_INVALIDARG, L"%s is camera VMD file", fname);
		}

		//モーション数
		int mcount;
		fread_s((void*)&mcount, 4, 4, 1, fp);
		motionKeys = vector<VMDMotion>(mcount);

		//モーションデータ読み込み
		for (int i = 0; i < mcount; i++) {
			motionKeys[i] = FreadMotion(fp);
			if (fps60enable) { motionKeys[i].iFrame *= 2; }
			lastFrame = max(lastFrame, motionKeys[i].iFrame);
		}

		//表情モーフ数
		int mocount;
		fread_s((void*)&mocount, 4, 4, 1, fp);
		morphKeys = vector<VMDMorph>(mocount);
		for (int i = 0; i < mocount; i++) {
			morphKeys[i] = FreadMorph(fp);
			if (fps60enable) { morphKeys[i].iFrame *= 2; }
			lastFrame = max(lastFrame, morphKeys[i].iFrame);
		}

		//フレーム番号順にソートする
		if (mcount != 0)
			qsort_s(motionKeys.data(), mcount, sizeof(VMDMotion), qs_compareMotion, nullptr);
		if (mocount != 0)
			qsort_s(morphKeys.data(), mocount, sizeof(VMDMorph), qs_compareMorph, nullptr);

		fclose(fp);
	}

	VMD::~VMD()
	{
	}


	/******************************************************************************************/
	// PoseSolver
	/******************************************************************************************/

	PoseBone::PoseBone()
	{
		origin = XMFLOAT3(0, 0, 0);

		keyTranslation = XMFLOAT3(0, 0, 0);
		keyRotation = XMQuaternionIdentity();

		appendedRotation = XMQuaternionIdentity();
		appendedTranslation = XMFLOAT3(0, 0, 0);

		IKRotation = XMQuaternionIdentity();

		localTranslation = XMFLOAT3(0, 0, 0);
		localRotation = XMQuaternionIdentity();
		toParent = XMMatrixIdentity();

		transform = XMMatrixIdentity();
	}

	PoseBone::~PoseBone()
	{
	}

	//boneのワールド変換が確定している時に呼ぶ。子孫フレームのtransformをboneの状態の変化に合わせる
	//物理でワールド変換が確定済みのボーンについては再計算しない
	void PoseBone::Propagate(PoseBone* bone)
	{

		if (!bone->transformResolved)
			ThrowIfFailed(E_FAIL, L"bone(%d)'s world transform is unresolved, can't propagate it", bone->iBone);

		for (int i = 0; i < bone->children.size(); i++) {
			auto child = bone->children[i];
			if (!child->transformResolved) {
				child->transform = child->toParent * bone->transform;
				child->transformResolved = true;
			}
			Propagate(child);
		}
	}

	//bone自体か、その親のどこかでローカル変換のみ更新されたのでワールド変換が無効というフラグを立てる
	void PoseBone::PropagateUnresolved(PoseBone* bone)
	{
		bone->transformResolved = false;

		for (int i = 0; i < bone->children.size(); i++)
			PropagateUnresolved(bone->children[i]);
	}

	//ローカル変換だけ更新し、ワールド変換が未確定である事を子孫にも伝える
	void PoseBone::UpdateLocalTransform()
	{
		toParent = oRtMatrix(origin, localRotation, localTranslation);
		PropagateUnresolved(this);
	}


	//ほぼIK用
	//このボーンのワールド変換を得る。ワールド変換が未確定の場合は確定済みの先祖ボーンにさかのぼってワールド変換を確定する
	XMMATRIX PoseBone::ResolveTransform()
	{
		if (transformResolved)
			return transform;

		//ワールド座標が確定された先祖ボーンまで辿る
		vector<PoseBone*> LocalChain(128);	//とりあえず128個あればいいだろ的な
		int nChain = 0;
		PoseBone* pb = this;
		while (!pb->transformResolved) {
			LocalChain[nChain] = pb;
			pb = pb->parent;
			nChain++;
		}

		//先祖ボーンから自分まで順にワールド変換を計算
		XMMATRIX m = pb->transform;
		for (int i = nChain - 1; i >= 0; i--) {
			pb = LocalChain[i];
			m = pb->toParent * m;
			pb->transform = m;
			pb->transformResolved = true;
		}

		return m;
	}

	//map用のデフォルト値が-1になるint
	struct DefaulltMinus1Int {
		int value = -1;
	};

	//セットアップ
	PoseSolver::PoseSolver(Physics* _physics, const PMXModel* _pmx, const VMD* _vmd)
	{
		pmx = _pmx;
		vmd = _vmd;
		physics = _physics;

		//まずボーンの本数だけPoseBoneの配列を作る
		bones.reserve(pmx->bones.size());
		boneMatrices.resize(pmx->bones.size());

		root = new PoseBone;	//-1ボーン
		root->transformResolved = true;

		//bonesにPMXのボーンデータを取り込む
		int i = 0;
		for (const auto& b : pmx->bones) {
			PoseBone* pb = new PoseBone;
			pb->iBone = i;
			pb->origin = b.position;
			bones.push_back(pb);
			i++;
		}

		//bonesの親子関係の構築
		int maxLevel = 0;	//最大変形階層。後で使う
		i = 0;
		for (const auto& b : pmx->bones) {
			maxLevel = max(maxLevel, b.level);
			auto parent = (b.parent == -1) ? root : bones[b.parent];
			bones[i]->parent = parent;				//親ボーンのセット
			parent->children.push_back(bones[i]);	//親ボーンの子リストに追加する
			i++;
		}


		//変形順序テーブルの作成
		vector<int>* orderPerLevelBP = new vector<int>[maxLevel + 1];	//変形階層ごとに入ってるボーンのリスト(物理前)
		vector<int>* orderPerLevelAP = new vector<int>[maxLevel + 1];	//変形階層ごとに入ってるボーンのリスト(物理後)
		bonesBeforePhys = 0;
		i = 0;
		for (int i = 0; i < pmx->bones.size(); i++) {
			auto& b = pmx->bones[i];
			if (b.IsAfterPhysics()) {
				orderPerLevelAP[b.level].push_back(i);
			} else {
				orderPerLevelBP[b.level].push_back(i);
				bonesBeforePhys++;
			}
		}
		TFOrder.reserve(pmx->bones.size());
		for (int i = 0; i <= maxLevel; i++)
			for (int j = 0; j < orderPerLevelBP[i].size(); j++)
				TFOrder.push_back(orderPerLevelBP[i][j]);
		for (int i = 0; i <= maxLevel; i++)
			for (int j = 0; j < orderPerLevelAP[i].size(); j++)
				TFOrder.push_back(orderPerLevelAP[i][j]);

		/*
		LOG(L"%d bones in TFOrder", TFOrder.size());
		for (int i = 0; i < TFOrder.size(); i++) {
			LOG(L"%03d %s", TFOrder[i], pmx->m_bs[TFOrder[i]].name);
		}
		*/

		//付与構造のデバッグ表示
		/*
		vector<bool> fuyoflag(TFOrder.size());
		for (int i = 0; i < fuyoflag.size(); i++)
			fuyoflag[i] = false;	//既に付与構造に挙げられたボーンを重複表示しないためのフラグ
		for (int i = TFOrder.size()-1; i >= 0; i--) {
			wstring s = L"";
			PMXBone b = pmx->m_bs[i];
			bool first = true;
			if (!fuyoflag[i]) {
				while (b.IsAppendRotation() || b.IsAppendTranslation()) {
					if (first) {
						fuyoflag[i] = true;
						s += Format(L"%s (%d, L%d)", b.name, i, b.level);
					}
					PMXBone pb = pmx->m_bs[b.appendParent];
					s += Format(L" <- %s (%d, L%d)", pb.name, b.appendParent, pb.level);
					fuyoflag[b.appendParent] = true;
					b = pb;
					first = false;
				}
				if (s != L"") {
					LOG(L"付与構造 : %s", s.c_str());
				}
			}
		}
		*/

		delete[] orderPerLevelAP;
		delete[] orderPerLevelBP;


		//TODO:現状では名前が15文字以上のボーン・モーフとマッチしない(vmdファイルの仕様上、元が15バイト以上の名前を格納できないので)
		//VMDに含まれるボーン名→ボーン番号の翻訳
		{
			map<wstring, DefaulltMinus1Int> boneDict;
			i = 0;
			for (const auto& b : pmx->bones) {
				boneDict[b.name].value = i;
				i++;
			}
			/*
			for (auto& m : vmd->motionKeys)
				m.iBone = boneDict[m.bone].value;	//-1がセットされている場合は、対応するボーンなし
			*/
			//モーションをボーンごとに仕分ける
			motionKeys = vector<vector<VMDMotion>>(pmx->bones.size());
			for (auto& m : vmd->motionKeys) {
				int idx = boneDict[m.bone].value;
				if (idx != -1)
					motionKeys[idx].push_back(m);
			}
			for (int i = 0; i < pmx->bones.size(); i++) {
				//一つもキーが無いボーンだった場合は、無回転・無移動のパラメータをいれとく
				if (motionKeys[i].size() == 0) {
					VMDMotion m = {};
					//m.iBone = i;
					m.rotation.w = 1;
					m.bezier.Rbx = m.bezier.Rby = m.bezier.Xbx = m.bezier.Xby = m.bezier.Ybx = m.bezier.Yby = m.bezier.Zbx = m.bezier.Zby = 1.0f;
					motionKeys[i].push_back(m);
				}
				//最後のフレーム+1の所に最後のフレームの時と同じ格好のモーションデータを入れておく(補完用)
				VMDMotion m = motionKeys[i].back();
				m.iFrame = vmd->lastFrame + 1;
				motionKeys[i].push_back(m);
				//同様に、必要なら最初のフレームと同じ格好で0フレームに入れておく
				VMDMotion m0 = motionKeys[i][0];
				if (m0.iFrame != 0) {
					m0.iFrame = 0;
					motionKeys[i].insert(motionKeys[i].begin(), m0);
				}
			}
		}

		//同様に、モーフ名→モーフ番号の翻訳
		{
			map<wstring, DefaulltMinus1Int> morphDict;
			for (int i = 0; i < pmx->morphs.size(); i++) {
				PMXMorph mo = pmx->morphs[i];
				morphDict[mo.name].value = i;
			}
			//for (auto& mo : vmd->morphKeys)
			//	mo.iMorph = morphDict[mo.name].value;	//-1がセットされている場合は、対応するモーフなし

			//モーションをモーフごとに仕分け
			int mocount = pmx->morphs.size();
			morphKeys = vector<vector<VMDMorph>>(mocount);
			for (auto& mo : vmd->morphKeys) {
				int idx = morphDict[mo.name].value;
				if (idx != -1)
					morphKeys[idx].push_back(mo);
			}
			for (int i = 0; i < mocount; i++) {
				//一つもキーの無い表情だったら、無表情のパラメータ入れとく
				if (morphKeys[i].size() == 0) {
					VMDMorph mo = {};
					//mo.iMorph = i;
					morphKeys[i].push_back(mo);
				}
				//最後のフレーム+1のとこに同じ状態の表情入れる
				VMDMorph mo = morphKeys[i].back();
				mo.iFrame = vmd->lastFrame + 1;
				morphKeys[i].push_back(mo);
				VMDMorph mo0 = morphKeys[i][0];
				if (mo0.iFrame != 0) {
					mo0.iFrame = 0;
					morphKeys[i].insert(morphKeys[i].begin(), mo0);
				}
			}
		}

		//モーフ値ベクタ初期化
		morphValues.resize(pmx->morphs.size());

		//物理対応
		AttachPhysics();
	}

	PoseSolver::~PoseSolver()
	{
		DetachPhysics();
		delete root;
		for (int i = 0; i < bones.size(); i++)
			delete bones[i];
	}

	int g_recur = 0;

	//ベジェ補間係数y = B(x)を求める
	float BezierT(float x, float ax, float ay, float bx, float by)
	{
		float eps = 4e-3;	//許容誤差。大体1/255くらい。元のデータが0～127だからそんなもんでしょう
		float t = x;
		float k0 = 1 + 3 * (ax - bx);
		float k1 = 3 * (bx - 2 * ax);
		float k2 = 3 * ax;
		for (int i = 0; i < 16; i++) {
			//float delta = k0 * t * t * t + k1 * t * t + k2 * t - x;
			float delta = ((k0 * t + k1) * t + k2) * t - x;
			if (abs(delta) <= eps)
				break;
			t -= delta / 2;
		}
		float r = 1 - t;
		//return t * t * t + 3 * t * t * r * by + 3 * t * r * r * ay;
		return ((t + 3 * r * by) * t + 3 * r * r * ay) * t;
	}


	//VMDのモーションから現在のフレームのローカル変形(位置・回転)を得る。モーフや付与変形は抜きで、各ボーンのキーフレームから得られる情報のみ
	void PoseSolver::GetKeyFrame(float time, int iBone, XMFLOAT3* translation, XMVECTOR* rotation)
	{
		int ib = iBone;

		if (ib < 0) {
			*translation = vec3(0, 0, 0);
			*rotation = vecQ::Identity;
			return;
		}


		float f = max(0, min(time, vmd->lastFrame));
		int targetFrame = floorf(f);
		VMDMotion m0, m1;	//前のフレームがm0, 後ろのフレームがm1

		int lo = 0, hi = motionKeys[ib].size() - 1;
		while (lo <= hi) {
			int mid = (hi + lo) >> 1;
			//二分探索でtargetFrameより後ろにある最初のキーフレームを探す
			if (motionKeys[ib][mid].iFrame <= targetFrame)
				lo = mid + 1;
			else
				hi = mid - 1;
		}
		m1 = motionKeys[ib][lo];
		m0 = motionKeys[ib][lo - 1];

		//必要に応じて補完する
		float t = (float)(f - m0.iFrame) / (float)(m1.iFrame - m0.iFrame);

		if (vec3(m0.position) == vec3(m1.position)) {
			*translation = m0.position;
		} else {
			float tx = BezierT(t, m1.bezier.Xax, m1.bezier.Xay, m1.bezier.Xbx, m1.bezier.Xby);
			float ty = BezierT(t, m1.bezier.Yax, m1.bezier.Yay, m1.bezier.Ybx, m1.bezier.Yby);
			float tz = BezierT(t, m1.bezier.Zax, m1.bezier.Zay, m1.bezier.Zbx, m1.bezier.Zby);
			*translation = Lerp(m0.position, m1.position, vec3(tx, ty, tz));
		}

		if (vecQ(m0.rotation) == vecQ(m1.rotation)) {
			*rotation = vecQ(m0.rotation);
		} else {
			float tr = BezierT(t, m1.bezier.Rax, m1.bezier.Ray, m1.bezier.Rbx, m1.bezier.Rby);
			*rotation = vecQ::Slerp(m0.rotation, m1.rotation, tr);
		}

		//if (iBone == 0) LOG(L"frame:%.2d t:%f B(t):%f", iFrame, t, tx);

		//LOG(L"#%d %s : %s %s", iBone, pmx->m_bs[iBone].name, vec2wstr(*translation).c_str(), quat2wstr(*rotation).c_str());
	}


	//VMDのモーションから現在のフレームのモーフの値を得る
	float PoseSolver::GetKeyFrameMorph(float time, int iMorph)
	{
		int im = iMorph;

		if (im < 0) {
			return 0;
		}

		float f = max(0, min(time, vmd->lastFrame));
		int targetFrame = floorf(f);
		//フレーム番号を0～lastFrameの範囲に限定する
		VMDMorph m0, m1;	//前のフレームがm0, 後ろのフレームがm1

		int lo = 0, hi = morphKeys[im].size() - 1;
		while (lo <= hi) {
			int mid = (hi + lo) >> 1;
			//二分探索でtargetFrameより後ろにある最初のキーフレームを探す
			if (morphKeys[im][mid].iFrame <= targetFrame)
				lo = mid + 1;
			else
				hi = mid - 1;
		}
		m1 = morphKeys[im][lo];
		m0 = morphKeys[im][lo - 1];

		//補完する
		float t = (float)(f - m0.iFrame) / (float)(m1.iFrame - m0.iFrame);
		return Lerp(m0.value, m1.value, t);
	}



	//timeにおけるポーズ(全ボーンの姿勢)を取得する
	void PoseSolver::Solve(float time)
	{
		int mocount = pmx->morphs.size();
		/*
		LARGE_INTEGER freqT, beginT;
		QueryPerformanceFrequency(&freqT);
		QueryPerformanceCounter(&beginT);
		*/

		//キーフレーム読んで各ボーンのローカル変形を取得
		for (auto&& ib : TFOrder)
			GetKeyFrame(time, ib, &bones[ib]->keyTranslation, &bones[ib]->keyRotation);

		//モーフのキーフレーム読む
		for (int i = 0; i < mocount; i++)
			morphValues[i] = GetKeyFrameMorph(time, i);

		//グループモーフ展開
		for (int i = 0; i < mocount; i++) {
			auto& mo = pmx->morphs[i];
			if (mo.kind == 0) {
				PMXGroupMorphOffset* mof = (PMXGroupMorphOffset*)mo.offsets;
				for (int j = 0; j < mo.offsetCount; j++) {
					auto ofs = *(mof + j);
					morphValues[ofs.morph] += morphValues[i] * ofs.ratio;
				}
			}
		}

		//各モーフの処理
		for (int i = 0; i < mocount; i++) {
			auto value = morphValues[i];
			if (value != 0) {
				auto& mo = pmx->morphs[i];
				if (mo.kind == 2) {
					//ボーンモーフなら各ボーンに反映する
					PMXBoneMorphOffset* mof = (PMXBoneMorphOffset*)mo.offsets;
					for (int j = 0; j < mo.offsetCount; j++) {
						auto ofs = *(mof + j);
						int ib = ofs.bone;
						bones[ib]->keyTranslation = bones[ib]->keyTranslation + ofs.translation * value;
						bones[ib]->keyRotation = vecQ(bones[ib]->keyRotation) * vecQ::Slerp(vecQ::Identity, vecQ(ofs.rotation), value);
					}
				}
				//LOG(L"morph %.3d %s %.3f", i, mo.name, value);
			}
		}

		//ローカル変換行列作成と、IK&付与のための準備
		//#pragma omp parallel for
		for (int i = 0; i < TFOrder.size(); i++) {
			PoseBone* b = bones[TFOrder[i]];
			b->localRotation = b->keyRotation;
			b->localTranslation = b->keyTranslation;
			b->toParent = oRtMatrix(b->origin, b->localRotation, b->localTranslation);

			b->IKLinked = false;
			b->IKRotation = vecQ::Identity;
			b->appendedRotation = vecQ::Identity;
			b->appendedTranslation = vec3(0, 0, 0);
			b->transformResolved = false;
		}


		//TODO:物理前後で分ける。物理前にbonesBeforePhys個、物理後に物理の結果をローカル変形に反映させてから残りのボーンを動かすようにすべし
		for (int i = 0; i < TFOrder.size(); i++) {
			int ib = TFOrder[i];
			auto& pmxb = pmx->bones[ib];
			auto& target = bones[ib];

			//LOG(L"processing #%03d %s(bone#%03d,level#%d)", i, pmxb.name, ib, pmxb.level);

			//付与の解決
			if (pmxb.appendParent != -1) {
				auto& aparent = bones[pmxb.appendParent];		//付与親情報
				auto& aparentB = pmx->bones[pmxb.appendParent];

				//ローカル付与はMMDが対応してないのでこっちでも未対応とする
				if (pmxb.IsAppendRotation()) {
					//回転付与
					vecQ q = aparentB.IsAppendRotation() ? aparent->appendedRotation : aparent->keyRotation;

					//IK回転量
					if (aparent->IKLinked)
						q = q * vecQ(aparent->IKRotation);

					target->appendedRotation = vecQ::Slerp(vecQ::Identity, q, pmxb.appendRatio);
				}


				if (pmxb.IsAppendTranslation()) {
					//移動付与
					vec3 tr = aparentB.IsAppendTranslation() ? aparent->appendedTranslation : aparent->keyTranslation;
					target->appendedTranslation = tr * pmxb.appendRatio;
				}

				//ローカル・グローバル変換の再計算(rotationはXMVECTORなので普通に*=するとクォータニオン扱いされず変になる)
				target->localRotation = vecQ(target->appendedRotation) * vecQ(target->keyRotation) * vecQ(target->IKRotation);
				target->localTranslation = target->appendedTranslation + target->keyTranslation;
				target->UpdateLocalTransform();
			}

			//IKボーンの場合はIKを解決する→IKリンクボーンのローカル変換が変更される
			if (pmxb.IsIK()) {
				IK(ib);
			}
		}

		//最終結果を反映
		root->Propagate(root);
		FinishMatrices();


		//ベンチマーク
		/*
		LARGE_INTEGER endT;
		QueryPerformanceCounter(&endT);
		double elapsed_time = (double)(endT.QuadPart - beginT.QuadPart) * 1E+6 / freqT.QuadPart;
		::LOG(L"*** %.2lf μs for solve pose overall***", elapsed_time);
		*/
	}

	void PoseSolver::FinishMatrices()
	{
		int i = 0;
		for (const auto& b : bones) {
			boneMatrices[i] = XMMatrixTranspose(b->transform);
			i++;
		}
	}


	//rootへの回転変換をセットし、親への変換を逆算する(IKの前後ではtranslationは不変)
	//実にIK的な関数
	void IKSetLocalFromGlobalRotation(PoseBone* bone, vecQ q)
	{
		vecQ oldQ = bone->localRotation;	//IK前のローカル回転変換
		vecQ newQ = q * Conjugate(Quaternion::CreateFromRotationMatrix(bone->parent->transform));	//IK後のローカル回転

		bone->localRotation = newQ;
		bone->IKRotation = Conjugate(oldQ) * newQ;
		bone->UpdateLocalTransform();

		//これをやらないと、この関数自体の計算にbone->parent->transformを使うので、今計算しているボーンのワールド座標が他のIKリンクから参照される場合おかしくなる
		bone->transform = bone->toParent * bone->parent->transform;
		bone->transformResolved = true;
	}

	//IK用。ps[idx]を中心として自分より末端のIKリンク全部とターゲットをqで回転させる
	void IKApplyTransform(int idx, vector<vec3>& ps, vector<vecQ>& Qs, vecQ q, vec3& target)
	{
		for (int i = 0; i <= idx; i++) {
			ps[i] = vec3::Transform(ps[i] - ps[idx], q) + ps[idx];
			Qs[i] *= q;
		}
		target = vec3::Transform(target - ps[idx], q) + ps[idx];
	}

	//可動範囲制限の有る関節のヒンジ軸を得る
	vec3 IKHingeAxis(const PMXIKLink& link)
	{
		vec3 hinge;
		vec3 range = link.hi - link.low;
		if (range.x >= range.y && range.x >= range.z) {
			hinge = (abs(link.hi.x) > abs(link.low.x) ? vec3(Sign(link.hi.x), 0, 0) : vec3(Sign(link.low.x), 0, 0));
		} else if (range.y >= range.x && range.y >= range.z) {
			hinge = (abs(link.hi.y) > abs(link.low.y) ? vec3(0, Sign(link.hi.y), 0) : vec3(0, Sign(link.low.y), 0));
		} else {
			hinge = (abs(link.hi.z) > abs(link.low.z) ? vec3(0, 0, Sign(link.hi.z)) : vec3(0, 0, Sign(link.low.z)));
		}
		return hinge;
	}

	//角度制限するための回転を返す
	vecQ IKLimitEulerQ(int j, vector<PMXIKLink>& links, vector<PoseBone*>& linkBs, vector<vecQ>& Qs)
	{
		vecQ parentQ = (j < links.size() - 1) ? Qs[j + 1] : vecQ::CreateFromRotationMatrix(linkBs.back()->parent->transform);
		vecQ relQ = Qs[j] * Conjugate(parentQ);
		//相対回転をオイラー角で解釈して範囲制限
		vec3 euler = relQ.ToEuler();
		euler.Clamp(links[j].low, links[j].hi);
		vecQ limitedQ = vecQ::CreateFromYawPitchRoll(euler) * parentQ;//範囲制限後の姿勢
		vecQ henkaQ = Conjugate(Qs[j]) * limitedQ; //この時点での回転をキャンセルして制限後の姿勢に持っていくための回転
		return henkaQ;
	}

	//IKの解決
	//注意：IKリンクの子ボーンが枝分かれする場合を考えていない
	// //IKリンクの下にIKリンク & 非IKリンクボーンで枝分かれは不可。IKリンクの下に複数のIKリンクで枝分かれも不可
	//一旦非IKリンクボーン1本挟んでから、そのボーンから枝分かれはOK
	void PoseSolver::IK(int iBone)
	{
		int i = iBone;
		auto& pmxb = pmx->bones[i];
		auto& targetB = bones[pmxb.IKTarget];

		//IKリンクカウント0とかいう場合は帰る(なんかそういうモデルは実際ある)
		if (pmxb.IKLinkCount == 0)
			return;

		//関連ボーンのワールド変換の確定
		bones[i]->ResolveTransform();
		targetB->ResolveTransform();

		//特に断りが無い限り座標系はroot座標系
		vec3 goal = vec3::Transform(bones[i]->origin, bones[i]->transform);	//IKボーンの位置
		vec3 target = vec3::Transform(targetB->origin, targetB->transform);	//ターゲットボーン(末端の点)の位置

		//最初から十分近づいてるなら何もしない
		if ((target - goal).LengthSquared() < 1e-4) {
			return;
		}

		//IKの目標：リンクボーンを回転させてターゲットボーンの位置(target)をIKボーン(goal)へ近づける
		// 正直、用語的にどうなんだろう、IKボーンの位置がtargetでターゲットボーンはend effectorとかじゃないかいう気はするが仕方ない

		//末端のリンクボーンから順にlinksに
		vector<PMXIKLink>links(pmxb.IKLinkCount);
		vector<PoseBone*>linkBs(pmxb.IKLinkCount);
		vector<vec3>ps(pmxb.IKLinkCount);	//IKの各点のグローバル位置。ps[0]が末端のリンク
		vector<vecQ>Qs(pmxb.IKLinkCount);
		for (int j = 0; j < pmxb.IKLinkCount; j++) {
			links[j] = pmxb.IKLinks[j];
			linkBs[j] = bones[links[j].bone];
			linkBs[j]->IKLinked = true;
			Matrix m = linkBs[j]->ResolveTransform();
			ps[j] = vec3::Transform(linkBs[j]->origin, m);//各ボーンの原点のグローバル位置
			Qs[j] = vecQ::CreateFromRotationMatrix(m);	 //グローバル回転。注意：スケール入りの行列不可
		}


		if (pmxb.IKLinkCount == 1) {
			//LOG(L"Solving 1Link IK:%s(%02d), Link:%s(%02d), Target:%s(%02d)", pmxb.name, i, pmx->m_bs[links[0].bone].name, links[0].bone, pmx->m_bs[pmxb.IKTarget].name, pmxb.IKTarget);
			//IKリンクが1つしかない場合、単純にgoalの方向を向けるだけ
			vecQ q = vecQ::FromToRotation(target - ps[0], goal - ps[0]);
			IKApplyTransform(0, ps, Qs, q, target);
			//角度制限
			if (links[0].isLimit) {
				vecQ henkaQ = IKLimitEulerQ(0, links, linkBs, Qs);
				IKApplyTransform(0, ps, Qs, henkaQ, target);
			}
			//root座標系で計算してたのでローカル変換に戻す
			IKSetLocalFromGlobalRotation(linkBs[0], Qs[0]);
		} else if (pmxb.IKLinkCount == 2) {
			//2ボーンIK
			// ps[1]根元、ps[0]がジョイント、targetが先っちょとする
			// 注意　①根元の可動範囲制限が考慮されてない　②関節の可動範囲は1軸に限定される
			float A = Length(ps[1] - ps[0]), B = Length(ps[0] - target), C = Length(ps[1] - goal);
			float rad = acos(Clamp((A * A + B * B - C * C) / (2 * A * B), -1, 1));	//余弦定理でps[0]を何度にすればいいか調べる
			//LOG(L"solving 2bone IK for %s deg:%.2f", pmxb.name, rad/PI*180);

			//関節の回転軸を決める
			vec3 axis = vec3(0, 1, 0);
			float range = PI;	//可動範囲
			float minrange = 0;
			if (links[0].isLimit) {
				//hiとlowで0から見て可動範囲が大きい方に曲げるように軸が選ばれる
				axis = IKHingeAxis(links[0]);
				//可動範囲が大きい方に最大何度曲げられるか？
				float hidot = Dot(links[0].hi, axis);
				float lodot = Dot(links[0].low, axis);
				range = max(abs(hidot), abs(lodot));
				minrange = (Sign(hidot) != Sign(lodot)) ? 0 : min(abs(hidot), abs(lodot));
			} else {
				//可動範囲の制限が無い場合はCrossで求める…書いたけどまだ検証してない
				vec3 scale, trans;
				vecQ toLocal;
				Matrix(linkBs[1]->parent->transform).Decompose(scale, toLocal, trans);	//注意：スケールをまだ考慮してない
				vec3 b10 = Rotate(ps[0] - ps[1], toLocal);	//根元の関節の座標系での向きを知りたいので
				vec3 b0g = Rotate(goal - ps[0], toLocal);
				axis = Normalize(Cross(b10, b0g));
				//一直線になってたらとりあえずZ軸とする
				if (Length(axis) == 0) {
					axis = vec3(0, 0, 1);
				}
			}

			// 関節を初期状態にした時の角度を求める
			A = Length(linkBs[0]->origin - linkBs[1]->origin);
			B = Length(linkBs[0]->origin - targetB->origin);
			C = Length(linkBs[1]->origin - targetB->origin);
			float straight = acos(Clamp((A * A + B * B - C * C) / (2 * A * B), -1, 1));
			minrange = max(minrange, PI - straight);	//初期状態より伸びないようにする

			//一旦、関節を完全に伸ばす
			vecQ mageQ = vecQ::FromToRotation(target - ps[0], ps[0] - ps[1]);

			//ヒンジ軸周りに回す(axisはそのままだとlinkBs[1]の座標系なので、ワールドに変換する)
			mageQ *= vecQ::CreateFromAxisAngle(vec3::TransformNormal(axis, linkBs[1]->transform), max(minrange, min(PI - rad, range)));
			IKApplyTransform(0, ps, Qs, mageQ, target);

			//最後に、根元の方向をゴール方向へ向ける
			vecQ q2 = vecQ::FromToRotation(target - ps[1], goal - ps[1]);
			IKApplyTransform(1, ps, Qs, q2, target);

			//debug
			//A = Length(ps[1] - ps[0]); B = Length(ps[0] - target); C = Length(ps[1] - target);
			//LOG(L"result deg:%.2f", acos(Clamp((A* A + B * B - C * C) / (2 * A * B), -1, 1)) * 180 / PI);

			//root座標系で計算してたのでローカル変換に戻す(根元→関節の順)
			IKSetLocalFromGlobalRotation(linkBs[1], Qs[1]);
			IKSetLocalFromGlobalRotation(linkBs[0], Qs[0]);

			//debug
			//target = vec3::Transform(targetB->origin, targetB->transform);
			//LOG(L"solved T%s  /  G%s deg:%.2f, err:%.2f", vec2wstr(target).c_str(), vec2wstr(goal).c_str(), rad/PI*180.0f, Length(target-goal));
		} else {
			//CCD IK
			// 
			//LOG(L"starting resolve IK %s", pmxb.name);

			//グローバル回転をリストにいれとく

			//ヒストリーバッファ
			int numHistory = pmxb.IKLinkCount * pmxb.IKLoopCount;
			vec3* pH = new vec3[numHistory];
			vecQ* QH = new vecQ[numHistory];
			float* errH = new float[pmxb.IKLoopCount];

			//ヒンジ軸。とりあえず可動範囲制限は1軸可動のみサポート
			vector<vec3>hinges;	hinges.resize(pmxb.IKLinkCount);
			for (int j = 0; j < pmxb.IKLinkCount; j++)
				hinges[j] = IKHingeAxis(links[j]);

			//根元からゴールまでの直線距離
			float goalLength = Length(ps[0] - goal);
			//根元からターゲットまでの各関節の長さの合計
			float totalJointsLength = 0;
			for (int j = 0; j < links.size(); j++) {
				if (j == 0)
					totalJointsLength += Length(ps[j] - target);
				else
					totalJointsLength += Length(ps[j] - ps[j - 1]);
			}

			//全部のボーンとゴールが一直線になってる事に対策する。ルート→ターゲットの距離がルート→ゴールの距離未満になるよう関節を曲げとく
			for (int iter = 0; iter < 20; iter++) {
				if (Length(target - ps[0]) < goalLength) {
					break;
				}
				for (int j = 1; j < pmxb.IKLinkCount; j++) {
					if (links[j].isLimit) {
						vecQ parentQ = (j < pmxb.IKLinkCount - 1) ? Qs[j + 1] : vecQ::CreateFromRotationMatrix(linkBs.back()->parent->transform);
						vecQ hogQ = Conjugate(parentQ) * Normalize(vecQ(hinges[j], PI / 5.5)) * parentQ;	//normalizeがあるので
						IKApplyTransform(j, ps, Qs, hogQ, target);
					}
				}
			}

			//target-goalの距離の2乗がこの値以下になったら終了
			float threshold = 0.01;

			//ps, QsについてCCD-IKを解く
			vec3 last_target = goal;
			int loops = 0;
			float leastErr = 1E+10;	//今まででエラーが最小になってた時のエラー値
			for (int iter = 0; iter < pmxb.IKLoopCount; iter++) {
				//収束判定
				float err = (target - goal).LengthSquared();
				if (err < threshold)	//収束した
					break;
				if (err > leastErr * 2)	//発散に向かっているので打ち切る
					break;
				leastErr = min(leastErr, err);

				//ロックが発生しているか関節の長さより遠い所にゴールがある
				if ((last_target - target).LengthSquared() < threshold) {
					//関節の長さより遠い所にゴールがあるなら、伸ばし切ってると思われるため終わる
					if (totalJointsLength <= goalLength)
						break;
					/*
					//ロックが発生してる。ほぐす
					for (int j = 0; j < links.size(); j++) {
						//ヒンジ軸回りに少し曲げる
						if (links[j].isLimit) {
							vecQ parentQ = (j < links.size() - 1) ? Qs[j + 1] : vecQ::CreateFromRotationMatrix(linkBs.back()->parent->transform);
							vecQ hogQ = Conjugate(parentQ) * Normalize(vecQ(hinges[j], 0.1)) * parentQ;
							IKApplyTransform(j, ps, Qs, hogQ, target);
						}
					}
					*/
				}

				last_target = target;
				//末端ボーンから順にtarget向き→goal向きへと回転させる
				for (int j = 0; j < pmxb.IKLinkCount; j++) {
					//軸制限の有無で共通の部分。まず、自由に動ける場合の回転qを決定
					vecQ q = vecQ::FromToRotation(target - ps[j], goal - ps[j]);
					float rad = acos(q.w) * 2;	//qは何ラジアンの回転か？
					if (abs(rad) > 1e-5)
						q = vecQ::Slerp(vecQ::Identity, q, min(1, pmxb.IKAngle / rad));	//単位角までの角度に制限する

					if (links[j].isLimit) {
						//回転の適用
						IKApplyTransform(j, ps, Qs, q, target);

						//↑の回転適用後の姿勢について、一つ根元寄りのリンクに対する相対的な回転を得て、適用
						vecQ henkaQ = IKLimitEulerQ(j, links, linkBs, Qs);
						IKApplyTransform(j, ps, Qs, henkaQ, target);
					} else {
						//軸制限なしならそのまま回転の適用
						IKApplyTransform(j, ps, Qs, q, target);
					}
				}

				for (int j = 0; j < pmxb.IKLinkCount; j++) {
					int hIdx = loops * pmxb.IKLinkCount + j;
					pH[hIdx] = ps[j];
					QH[hIdx] = Qs[j];
				}
				errH[loops] = (target - goal).LengthSquared();

				//LOG(L"loops#%.2d %s  : %f to go", loops, vec2wstr(target).c_str(), Length(target - goal));
				loops++;
			}

			//一番誤差が低かった時のps・Qsを採用
			int bestIdx = 0;
			float best = 1E+10;
			for (int j = 0; j < loops; j++) {
				if (best > errH[j] && isfinite(errH[j])) {
					best = errH[j];
					bestIdx = j;
				}
			}
			//LOG(L"bestIdx = %d", bestIdx);
			for (int j = 0; j < pmxb.IKLinkCount; j++) {
				int hIdx = bestIdx * pmxb.IKLinkCount + j;
				ps[j] = pH[hIdx];
				Qs[j] = QH[hIdx];
			}


			//解けたら根元から順にps,Qsから、rootへの変換を計算し、さらに親への変換も得る
			vec3 o;
			for (int j = pmxb.IKLinkCount - 1; j >= 0; j--) {
				//LOG(L"ps[%d] = %s", j, vec2wstr(ps[j]).c_str());
				IKSetLocalFromGlobalRotation(linkBs[j], Qs[j]);
			}


			//debug
			//target = vec3::Transform(targetB->origin, targetB->transform);
			//LOG(L"solved #%d, T%s  /  G%s ", loops, vec2wstr(target).c_str(), vec2wstr(goal).c_str());

			delete[] pH;
			delete[] QH;
			delete[] errH;
		}// CCD-IK

	}// IK()


	/******************************************************************************************/
	// 物理 : benikabocha様作 sabaを 大いに参考にさせていただきました
	/******************************************************************************************/
	struct MMDFilterCallback : public btOverlapFilterCallback
	{
		bool needBroadphaseCollision(btBroadphaseProxy* proxy0, btBroadphaseProxy* proxy1) const override
		{
			auto findIt = std::find_if(
				m_nonFilterProxy.begin(),
				m_nonFilterProxy.end(),
				[proxy0, proxy1](const auto& x) {return x == proxy0 || x == proxy1; }
			);
			if (findIt != m_nonFilterProxy.end()) {
				return true;
			}
			bool collides = (proxy0->m_collisionFilterGroup & proxy1->m_collisionFilterMask) != 0;
			collides = collides && (proxy1->m_collisionFilterGroup & proxy0->m_collisionFilterMask);
			return collides;
		}

		std::vector<btBroadphaseProxy*> m_nonFilterProxy;
	};

	//ボーンと関連づいてない剛体用
	class DefaultMotionState : public MMDMotionState {
	private:
		btTransform	m_initialTransform;
		btTransform	m_transform;
	public:
		DefaultMotionState(const XMMATRIX& transform)
		{
			Matrix tr = transform;
			m_transform.setFromOpenGLMatrix(&tr._11);
			m_initialTransform = m_transform;
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			worldTransform = m_transform;
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
			m_transform = worldTransform;
		}

		virtual void Reset() override
		{
			m_transform = m_initialTransform;
		}

		virtual void ReflectGlobalTransform() override
		{
		}
	};

	//物理演算剛体用
	class DynamicMotionState : public MMDMotionState {
	private:
		PoseBone* m_bone;
		Matrix	m_offset;		//剛体座標系→bone座標系変換
		Matrix	m_invOffset;	//bone座標系→剛体座標系変換
		btTransform	m_transform;
		bool		m_override;

		btTransform	m_initialTransform;
	public:
		DynamicMotionState(PoseBone* bone, const XMMATRIX& offset, bool override = true)
			: m_bone(bone), m_offset(offset), m_override(override)
		{
			//追加
			Matrix tr = offset;
			m_transform.setFromOpenGLMatrix(&tr._11);
			m_initialTransform = m_transform;

			m_invOffset = Matrix(offset).Invert();
			Reset();
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			worldTransform = m_transform;
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
			m_transform = worldTransform;
		}

		void Reset() override
		{
			Matrix global = m_offset * Matrix(m_bone->transform);
			m_transform.setFromOpenGLMatrix(&global._11);

			//↑から↓に変更
			//m_transform = m_initialTransform;
		}

		void ReflectGlobalTransform() override
		{
			if (m_override) {
				Matrix world;	//剛体→ワールド変換
				m_transform.getOpenGLMatrix(&world._11);
				Matrix btGlobal = m_invOffset * world;
				m_bone->transform = btGlobal;
				//TODO:物理後変形の場合の事を考える
				m_bone->PropagateUnresolved(m_bone);	//自分のボーンのワールド変換は確定済みだが子孫ボーンのワールド変換は未定、という状態にする(自分のローカル変換はおかしな状態になっている)
				m_bone->transformResolved = true;
			}
		}

	};

	//物理+ボーン位置合わせ剛体用...移動分はボーンの位置、回転分だけ物理というボーン
	class DynamicAndBoneMergeMotionState : public MMDMotionState
	{
	private:
		PoseBone* m_bone;
		Matrix	m_offset;
		Matrix m_invOffset;
		btTransform	m_transform;
		bool		m_override;

		btTransform m_initialTransform;
	public:
		DynamicAndBoneMergeMotionState(PoseBone* bone, const XMMATRIX& offset, bool override = true)
			: m_bone(bone)
			, m_offset(offset)
			, m_override(override)
		{
			//追加
			Matrix tr = offset;
			m_transform.setFromOpenGLMatrix(&tr._11);
			m_initialTransform = m_transform;

			m_invOffset = Matrix(offset).Invert();
			Reset();
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			worldTransform = m_transform;
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
			m_transform = worldTransform;
		}

		void Reset() override
		{
			Matrix global = m_offset * Matrix(m_bone->transform);
			m_transform.setFromOpenGLMatrix(&global._11);

			//↑から↓に変更
			//m_transform = m_initialTransform;
		}

		void ReflectGlobalTransform() override
		{
			//メモ m_offset : rigid->bone 行列

			if (m_override) {
				/* sabaの実装だと位置合わせのためのコードが↓にあるけどなんか崩れてしまうので
				Matrix world;
				m_transform.getOpenGLMatrix(&world._11);
				Matrix btGlobal = m_invOffset * world;
				Matrix global = m_bone->transform;
				btGlobal._41 = global._41;
				btGlobal._42 = global._42;
				btGlobal._43 = global._43;
				m_bone->transform = btGlobal;
				m_bone->PropagateUnresolved(m_bone);
				m_bone->transformResolved = true;
				*/

				//OpenGLとDirectXMathの流儀で行列を掛ける順序が逆なのでフレームを合わせる側もおそらく逆になるのだと思う
				m_bone->ResolveTransform();
				Matrix world;
				m_transform.getOpenGLMatrix(&world._11);
				Matrix global = m_offset * m_bone->transform;
				world._41 = global._41;	world._42 = global._42;	world._43 = global._43;
				m_bone->transform = m_invOffset * world;
				m_bone->PropagateUnresolved(m_bone);
				m_bone->transformResolved = true;
			}
		}


	};

	//ボーン追従剛体用
	class KinematicMotionState : public MMDMotionState {
	private:
		PoseBone* m_bone;
		XMMATRIX m_offset;
	public:
		KinematicMotionState(PoseBone* bone, const XMMATRIX& offset) : m_bone(bone), m_offset(offset) {}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			Matrix m;
			if (m_bone != nullptr)
				m = Matrix(m_offset) * Matrix(m_bone->transform);
			else
				m = m_offset;

			worldTransform.setFromOpenGLMatrix(&m._11);
		}

		void setWorldTransform(const btTransform& worldTransform) override {}
		void Reset() override {}
		void ReflectGlobalTransform() override {}
	};



	Physics::Physics()
	{
		m_broadphase = std::make_unique<btDbvtBroadphase>();
		m_collisionConfig = std::make_unique<btDefaultCollisionConfiguration>();
		m_dispatcher = std::make_unique<btCollisionDispatcher>(m_collisionConfig.get());

		m_solver = std::make_unique<btSequentialImpulseConstraintSolver>();

		m_world = std::make_unique<btDiscreteDynamicsWorld>(
			m_dispatcher.get(),
			m_broadphase.get(),
			m_solver.get(),
			m_collisionConfig.get()
		);

		m_world->setGravity(btVector3(0, -9.8f * 10.0f, 0));

		m_groundShape = std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0.0f);

		btTransform groundTransform;
		groundTransform.setIdentity();

		m_groundMS = std::make_unique<btDefaultMotionState>(groundTransform);

		btRigidBody::btRigidBodyConstructionInfo groundInfo(0, m_groundMS.get(), m_groundShape.get(), btVector3(0, 0, 0));
		m_groundRB = std::make_unique<btRigidBody>(groundInfo);

		m_world->addRigidBody(m_groundRB.get());

		auto filterCB = std::make_unique<MMDFilterCallback>();
		filterCB->m_nonFilterProxy.push_back(m_groundRB->getBroadphaseProxy());
		m_world->getPairCache()->setOverlapFilterCallback(filterCB.get());
		m_filterCB = std::move(filterCB);
	}

	Physics::~Physics()
	{
		if (m_world != nullptr && m_groundRB != nullptr)
			m_world->removeRigidBody(m_groundRB.get());

		m_broadphase = nullptr;
		m_collisionConfig = nullptr;
		m_dispatcher = nullptr;
		m_solver = nullptr;
		m_world = nullptr;
		m_groundShape = nullptr;
		m_groundMS = nullptr;
		m_groundRB = nullptr;
	}

	void Physics::Update(double dt)
	{
		for (auto& n : m_notifees)
			n->OnUpdatePhysicsBegin();

		if (m_world != nullptr)
			m_world->stepSimulation(dt, maxSubsteps, static_cast<btScalar>(1.0 / fps));

		for (auto& n : m_notifees)
			n->OnUpdatePhysicsEnd();
	}

	void Physics::AddRigidBody(RigidBody* rb)
	{
		m_world->addRigidBody(rb->rigidBody.get(), 1 << rb->group, rb->groupMask);
	}

	void Physics::RemoveRigidBody(RigidBody* rb)
	{
		m_world->removeRigidBody(rb->rigidBody.get());
	}

	void Physics::AddJoint(Joint* joint)
	{
		if (joint->constraint != nullptr)
			m_world->addConstraint(joint->constraint.get());
	}

	void Physics::RemoveJoint(Joint* joint)
	{
		if (joint->constraint != nullptr)
			m_world->removeConstraint(joint->constraint.get());
	}

	void Physics::AddNotifee(PoseSolver* solver)
	{
		m_notifees.push_back(solver);
	}

	void Physics::DeleteNotifee(PoseSolver* solver)
	{
		auto idx = find(m_notifees.begin(), m_notifees.end(), solver);
		m_notifees.erase(idx);
	}

	void Physics::Reset()
	{
		for (auto& n : m_notifees)
			n->OnResetPhysicsBegin();

		//↓は別にいらんのではないかという気がするのでやらないことにした
		//Updateを呼ぶとイベントハンドラも呼ばれるので直接stepSimulationする
		//if (m_world != nullptr)
		//	m_world->stepSimulation(1/60.0, maxSubsteps, static_cast<btScalar>(1.0 / fps));

		for (auto& n : m_notifees)
			n->OnResetPhysicsEnd();
	}

	void Physics::Prewarm(int frames, float timePerFrame)
	{

		for (int i = 0; i < frames; i++) {
			for (auto& n : m_notifees)
				n->OnPrewarmPhysics(i, frames);

			Update(timePerFrame);

			//OnPrewarmPhysicsはi==0の時に初期スタンスになるので、この時Reset()して剛体の運動量を0にする
			if (i == 0)
				Reset();
		}
		Reset();
	}


	//model内のiRB番の剛体情報をもとに、Bullet用剛体オブジェクトを作る
	RigidBody::RigidBody(const PMXModel* model, int iRB, const PoseSolver* solver)
	{
		m_physics = solver->physics;

		PMXBody r = model->bodies[iRB];
		switch (r.boxKind) {
		case 0:
			m_shape = std::make_unique<btSphereShape>(r.boxSize.x);
			break;
		case 1:
			m_shape = std::make_unique<btBoxShape>(btVector3(r.boxSize.x, r.boxSize.y, r.boxSize.z));
			break;
		case 2:
			m_shape = std::make_unique<btCapsuleShape>(r.boxSize.x, r.boxSize.y);
			break;
		default:
			break;
		}

		//m_shape->setMargin(0.1);

		btScalar mass(0);
		btVector3 localInertia(0, 0, 0);
		PoseBone* b = r.bone >= 0 ? solver->bones[r.bone] : nullptr;
		bone = b;

		if (r.mode != 0) {
			mass = r.mass;
		}
		if (mass != 0) {
			m_shape->calculateLocalInertia(mass, localInertia);
		}

		Matrix rotMat = Matrix::CreateFromYawPitchRoll(r.rotation);
		Matrix translateMat = Matrix::CreateTranslation(r.position);
		Matrix rbMat = rotMat * translateMat;

		m_offsetMat = (b != nullptr) ? rbMat * Matrix(b->transform).Invert() : rbMat * Matrix(solver->root->transform).Invert();

		btMotionState* motionState = nullptr;


		if (r.mode == 0) {
			m_kinematicMotionState = std::make_unique<KinematicMotionState>(b, m_offsetMat);
			motionState = m_kinematicMotionState.get();
		} else {
			if (b != nullptr) {
				if (r.mode == 1) {
					m_activeMotionState = std::make_unique<DynamicMotionState>(b, m_offsetMat);
					m_kinematicMotionState = std::make_unique<KinematicMotionState>(b, m_offsetMat);
					motionState = m_activeMotionState.get();
				} else if (r.mode == 2) {
					m_activeMotionState = std::make_unique<DynamicAndBoneMergeMotionState>(b, m_offsetMat);
					m_kinematicMotionState = std::make_unique<KinematicMotionState>(b, m_offsetMat);
					motionState = m_activeMotionState.get();
				}
			} else {
				m_activeMotionState = std::make_unique<DefaultMotionState>(m_offsetMat);
				m_kinematicMotionState = std::make_unique<KinematicMotionState>(b, m_offsetMat);
				motionState = m_activeMotionState.get();
			}
		}

		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, m_shape.get(), localInertia);
		rbInfo.m_linearDamping = r.positionDamping;
		rbInfo.m_angularDamping = r.rotationDamping;
		rbInfo.m_restitution = r.restitution;
		rbInfo.m_friction = r.friction;
		rbInfo.m_additionalDamping = true;

		rigidBody = std::make_unique<btRigidBody>(rbInfo);
		rigidBody->setUserPointer(this);
		rigidBody->setSleepingThresholds(0.01f, 0.1f * PI / 180.0f);
		rigidBody->setActivationState(DISABLE_DEACTIVATION);
		if (r.mode == 0)
			rigidBody->setCollisionFlags(rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);

		mode = r.mode;
		group = r.group;
		groupMask = r.passGroup;
	}

	void RigidBody::SetActivation(bool active)
	{
		if (mode != 0) {
			if (active) {
				rigidBody->setCollisionFlags(rigidBody->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
				rigidBody->setMotionState(m_activeMotionState.get());
			} else {
				rigidBody->setCollisionFlags(rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
				rigidBody->setMotionState(m_kinematicMotionState.get());
			}
		} else {
			rigidBody->setMotionState(m_kinematicMotionState.get());
		}
	}

	void RigidBody::ResetTransform()
	{
		if (m_activeMotionState != nullptr)
			m_activeMotionState->Reset();
	}

	void RigidBody::Reset()
	{
		auto cache = m_physics->m_world->getPairCache();
		if (cache != nullptr) {
			auto dispatcher = m_physics->m_world->getDispatcher();
			cache->cleanProxyFromPairs(rigidBody->getBroadphaseHandle(), dispatcher);
		}
		rigidBody->setAngularVelocity(btVector3(0, 0, 0));
		rigidBody->setLinearVelocity(btVector3(0, 0, 0));
		rigidBody->clearForces();
	}

	void RigidBody::ReflectGlobalTransform()
	{
		if (m_activeMotionState != nullptr)
			m_activeMotionState->ReflectGlobalTransform();

		if (m_kinematicMotionState != nullptr)
			m_kinematicMotionState->ReflectGlobalTransform();
	}

	void RigidBody::CalcLocalTransform()
	{
		if (bone != nullptr) {
			auto parent = bone->parent;
			if (parent != nullptr) {
				auto local = Matrix(bone->transform) * Matrix(parent->transform).Invert();
				bone->toParent = local;
			}
		}
	}

	XMMATRIX RigidBody::GetTransform()
	{
		btTransform transform = rigidBody->getCenterOfMassTransform();
		Matrix mat;
		transform.getOpenGLMatrix(&mat._11);
		return mat;
	}

	Joint::Joint(const PMXModel* model, int index, RigidBody* rigidBodyA, RigidBody* rigidBodyB)
	{
		PMXJoint pmxJoint = model->joints[index];
		btMatrix3x3 rotMat;
		rotMat.setEulerZYX(pmxJoint.rotation.x, pmxJoint.rotation.y, pmxJoint.rotation.z);

		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(pmxJoint.position.x, pmxJoint.position.y, pmxJoint.position.z));
		transform.setBasis(rotMat);

		btTransform invA = rigidBodyA->rigidBody->getWorldTransform().inverse();
		btTransform invB = rigidBodyB->rigidBody->getWorldTransform().inverse();
		invA = invA * transform;
		invB = invB * transform;

		auto constraint = std::make_unique<btGeneric6DofSpringConstraint>(*rigidBodyA->rigidBody, *rigidBodyB->rigidBody, invA, invB, true);
		constraint->setLinearLowerLimit(btVector3(pmxJoint.moveLo.x, pmxJoint.moveLo.y, pmxJoint.moveLo.z));
		constraint->setLinearUpperLimit(btVector3(pmxJoint.moveHi.x, pmxJoint.moveHi.y, pmxJoint.moveHi.z));

		constraint->setAngularLowerLimit(btVector3(pmxJoint.angleLo.x, pmxJoint.angleLo.y, pmxJoint.angleLo.z));
		constraint->setAngularUpperLimit(btVector3(pmxJoint.angleHi.x, pmxJoint.angleHi.y, pmxJoint.angleHi.z));

		if (pmxJoint.springMove.x != 0) {
			constraint->enableSpring(0, true);
			constraint->setStiffness(0, pmxJoint.springMove.x);
		}
		if (pmxJoint.springMove.y != 0) {
			constraint->enableSpring(1, true);
			constraint->setStiffness(1, pmxJoint.springMove.y);
		}
		if (pmxJoint.springMove.z != 0) {
			constraint->enableSpring(2, true);
			constraint->setStiffness(2, pmxJoint.springMove.z);
		}
		if (pmxJoint.springRotate.x != 0) {
			constraint->enableSpring(3, true);
			constraint->setStiffness(3, pmxJoint.springRotate.x);
		}
		if (pmxJoint.springRotate.y != 0) {
			constraint->enableSpring(4, true);
			constraint->setStiffness(4, pmxJoint.springRotate.y);
		}
		if (pmxJoint.springRotate.z != 0) {
			constraint->enableSpring(5, true);
			constraint->setStiffness(5, pmxJoint.springRotate.z);
		}

		this->constraint = std::move(constraint);

	}


	//物理エンジンに、モデルの持っている剛体・ジョイントをくっつける
	void PoseSolver::AttachPhysics()
	{
		if (physics == nullptr)
			return;

		body.resize(pmx->bodies.size());
		for (int i = 0; i < pmx->bodies.size(); i++) {
			body[i] = new RigidBody(pmx, i, this);
			physics->AddRigidBody(body[i]);
		}

		joint.resize(pmx->joints.size());
		for (int i = 0; i < pmx->joints.size(); i++) {
			auto& j = pmx->joints[i];
			joint[i] = new Joint(pmx, i, body[j.bodyA], body[j.bodyB]);
			physics->AddJoint(joint[i]);
		}

		physics->AddNotifee(this);
	}

	//物理エンジンから切断
	void PoseSolver::DetachPhysics()
	{
		if (physics == nullptr)
			return;

		//BulletPhysicsのconstraintはrigidbodyへのポインタを持っているので解放する時はcostraint(joint)から解放しないとダメとのこと
		for (auto&& jt : joint) {
			physics->RemoveJoint(jt);
			delete jt;
		}

		for (auto&& bd : body) {
			physics->RemoveRigidBody(bd);
			delete bd;
		}

		physics->DeleteNotifee(this);
	}

	//物理エンジン更新開始
	void PoseSolver::OnUpdatePhysicsBegin()
	{
		for (auto bd : body)
			bd->SetActivation(true);
	}

	//物理エンジン更新終了、ボーンに反映
	void PoseSolver::OnUpdatePhysicsEnd()
	{
		for (auto bd : body)
			bd->ReflectGlobalTransform();

		root->Propagate(root);
		FinishMatrices();
	}

	//物理エンジンリセット開始
	void PoseSolver::OnResetPhysicsBegin()
	{
		for (auto bd : body) {
			bd->SetActivation(false);
			bd->ResetTransform();
		}
	}

	//物理エンジンリセット終了、ボーンに反映
	//ResetはSolve直後やAスタンス時に呼んで剛体の位置をボーンアニメの状態に合わせる＆剛体の運動量を0にするのが目的なので
	//剛体の位置をボーンに反映させるための処理は必要ないはず
	void PoseSolver::OnResetPhysicsEnd()
	{
		//for (auto bd : body)
			//bd->ReflectGlobalTransform();

		//for (int i = 0; i < body.size(); i++)
		//	body[i]->CalcLocalTransform();

		for (auto bd : body)
			bd->Reset();

		//root->Propagate(root);
		//FinishMatrices();
	}

	//Aスタンスから最後にSolveした時のキーフレームの状態にジワジワと近づける事で剛体が貫通状態からスタートするのをある程度防ぐ
	//ポーズによっては却って物理がおかしくなるので、framesをなるべく増やすのが良いと思われる
	void PoseSolver::OnPrewarmPhysics(int frames, int totalFrames)
	{
		//framesが0なら初期化＝終了状態を保存
		if (frames == 0) {
			m_goalT.resize(bones.size());
			m_goalQ.resize(bones.size());

			for (int i = 0; i < bones.size(); i++) {
				m_goalT[i] = bones[i]->localTranslation;
				m_goalQ[i] = bones[i]->localRotation;
			}
		}

		float t = frames / (float(totalFrames) - 1);

		for (int j = 0; j < bones.size(); j++) {
			Vector3 p = m_goalT[j] * t;
			Quaternion q = Quaternion::Slerp(Quaternion::Identity, m_goalQ[j], t);
			bones[j]->localTranslation = p;
			bones[j]->localRotation = q;
			bones[j]->toParent = oRtMatrix(bones[j]->origin, q, p);
			bones[j]->transformResolved = false;
		}
	}

	/******************************************************************************************/
	// CameraSolver ユーティリティ
	/******************************************************************************************/
	// 球面線形補間を計算する関数（結局カメラでは球面線形補間を使用していない.この関数自体あっているかもわからない.）
	float slerp(float a, float b, float t) {
		// この関数は, 回転にかかわるものだけを対象とする.
		float normalized = (b - a) / PI;	// 単位ベクトルに変換
		if (abs(normalized) > 1.0) {
			// 180[deg]を超えるような回転は, 線形補完とする
			return lerp(a, b, t);
		}

		// 球面線形補間
		float angle = std::acos(normalized);
		if (abs(sin(angle)) < 0.001) {
			// sin(angle)が0近傍であれば, 線形補完とする
			return lerp(a, b, t);
		}
		else {
			// それ以外は球面線形補間を行う
			float invSinAngle = 1.0f / std::sin(angle);
			float weightA = std::sin((1.0f - t) * angle) * invSinAngle;
			float weightB = std::sin(t * angle) * invSinAngle;

			return weightA * a + weightB * b;
		}
	}
	
	// 3次のベジェ曲線を計算する関数(参考にさせていただきました⇒edvakf氏 https://edvakf.hatenadiary.org/entry/20111016/1318716097)
	// ax, ay: 制御点1の座標
	// bx, by: 制御点2の座標
	// y1	 : 補完元1のy座標
	// y2	 : 補完元2のy座標
	// x     : 補完したいx座標
	float interpolateBezier(float ax, float ay, float bx, float by,
							float y1, float y2, float x, bool b_slerp = false)
	{
		float t = 0.5;		// 真ん中に設定
		float s = 1.0 - t;
		for (int i = 0; i < 32; i++)
		{
			// 二分法により、tの数値解を求める
			float ft = (3 * s * s * t * ax) + (3 * s * t * t * bx) + (t * t * t) - x;
			if (abs(ft) < 0.000001 )
				break;
			if (ft > 0)
				t -= (float)1.0 / ((LONGLONG)4 << i);
			else	// ft < 0
				t += (float)1.0 / ((LONGLONG)4 << i);
			s = 1 - t;
		}

		// 3次ベジェ曲線より, tに対するyゲイン(0～1)を求める.
		float y_gain = (3 * s * s * t * ay) + (3 * s * t * t * by) + (t * t * t);
		// 補完元y座標とyゲインより, 補完したいx座標に対するy座標値を求める.
		float y_out;
		if (b_slerp)
			y_out = slerp(y1, y2, y_gain);		// 球面線形補間
		else
			y_out = (y2 - y1) * y_gain + y1;	// 線形補完

		return y_out;
	}

	/******************************************************************************************/
	// CameraSolver
	/******************************************************************************************/
	//VMDのモーションから現在のフレームのローカル変形(位置・回転)を得る。
	void CameraSolver::GetKeyFrame(float time, int iCam, VMDCamera* vmdcamera)
	{
		int ic = iCam;

		if (ic != 0) {
			// 現状はカメラ0番のみ対応.0版以外が指定された場合にはオール0のキーフレームを返す.
			*vmdcamera = {0};
			return;
		}

		float f = max(0, min(time, vmdcams[ic]->lastFrame));
		int targetFrame = floorf(f);
		vmdcamera->iFrame = targetFrame;
		
		VMDCamera c0, c1;	//前のフレームがc0, 後ろのフレームがc1

		int lo = 0, hi = cameraKeys[ic].size() - 1;
		while (lo <= hi) {
			int mid = (hi + lo) >> 1;
			//二分探索でtargetFrameより後ろにある最初のキーフレームを探す
			if (cameraKeys[ic][mid].iFrame <= targetFrame)
				lo = mid + 1;
			else
				hi = mid - 1;
		}
		c1 = cameraKeys[ic][lo];
		c0 = cameraKeys[ic][lo - 1];

		//必要に応じて補完する(カメラ位置, カメラ角度, 距離, 視野角)
		float t = (float)(f - c0.iFrame) / (float)(c1.iFrame - c0.iFrame);

		if (vec3(c0.position) == vec3(c1.position)) {
			vmdcamera->position = c0.position;
		} else {
			// 補間曲線で補間（ボーンと同じ補間曲線を使ってもよい）
			vmdcamera->position.x = interpolateBezier(c1.bezier.Xax, c1.bezier.Xay,
				c1.bezier.Xbx, c1.bezier.Xby, c0.position.x, c1.position.x, t);
			vmdcamera->position.y = interpolateBezier(c1.bezier.Yax, c1.bezier.Yay,
				c1.bezier.Ybx, c1.bezier.Yby, c0.position.y, c1.position.y, t);
			vmdcamera->position.z = interpolateBezier(c1.bezier.Zax, c1.bezier.Zay,
				c1.bezier.Zbx, c1.bezier.Zby, c0.position.z, c1.position.z, t);

			// ボーンと同じ補間曲線
			//float tx = BezierT(t, c1.bezier.Xax, c1.bezier.Xay, c1.bezier.Xbx, c1.bezier.Xby);
			//float ty = BezierT(t, c1.bezier.Yax, c1.bezier.Yay, c1.bezier.Ybx, c1.bezier.Yby);
			//float tz = BezierT(t, c1.bezier.Zax, c1.bezier.Zay, c1.bezier.Zbx, c1.bezier.Zby);
			//vmdcamera->position = Lerp(c0.position, c1.position, vec3(tx, ty, tz));
		}

		if (vec3(c0.rotation) == vec3(c1.rotation)) {
			vmdcamera->rotation = c0.rotation;
		} else {
			// 補間曲線で補間（ボーンと同じ補間曲線を使ってもよい）
			vmdcamera->rotation.x = interpolateBezier(c1.bezier.Rax, c1.bezier.Ray,
				c1.bezier.Rbx, c1.bezier.Rby, c0.rotation.x, c1.rotation.x, t, false);
			vmdcamera->rotation.y = interpolateBezier(c1.bezier.Rax, c1.bezier.Ray,
				c1.bezier.Rbx, c1.bezier.Rby, c0.rotation.y, c1.rotation.y, t, false);
			vmdcamera->rotation.z = interpolateBezier(c1.bezier.Rax, c1.bezier.Ray,
				c1.bezier.Rbx, c1.bezier.Rby, c0.rotation.z, c1.rotation.z, t, false);

			// ボーンと同じ補間曲線
			//float tr = BezierT(t, c1.bezier.Rax, c1.bezier.Ray, c1.bezier.Rbx, c1.bezier.Rby);
			//vmdcamera->rotation = Lerp(c0.rotation, c1.rotation, vec3(tr, tr, tr));
		}

		if(c0.distance == c1.distance)
		{
			vmdcamera->distance = c0.distance;
		} else {
			// 補間曲線で補間（ボーンと同じ補間曲線を使ってもよい）
			vmdcamera->distance = interpolateBezier(c1.bezier.Lax, c1.bezier.Lay,
				c1.bezier.Lbx, c1.bezier.Lby, c0.distance, c1.distance, t);

			// ボーンと同じ補間曲線
			//XMFLOAT3 c0_distance = XMFLOAT3(c0.distance, c0.distance, c0.distance);
			//XMFLOAT3 c1_distance = XMFLOAT3(c1.distance, c1.distance, c1.distance);
			//float td = BezierT(t, c1.bezier.Lax, c1.bezier.Lay, c1.bezier.Lbx, c1.bezier.Lby);
			//XMFLOAT3 cm_distance = Lerp(c0_distance, c1_distance, vec3(td, td, td));
			//vmdcamera->distance = cm_distance.x;
		}

		if(c0.viewAngle == c1.viewAngle)
		{
			vmdcamera->viewAngle = c0.viewAngle;
		} else {
			// 補間曲線で補間（ボーンと同じ補間曲線を使ってもよい）
			vmdcamera->viewAngle = interpolateBezier(c1.bezier.Vax, c1.bezier.Vay,
				c1.bezier.Vbx, c1.bezier.Vby, c0.viewAngle, c1.viewAngle, t);

			// ボーンと同じ補間曲線
			//XMFLOAT3 c0_viewAngle = XMFLOAT3(c0.viewAngle, c0.viewAngle, c0.viewAngle);
			//XMFLOAT3 c1_viewAngle = XMFLOAT3(c1.viewAngle, c1.viewAngle, c1.viewAngle);
			//float tv = BezierT(t, c1.bezier.Vax, c1.bezier.Vay, c1.bezier.Vbx, c1.bezier.Vby);
			//XMFLOAT3 cm_viewAngle = Lerp(c0_viewAngle, c1_viewAngle, vec3(tv, tv, tv));
			//vmdcamera->viewAngle = cm_viewAngle.x;
		}

		//if (iBone == 0) LOG(L"frame:%.2d t:%f B(t):%f", iFrame, t, tx);
		//LOG(L"#%d %s : %s %s", iBone, pmx->m_bs[iBone].name, vec2wstr(*translation).c_str(), quat2wstr(*rotation).c_str());
	}

	//時刻timeにおけるカメラのパラメータを反映する。開始フレームは0
	void CameraSolver::Solve(float time, int iCam, VMDCamera* vmdcamera)
	{
		int ic = iCam;

		if (ic != 0) {
			// 現状はカメラ0番のみ対応.0版以外が指定された場合にはオール0のキーフレームを返す.
			*vmdcamera = {0};
			return;
		}

		int ccount = vmdcams[ic]->cameraKeys.size();
		/*
		LARGE_INTEGER freqT, beginT;
		QueryPerformanceFrequency(&freqT);
		QueryPerformanceCounter(&beginT);
		*/
		
		//一つもキーが無い場合は、無回転・無移動のパラメータをいれとく
		if (ccount == 0) {
			VMDCamera c = {};
			c.bezier.Rbx = c.bezier.Rby = c.bezier.Xbx = c.bezier.Xby = c.bezier.Ybx = c.bezier.Yby = c.bezier.Zbx = c.bezier.Zby = 1.0f;
			c.bezier.Lbx = c.bezier.Lby = c.bezier.Vbx = c.bezier.Vby = 1.0f;
			c.position.y = 10.0f;
			c.distance = 45.0f;
			c.viewAngle = 30.0f;
			*vmdcamera = c;
		}

		//キーフレーム読んでカメラのパラメータを取得
		GetKeyFrame(time, ic, vmdcamera);

		//ベンチマーク
		/*
		LARGE_INTEGER endT;
		QueryPerformanceCounter(&endT);
		double elapsed_time = (double)(endT.QuadPart - beginT.QuadPart) * 1E+6 / freqT.QuadPart;
		::LOG(L"*** %.2lf μs for solve pose overall***", elapsed_time);
		*/
	}

	void CameraSolver::Add(int iCam, const VMDCam* _vmdcam)
	{
		// TODO. 現状はカメラ0番のみ対応.
	}

	CameraSolver::CameraSolver(const VMDCam* _vmdcam)
	{
		//まずカメラ数だけVMDCamとVMDCameraの配列を作る(現状は1つだけ)
		int ic = 0;
		vmdcams = std::vector<const VMDCam*>();
		vmdcams.push_back(_vmdcam);
		cameraKeys = vector<std::vector<VMDCamera>>(ic + 1);

		//VMDに含まれるカメラキーフレームの追加
		{
			//カメラキーフレームの追加
			for (auto& c : vmdcams[ic]->cameraKeys) {
				cameraKeys[ic].push_back(c);
			}
			for (int i = 0; i < (ic + 1); i++) {
				//一つもキーが無い場合は、無回転・無移動のパラメータをいれとく
				if (cameraKeys[i].size() == 0) {
					VMDCamera c = {};
					c.bezier.Rbx = c.bezier.Rby = c.bezier.Xbx = c.bezier.Xby = c.bezier.Ybx = c.bezier.Yby = c.bezier.Zbx = c.bezier.Zby = 1.0f;
					c.bezier.Lbx = c.bezier.Lby = c.bezier.Vbx = c.bezier.Vby = 1.0f;
					c.position.y = 10.0f;
					c.distance = 45.0f;
					c.viewAngle = 30.0f;
					cameraKeys[i].push_back(c);
				}
				//最後のフレーム+1の所に最後のフレームの時と同じ格好のモーションデータを入れておく(補完用)
				VMDCamera c = cameraKeys[i].back();
				c.iFrame = vmdcams[ic]->lastFrame + 1;
				cameraKeys[i].push_back(c);
				//同様に、必要なら最初のフレームと同じ格好で0フレームに入れておく
				VMDCamera c0 = cameraKeys[i][0];
				if (c0.iFrame != 0) {
					c0.iFrame = 0;
					cameraKeys[i].insert(cameraKeys[i].begin(), c0);
				}
			}
		}
	}
	
	CameraSolver::~CameraSolver()
	{
		
	}

} //namespace

