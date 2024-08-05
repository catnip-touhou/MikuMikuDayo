#ifndef YRZ_HLSLI
#define YRZ_HLSLI

//DXRよろずヘルパー HLSL出張版

//参考文献

//Bruce Walter氏, Stephen R. Marschner氏, Hongsong Li氏, Kenneth E. Torrance氏
//"Microfacet Models for Refraction through Rough Surfaces"
//Eurographics Symposium on Rendering 2007

//Eric Heitz氏, Eugene d’Eon氏
//"Importance Sampling Microfacet-Based BSDFs using the Distribution of Visible Normals"
//Eurographics Symposium on Rendering 2014

//Eric Heitz氏
//"Sampling the GGX Distribution of Visible Normals"
//Journal of Computer Graphics Techniques Vol. 7, No. 4, 2018

//Bjorn Ottosson氏 "A perceptual color space for image processing"
//https://bottosson.github.io/posts/oklab/

//Tiny Texel氏 "ACEScg"
//https://www.shadertoy.com/view/WltSRB

//Ocean Optic Web Book(CC-BY 2.0)
//https://www.oceanopticsbook.info/view/photometry-and-visibility/from-xyz-to-rgb

//Jonathan Dupuy氏, Anis Benyoub氏 "Sampling Visible GGX Normals with Spherical Caps"
//https://arxiv.org/abs/2306.05044

//Mark Jarzynski氏, Marc Olano氏 "Hash Functions for GPU Rendering"
//https://jcgt.org/published/0009/03/02/paper.pdf

//Rodolphe Vaillant氏 "デュアルクォータニオン スキニング チュートリアル・解説 C++"
//http://rodolphe-vaillant.fr/entry/130/dual-quaternions-skinning-tutorial-and-cpp-codes-japanese

//Ladislav Kavan氏 "Skinning with Dual Quaternions"
//https://users.cs.utah.edu/~ladislav/dq/index.html

//takao氏 "NYAHOON games デュアルクォータニオン徹底解説"
//https://nyahoon.com/blog/1836

//Peter-Pike Sloan氏 "Stupid Spherical Harmonics (SH) Tricks"
//https://www.ppsloan.org/publications/

//Color and Vision Research Labs様 "Older CIE Standards"
//http://www.cvrl.org/cie.htm

//pbrt
//https://github.com/mmp/pbrt-v3


/****************************************************************************
  定数
****************************************************************************/

static float PI = acos(-1);

/****************************************************************************
  マクロ
****************************************************************************/


/****************************************************************************
  算術ヘルパー
****************************************************************************/

//二乗
//template<typename T> T sqr(T a) {return a*a;}
float sqr(float a) { return a*a;}
float2 sqr(float2 a) { return a*a;}
float3 sqr(float3 a) { return a*a;}
float4 sqr(float4 a) { return a*a;}

//長さの二乗
float SqrLen(float a) { return dot(a,a); }
float SqrLen(float2 a) { return dot(a,a); }
float SqrLen(float3 a) { return dot(a,a); }
float SqrLen(float4 a) { return dot(a,a); }

//3要素の中の最大値
float ReduceMax(float3 a)
{
	return max(max(a.x,a.y),a.z);
}

//3要素の中の最小値
float ReduceMin(float3 a)
{
	return min(min(a.x,a.y),a.z);
}

//{cos(t),sin(t)}を返す
float2 CosSin(float t)
{
	float2 a;
	sincos(t, a.y,a.x);
	return a;
}

//長さと向き両方
float3 LenDir(float3 v, out float len)
{
	float d2 = dot(v,v);
	if (d2) {
		len = sqrt(d2);
		return v/len;
	} else {
		len = 0;
		return 0;
	}
}

//指数補間
float Exerp(float a,float b,float r) {return a*pow(b/a,r);}
float2 Exerp(float2 a,float2 b,float2 r) {return a*pow(b/a,r);}
float3 Exerp(float3 a,float3 b,float3 r) {return a*pow(b/a,r);}
float4 Exerp(float4 a,float4 b,float4 r) {return a*pow(b/a,r);}

//逆線形補間 val = (1-k)mi + kma としてkを返す
//mi == ma の時はdefを返す
float InvLerp(float mi, float ma, float val, float def = 0.5)
{
    return (ma==mi) ? def : (val-mi)/(ma-mi);
}

/****************************************************************************
  ハッシュ・ランダム・ノイズ
****************************************************************************/

//Mark Jarzynski氏, Marc Olano氏 "Hash Functions for GPU Rendering"
//https://jcgt.org/published/0009/03/02/paper.pdf より
uint4 PCG4d(uint4 v)
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
	v ^= v >> 16u;
	v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
	return v;
}

uint3 PCG3d(uint3 v)
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
	return v;
}

//0～1の範囲にしてくれるヤツ
float4 Hash4(uint4 x)
{
	uint4 p = PCG4d(x) & 0x007FFFFF;	//下位23ビットだけ採用
	return p / float(0x00800000);
}

//0～1の範囲にしてくれるヤツ
float3 Hash3(uint3 x)
{
	uint3 p = PCG3d(x) & 0x007FFFFF;	//下位23ビットだけ採用
	return p / float(0x00800000);
}

//Golden ratio hashing
static float RCP_G = 0.6180339887498948; //x^2=x+1の解(黄金数)、の逆数
static float RCP_G2 = 0.7548776662466927;//x^3=x+1の解、の逆数
static float RCP_G3 = 0.819172513396164; //x^4=x+1の解、の逆数
static float RCP_G4 = 0.85667780347811;
static float2 RCP_G2_2 = float2(RCP_G2,RCP_G2*RCP_G2);
static float3 RCP_G3_3 = float3(RCP_G3,RCP_G3*RCP_G3,RCP_G3*RCP_G3*RCP_G3);
static float4 RCP_G4_4 = float4(RCP_G4,RCP_G4*RCP_G4,RCP_G4*RCP_G4*RCP_G4, RCP_G4*RCP_G4*RCP_G4*RCP_G4);

//1 out
float GoldenHash11(float p) { return frac(p * RCP_G); }
float GoldenHash12(float2 p) { return frac(dot(p, RCP_G2_2)); }
float GoldenHash13(float3 p) { return frac(dot(p, RCP_G3_3)); }

//2 out
float2 GoldenHash21(float p) { return frac(p * RCP_G2_2); }
float2 GoldenHash22(float2 p) { return frac(p * RCP_G2_2); }
float2 GoldenHash23(float3 p) { p *= RCP_G3_3; return frac(float2(p.xy+p.yz)); }

//3 out
float3 GoldenHash31(float p) { return frac(p * RCP_G3_3); }
float3 GoldenHash32(float2 p) { float2 q = RCP_G2_2; return frac(float3(p*q, dot(p.xy,q.yx))); }     //未検証
float3 GoldenHash33(float3 p) { p *= RCP_G3_3; return frac(float3(p.xyx+p.yzz)); }  //未検証

//1次元の数p と 2次元の数q から1次元の数を作る (q.x,q.y間でコヒーレンシ有り、pとは無しの時用)
float GoldenHash1_12(float p, float2 q) { return frac(dot(float3(p,q) , float3(RCP_G, RCP_G2_2))); }

//1次元の数p と 2次元の数q から2次元の数を作る
float2 GoldenHash2_12(float p, float2 q) { return GoldenHash23(float3(p,q)); }

//1次元の数p と 2次元の数q から3次元の数を作る
float3 GoldenHash3_12(float p, float2 q) { return frac(float3(p,q) * float3(RCP_G, RCP_G2_2)); }     //未検証


//ランダム関数
//frameはフレーム番号、xiは適当なオフセット値
//呼ぶたびにオフセット値が+1されるのでrandom4を呼ぶたびに違う値が返る
float4 Random4(uint frame, inout uint xi)
{
	float4 ret = Hash4(uint4(DispatchRaysIndex().xy, frame, xi));
	xi++;
	return ret;
}


//色つきバリューノイズ
float3 ValueNoise33(float3 p)
{
    float3 i = floor(p);
    float3 f = smoothstep(0,1,frac(p));
    float2 d = {0,1};

    return lerp(
        lerp(
            lerp ( Hash3(i + d.xxx), Hash3(i + d.yxx), f.x ),
            lerp ( Hash3(i + d.xyx), Hash3(i + d.yyx), f.x ), f.y),
        lerp(
            lerp ( Hash3(i + d.xxy), Hash3(i + d.yxy), f.x ),
            lerp ( Hash3(i + d.xyy), Hash3(i + d.yyy), f.x ), f.y),
        f.z);
}


/****************************************************************************
  線形代数
****************************************************************************/

static float4x4 Identity44 = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static float3x3 Identity33 = {1,0,0, 0,1,0, 0,0,1};
static float2x2 Identity22 = {1,0, 0,1};


//接空間生成、法線nから互いに直交する基底ベクトルを作る
float3x3 ComputeTBN(float3 n)
{
	//float3 Z = abs(n.z) < 0.999999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 Z = abs(n.y) < 0.999999 ? float3(0, 1, 0) : float3(1, 0, 0);
	float3 X = cross(n, Z);
	float3 Y = normalize(cross(n, X));
	X = cross(n, Y);
	return float3x3(X,Y,n);
}


//UV座標の変化から法線マップ用の接空間を得る
//Nは法線
//px,uvxは三角ポリゴンの情報。具体的には以下の通り
//p0,uv0 : 重心座標(0,0)の時の頂点の位置,uv座標
//p1,uv1 : 重心座標(1,0)の時の頂点の位置,uv座標
//p2,uv2 : 重心座標(0,1)の時の頂点の位置,uv座標
//out valid : ポリゴンにUVが設定されていないと思われる場合はfalse
float3x3 ComputeTBN_UV(float3 N, float3 p0, float3 p1, float3 p2, float2 uv0, float2 uv1, float2 uv2, out bool valid)
{
	valid = false;
    float3 dp1 = p1-p0;
    float3 dp2 = p2-p0;
    float2 duv1 = uv1-uv0;
    float2 duv2 = uv2-uv0;

	//パッチ上でuvの変化が全くない場合は接空間の生成が出来ないのでvalid=falseにし、Nのみから生成した接空間を返す
	if (min(dot(duv1,duv1),dot(duv2,duv2)) < 1E-12)
		return ComputeTBN(N);

    float3x3 M = float3x3(dp1, dp2, cross(dp1, dp2));
    float2x3 inverseM = float2x3(cross(M[1], M[2]), cross(M[2], M[0]));
    float3 Tangent = mul(float2(duv1.x, duv2.x), inverseM);
    float3 Binormal = mul(float2(duv1.y, duv2.y), inverseM);

	//du方向とdv方向が一致してしまっている
	if (all(Tangent==0) || all(Binormal==0))
		return ComputeTBN(N);

    valid = true;
    return float3x3(normalize(Tangent), normalize(Binormal), N);
}

//Nに併せてT,Bを直交化し、左手系(親フレームと同じ手)に直す
//シェーディング用のTBNを法線マップ用のTBNから作りたい時にどうぞ
float3x3 Orthonormalize(float3x3 TBN)
{
	float3 Tangent = TBN[0];
	float3 Binormal = TBN[1];
	float3 n = TBN[2];

    Tangent = normalize(cross(Binormal, n));
    Binormal = cross(n, Tangent);

    //逆手を順手に直す
    if (dot(n, cross(Tangent,Binormal)) < 0)
        Tangent = -Tangent;

    return float3x3(Tangent,Binormal,n);
}

//2Dの回転と各軸周り
float2 Rot2(float2 v, float t)
{
    float s,c;
    sincos(t,s,c);
    return mul(v,float2x2(c,-s,s,c));
}

float3 RotX(float3 v, float t)
{
    float2 r = Rot2(v.yz,t);
    return float3(v.x, r.xy);
}

float3 RotY(float3 v, float t)
{
    float2 r = Rot2(v.xz,t);
    return float3(r.x, v.y, r.y);
}

float3 RotZ(float3 v, float t)
{
    float2 r = Rot2(v.xy,t);
    return float3(r.xy, v.z);
}

//軸a周りの回転(aは正規化済み)
float3 RotAxis(float3 v, float3 a, float t)
{
    float s,c;
    sincos(t, s,c);
    return v * c + (1-c)*dot(a,v)*a + cross(a,v)*s;
}

//球面線形補間。a,bとも正規化済みならnormalized=trueにすると軽くなる
float3 Slerp(float3 a, float3 b, float t, uniform bool normalized = true)
{
    float la,lb,s;
    float3 na,nb;
    [branch]
    if (normalized) {
        s = 1;  na = a; nb = b;
    } else {
        la = length(a); lb = length(b);
        if (la==0)  return t*b;
        if (lb==0)  return (1-t)*a;
        na = a/la;  nb = b/lb;
        s = lerp(la,lb,t);
    }
    float AoB = dot(na,nb);

    float3 r;
    //数値誤差対策。なす角が小さい場合はlerpで近似(sinθ≒θ)
    if (AoB > 0.9999) {
        r = lerp(a,b,t);
    } else {
        float theta = acos(AoB);
        float sinT = sqrt(1-AoB*AoB);
        r = s * (sin((1-t)*theta)*na + sin(t*theta)*nb) / sinT;
    }
    return r;
}


//mの回転分のみからクォータニオンを作る, 参考文献のRodolphe Vaillant氏のコードを参考にしました
//拡大・剪断要素の入った行列は多分扱えないので注意
float4 QuatFromMatrix(float4x4 m)
{
    // Compute trace of matrix
    float T = 1 + m._11 + m._22 + m._33;

    float S, X, Y, Z, W;

    // to avoid large distortions!
    if ( T > 0.00000001f ) {
        S = sqrt(T) * 2.f;
        X = ( m._23 - m._32 ) / S;
        Y = ( m._31 - m._13 ) / S;
        Z = ( m._12 - m._21 ) / S;
        W = 0.25f * S;
    } else {
        if ( m._11 > m._22 && m._11 > m._33 ) {
            // Column 0 :
            S  = sqrt( 1.0f + m._11 - m._22 - m._33 ) * 2.f;
            X = 0.25f * S;
            Y = (m._12 + m._21 ) / S;
            Z = (m._31 + m._13 ) / S;
            W = (m._23 - m._32 ) / S;
        } else if ( m._22 > m._33 ) {
            // Column 1 :
            S  = sqrt( 1.0f + m._22 - m._11 - m._33 ) * 2.f;
            X = (m._12 + m._21 ) / S;
            Y = 0.25f * S;
            Z = (m._23 + m._32 ) / S;
            W = (m._31 - m._13 ) / S;
        }
        else
        {   // Column 2 :
            S  = sqrt( 1.0f + m._33 - m._11 - m._22 ) * 2.f;
            X = (m._31 + m._13 ) / S;
            Y = (m._23 + m._32 ) / S;
            Z = 0.25f * S;
            W = (m._12 - m._21 ) / S;
        }
    }
    return float4(X,Y,Z,W);
}

//正規化済みクォータニオンから回転行列を作る
float4x4 MatrixFromQuat(float4 q)
{
    float xx = q.x*q.x, xy = q.x*q.y,   xz = q.x*q.z,   xw = q.x*q.w;
    float yy = q.y*q.y, yz = q.y*q.z,   yw = q.y*q.w;
    float zz = q.z*q.z, zw = q.z*q.w;
    float ww = q.w*q.w;

    return float4x4(
        ww + xx - yy - zz, 2*(xy + zw), 2*(xz - yw), 0,
        2*(xy - zw), ww-xx+yy-zz, 2.f * (yz + xw), 0,
        2*(xz + yw), 2*(yz - xw), ww-xx-yy+zz, 0,
        0,0,0,1
    );
}

//クォータニオン同士の積
float4 MulQ(float4 q, float4 p)
{
    return float4( cross(q.xyz,p.xyz) + q.w*p.xyz + p.w*q.xyz, dot(q.xyz,p.xyz)-q.w*p.w );
}

//共役クォータニオン
float4 ConjQ(float4 q)
{
    return float4(-q.xyz,q.w);
}

//軸ベクトルと回転角度からクォータニオンを作る
float4 QuatFromAxisAngle(float3 axis, float rad)
{
    float s,c;
    sincos(rad/2, s,c);
    return float4(s*axis, c);
}

float3 Transform(float3 v, float4x4 m)
{
    return mul(float4(v,1), m).xyz;
}

//ベクトルv をクォータニオンqで回転させる
float3 Rotate(float3 v, float4 q)
{
    return MulQ(MulQ(q,float4(v,0)),ConjQ(q)).xyz;
}

float3 Rotate(float3 v, float4x4 m)
{
    return mul(v, (float3x3)m);
}


//クォータニオンのslerp
//注意
//1.正規化済みクォータニオンのみ対応
//2.クォータニオンのslerpは本来なら遠回りの経路を選ぶことが有るが、このコードでは常に近回りの経路を選ぶ
//3.q,pのなす角が小さい場合はlerpで近似する(数値誤差対策のため)
float4 Slerp(float4 q, float4 p, float t)
{
    float cosT = clamp(dot(q,p),-1,1); //cos θ/2, clampは念のための誤差対策

    //θが180°以上の時は遠回りに回転しようとするので近回りを選択する
    if (cosT < 0) {
        cosT = -cosT;
        p = -p;
    }

    //θが小さい時はsinθ≒θで近似(誤差対策)
    float4 r;
    if (cosT > 0.9999) {
        r = lerp(q,p,t);
    } else {
        float T = acos(cosT);
        r = sin((1-t)*T) * q + sin(t*T) * p;    //正規化するので /sinT は省略
    }

    return normalize(r);
}



//デュアルクォータニオン
struct DualQ {
    float4 q;   //回転分
    float4 t;   //平行移動分
    /*
    DualQ operator + (DualQ d) { DualQ R = {q+d.q, t+d.t}; return R; }
    DualQ operator - (DualQ d) { DualQ R = {q-d.q, t-d.t}; return R; }
    DualQ operator * (DualQ d) { DualQ R = {MulQ(q,d.q), MulQ(t,d.q) + MulQ(q,d.t)}; return R; }
    DualQ operator * (float s) { DualQ R = {q*s,t*s}; return R; }
    DualQ operator / (float s) { DualQ R = {q/s,t/s}; return R; }
    */
};


//2つのクォータニオンをそのまま結合してデュアルクォータニオンを作る
DualQ DualQFromQQ(float4 q, float4 t)
{
    DualQ R = {q,t};
    return R;
}

//回転Qと平行移動分tからデュアルクォータニオンを作る
//参考文献のRodolphe Vaillant氏のコードを参考にしました
DualQ DualQFromQT(float4 q, float3 t)
{
    float w = -0.5f*( t.x * q.x + t.y * q.y + t.z * q.z);
    float i =  0.5f*( t.x * q.w + t.y * q.z - t.z * q.y);
    float j =  0.5f*(-t.x * q.z + t.y * q.w + t.z * q.x);
    float k =  0.5f*( t.x * q.y - t.y * q.x + t.z * q.w);

    return DualQFromQQ(q, float4(i, j, k, w));
}

//行列から作る
DualQ DualQFromMatrix(float4x4 m)
{
    float4 q = QuatFromMatrix(m);
    float3 t = m._41_42_43;
    return DualQFromQT(q,t);
}

//参考文献のRodolphe Vaillant氏のコードを参考にしました
float4x4 MatrixFromDualQ(DualQ d)
{
    float3 t;
    float norm = length(d.q);

    // Rotation matrix from non-dual quaternion part
    float4x4 m = MatrixFromQuat(d.q / norm);

    // translation vector from dual quaternion part:
    t.x = 2.f*(-d.t.w*d.q.x + d.t.x*d.q.w - d.t.y*d.q.z + d.t.z*d.q.y);
    t.y = 2.f*(-d.t.w*d.q.y + d.t.x*d.q.z + d.t.y*d.q.w - d.t.z*d.q.x);
    t.z = 2.f*(-d.t.w*d.q.z - d.t.x*d.q.y + d.t.y*d.q.x + d.t.z*d.q.w);
    m._41_42_43 = t/norm;

    return m;
}

DualQ AddDualQ(DualQ A, DualQ B)
{
    return DualQFromQQ(A.q + B.q, A.t + B.t);
}

DualQ ScaleDualQ(DualQ A, float t)
{
    return DualQFromQQ(A.q*t, A.t*t);
}

DualQ MulDualQ(DualQ A, DualQ B)
{
    DualQ R;
    R.q = MulQ(A.q,B.q);
    R.t = MulQ(A.t,B.q) + MulQ(A.q,B.t);
    return R;
}

DualQ IdentityDualQ()
{
    return DualQFromQQ( float4(0,0,0,1), float4(0,0,0,0));
}

DualQ NormalizeDualQ(DualQ d)
{
    float norm = length(d.q);
    return DualQFromQQ(d.q/norm, d.t/norm);
}

DualQ LerpDualQ(DualQ a, DualQ b, float u)
{
    return AddDualQ(ScaleDualQ(a,1-u), ScaleDualQ(b,u));
}

//↓の補間関数のための係数算出関数
float DualQ_FOu(float O, float sinO,  float u)
{
    return cos(u*O) * (u*sinO-sin(u*O))/(sinO*sinO);
}

//DualQ同士の補間(近似の少ない版)
//注意：遠回り回避は入れてない
DualQ InterpolateDualQ(DualQ A, DualQ B, float u)
{
    if (u==0)
        return A;
    if (u==1)
        return B;

    //資料のq->t, 資料のr->q
    float cosO = clamp(dot(A.q, B.q),-1,1);

    if (cosO > 0.999)
        return LerpDualQ(A,B,u);  //AとBの角度が近い時は線形補間で近似

    float sinO = sqrt(1-cosO*cosO);

    float O = acos(cosO);

    float LsinO = -(dot(A.q,B.t)+dot(A.t,B.q));
    DualQ R,S;
    float sR = sin(u*O)/sinO;
    R.q = sR * B.q;
    R.t = sR * (B.t - DualQ_FOu(O, sinO, u)*LsinO*B.q );
    float sS = sin((1-u))*O/sinO;
    S.q = sS * A.q;
    S.t = sS * (A.t - DualQ_FOu(O, sinO, 1-u)*LsinO*A.q );
    return AddDualQ(R,S);
}

//左上が0、右下が1の座標系(xyと奥行z)からビュー座標系へ変換
//z要素はカメラのZ軸沿いの長さ(一般的な線形深度)
//fovは垂直画角をθとしてcot(θ/2)
//aspectは横解像度[pix]/縦解像度[pix]
float3 UVZtoView(float2 d, float z, float fov, float aspect)
{
	return float3( (d.xy*2-1) / float2(fov/aspect,-fov) * z, z);
}

float2 ViewToUV(float3 v, float fov, float aspect)
{
	return ((v.xy/v.z) * float2(fov/aspect,-fov))*.5+.5;
}



/****************************************************************************
  交差判定
****************************************************************************/
//原点中心の半径Rの球とpからrの向きに発射されるレイとの当たり判定
bool RaySphere(float3 p, float3 r, float R, out float t0, out float t1)
{
	float B = 2 * dot(r,p);
	float C = dot(p,p)-R*R;
	float det = B*B-4*C;
	if (det <= 0)
		return false;
	t1 = (-B + sqrt(det)) / 2;
	t0 = (-B - sqrt(det)) / 2;
	if (t1 < 0)
		return false;
	
	t0 = max(0,t0);
	return true;
}

//平面dot(N,X) + d = 0との交差判定
bool RayPlane(float3 ro, float3 rd, float3 N, float d, out float t)
{
    t = (-d - dot(N,ro)) / dot(N,rd);
    return t >= 0;
}


/****************************************************************************
  サンプリング
****************************************************************************/

// Sampling the visible hemisphere as half vectors
//"Sampling Visible GGX Normals with Spherical Caps"
//Jonathan Dupuy, Anis Benyoub 
//https://arxiv.org/abs/2306.05044 より
float3 SampleVndf_Hemisphere(float2 u, float3 wi)
{
	// sample a spherical cap in (-wi.z, 1]
	float z = mad(1 - u.y, 1 + wi.z, -wi.z);
	float st = sqrt(saturate(1 - z * z));	//sinθ
	float3 c = float3(st*CosSin(2 * PI * u.x), z);
	// compute halfway direction;
	float3 h = c + wi;
	// return without normalization (as this is done later)
	return h;
}

//wi ← -rd
float3 VNDF(float3 wi, float3x3 TBN, float2 Xi, float2 a)
{
    if (any(a==0)) {
        return TBN[2];
    }
	float3 wiTan = mul(TBN, wi);
	float3 wiStd = normalize(float3(wiTan.xy*a, wiTan.z));
	float3 wmStd = SampleVndf_Hemisphere(Xi, wiStd);
	float3 wm = float3(wmStd.xy*a, wmStd.z);
	return mul(normalize(wm), TBN);
}

//Heitz氏らのコードより
float3 _VNDF(float3 wi, float3x3 TBN, float2 rand, float2 alpha) {
    wi = mul(TBN,wi);
    float3 i_std = normalize(float3(wi.xy * alpha, wi.z));
    // Sample a spherical cap
    float phi = 2.0f * PI * rand.x;
    float a = saturate(min(alpha.x, alpha.y)); // Eq. 6
    float s = 1.0f + length(float2(wi.x, wi.y)); // Omit sgn for a<=1
    float a2 = a * a; float s2 = s * s;
    float k = (1.0f- a2) * s2 / (s2 + a2 * wi.z * wi.z); // Eq. 5
    float b = wi.z > 0 ? k * i_std.z : i_std.z;
    float z = mad(1.0f- rand.y, 1.0f + b,-b);
    float sinTheta = sqrt(saturate(1.0f- z * z));
    float3 o_std = {sinTheta * cos(phi), sinTheta * sin(phi), z};
    // Compute the microfacet normal m
    float3 m_std = i_std + o_std;
    float3 m = normalize(float3(m_std.xy * alpha, m_std.z));
    return mul(m,TBN);
    // Return the reflection vector o
    //return mul(2.0f * dot(wi, m) * m- wi, TBN);
 }

float3 SampleCosWeightedHemisphere(float3x3 TBN, float2 Xi, out float pdf)
{
	float r = sqrt(Xi.x);
	float z = sqrt(1 - Xi.x);
	pdf = z / PI;
	return mul(float3(r*CosSin(2 * PI * Xi.y), z), TBN);
}

//cos^powerに比例
//https://ameye.dev/notes/sampling-the-hemisphere/ より
float3 SamplePowerCosWeightedHemisphere(float power, float3x3 TBN, float2 Xi, out float pdf)
{
    float z = pow(Xi.x,1/(power+1));
    float r = sqrt(1-z*z);
    pdf = (power+1) * pow(z,power) / (2*PI);
	return mul(float3(r*CosSin(2 * PI * Xi.y), z), TBN);
}

//半球内で一様。トゥーンシェーディングなどのためのサンプリング
float3 SampleUniformHemisphere(float3x3 TBN, float2 Xi, out float pdf)
{
	float ct = Xi.x;
	float st = sqrt(1-ct*ct);
	pdf = 0.5 / PI;
	return mul(float3(st*CosSin(2 * PI * Xi.y), ct), TBN);
}

//全球で一様 pdfは1/(4*PI)で一定
float3 SampleUniformSphere(float2 Xi)
{
    float ct,st,sp,cp;
    sincos(Xi.x * 2 * PI, sp,cp);
    ct = frac(Xi.y*2);    //サイの目を2分割して使う
    ct = Xi.y > 0.5 ? ct : -ct;
    st = sqrt(1-ct*ct);
    return float3(st*cp, st*sp, ct);
}

//https://pbr-book.org/3ed-2018/Light_Transport_II_Volume_Rendering/Sampling_Volume_Scattering より
//Henyey-Greenstein関数に比例した確率密度に従ってNを天頂方向にした半球内のどこかをサンプリングする
//pdfが欲しい場合はこの値の帰り値と元のTBN[2]のdot取ってHenyeyGreensteinに入れれば良い
float3 SampleHG(float3x3 TBN, float2 Xi, float g)
{
    float ct = g ? 1/(2*g) * (1 + g*g - sqr( (1-g*g)/(1-g+2*g*Xi.x) )) : 1-2*Xi.x;
    float st = sqrt(1-ct*ct);
	float sp,cp;
	sincos(2*PI*Xi.y, sp,cp);
    float3 n = {st*cp, st*sp, ct};
    return mul(n,TBN);
}

//三角形pのうち1点
float3 SampleTirangle(float3 P[3], float2 Xi, out float area)
{
    //折り返し
    if (dot(Xi,1) >= 1)
        Xi = 1-Xi;

	float3 bary = {1-Xi.x-Xi.y, Xi.x, Xi.y};
    float3 V = 0;
    for (int i=0; i<3; i++) {
        V += P[i] * bary;
    }
	area = length(cross(P[0]-P[1], P[0]-P[2]))/2;
    return V;
}

//円板上を面積当たりの確率が等確率になるようサンプリング
//原点O 半径r TBNのNを軸とする
//pdfは面積当たりの確率密度で、サンプル位置によらず一定
float3 SampleConcentricDisk(float3 O, float R, float3x3 TBN, float2 Xi, out float pdf)
{
    pdf = 1/(PI*R*R);
    float2 tb = CosSin(Xi.y*2*PI);
    return O + R * sqrt(Xi.x) * (TBN[0]*tb[0] + TBN[1]*tb[1]);
}


//cos(θ) = 1～cosTMaxの範囲でのコーンのサンプリング pdfは[1/sr]
float3 SampleCone(float3x3 TBN, float2 Xi, float cosTMax, out float pdf)
{
    float ct = lerp(1,cosTMax,Xi.x);
    float st = sqrt(1-ct*ct);
    float cp,sp;
    sincos(Xi.y*2*PI, sp,cp);

    pdf = 1 / (2*PI*(1-cosTMax));
    return mul(float3(cp*st, sp*st, ct), TBN);
}

/****************************************************************************
  シェーディング
****************************************************************************/

bool SameHemisphere(float3 wi, float3 wo, float3 N)
{
	return (dot(wi, N) > 0) && (dot(wo, N) > 0);
}

//フレネル項
float FresnelSchlick(float LoH)
{
	return LoH<1 ? pow(1 - LoH, 5) : 0;
}

//媒質の屈折率n1からn2へ入射した時の反射率 ※n1とn2が逆でも結果は一緒
float IORtoF0(float n1, float n2)
{
    return sqr((n2-n1)/(n2+n1));
}

float IORtoF0(float IOR)
{
    return sqr((IOR-1)/(IOR+1));
}

float IORtoSpecular(float IOR)
{
	return IORtoF0(IOR)/0.08;
}

//a2はroughness^4
float SmithG1(float a2, float NoV)
{
	return 2 / (1 + sqrt(a2/(NoV*NoV) + (1-a2)));
}

// SmithG1 / 2NoV
float SmithG2(float a2, float NoV)
{
	return 1 / (NoV + sqrt(a2 + (1-a2)*sqr(NoV)));
}


//NoL入り、F抜きです, wiはV (-rd)
//aはroughness^2
float MicrofacetBRDFdivPDF(float a, float3 wi, float3 wo, float3 N)
{
	//法線のスムージングなどの結果裏向きの面がレンダリングされそうになる事はあるのでそれに対策する
	if (!SameHemisphere(wi, wo, N))
		return 0;
	float NoL = max(dot(N, wi), 1e-10);
	return SmithG1(a*a, NoL);
}

//anisotropic GGX のBRDFをサンプリング時のpdfで割った値を返す(NoL入り、F抜き)
//aはroughness^2(T方向とB方向それぞれ)
float GGXAnisoBRDFdivPDF(float2 a, float3 wo, float3 wm, float3x3 TBN, bool refract = false)
{
    float NoL = dot(TBN[2], wo);

    if (refract)
        NoL = max(abs(NoL),1e-10);
    else
        NoL = max(NoL,1e-10);

    //反射率をpdfで割るとwo側のG項だけになる
    float a2 = sqr(dot(wm,TBN[0])*a.x) + sqr(dot(wm,TBN[1])*a.y);
	return SmithG1(a2, NoL);
}

//Trowbridge-Reitz法線分布関数。接空間でのマイクロ法線を代入してpdfを得る
float D_GGX(float2 a, float3 wmTan)
{
    return 1 / (PI * a.x * a.y * sqr(sqr(wmTan.x / a.x) + sqr(wmTan.y / a.y) + sqr(wmTan.z)));
}

//light sample用、GGXAnisoのBRDFのみ(F抜き、NoL抜き), wiとwoは逆にしても同じ値が返る
float GGXAnisoBRDF(float2 a, float3 wi, float3 wo, float3x3 TBN)
{
    //仕方ないので
    if (any(a<1e-6))
        return 0;

    float3 H = normalize(wi+wo);
    float3 wmTan = mul(TBN,H);
    float NoL = max(abs(dot(wo, TBN[2])),1e-10);
    float NoV = max(abs(dot(wi, TBN[2])),1e-10);
    float D =  D_GGX(a,wmTan);
    float a2 = sqr(wmTan.x*a.x) + sqr(wmTan.y*a.y);
    float G = SmithG2(a2, NoL) * SmithG2(a2, NoV);

	return D*G;
}

//F抜き、NoL抜き
//etaI:wi(V)側の媒質屈折率, GGX原著論文でのoがwiで、iがwoなので注意
// wiとwo、etaIとetaOを反転すると、返り値/η^2は同じになる(ηは光の出る方向の媒質屈折率)
float GGXAnisoBTDF(float2 a, float3 wi, float3 wo, float3x3 TBN, float etaI, float etaO)
{
    //仕方ないので
    if (any(a<1e-6))
        return 0;

    float NoL = max(abs(dot(wo, TBN[2])),1e-10);
    float NoV = max(abs(dot(wi, TBN[2])),1e-10);
    float3 H = -normalize( etaI*wi + etaO*wo);

    float3 wmTan = mul(TBN,H);

    float a2 = sqr(wmTan.x*a.x) + sqr(wmTan.y*a.y);
    float D = D_GGX(a, wmTan);
    float G = SmithG2(a2, NoL) * SmithG2(a2,NoV);   //GGX原著で言うと G(i,o,ht) / 4|N・i||N・o|
    float IoH = dot(wi,H);
    float OoH = dot(wo,H);
    float J = etaI * etaI * abs(IoH)/sqr(etaO*OoH + etaI*IoH);  //ヤコビヤン
    return  J*D*G * abs(OoH*IoH) * 4;
}

//visible normalの分布(ωmから見た分布、ヤコビアン適用前)
float D_VNDF(float2 a, float3 wi, float3 wm, float3x3 TBN)
{
    float3 wiTan = mul(TBN,wi);
    float3 wmTan = mul(TBN,wm);
    float a2 = sqr(wmTan.x*a.x) + sqr(wmTan.y*a.y);

    return SmithG1(a2,wiTan.z) * max(0,dot(wi,wm)) * D_GGX(a,wmTan) / wiTan.z;
}

//上記二つからVNDFのPDFを求める
//wiは-rdの事, fはNoL入りBRDF
float VNDFPDF(float2 a, float3 wi, float3 wm, float3x3 TBN)
{
    /* D_GGX(wm) G2(wi) / 2
    導出 :
        VNDFのωm基準のpdf = Dωi(ωm) = G1(ωi)|ωi・ωm|D(ωm) / |ωi・ωg|
        つまり、ωo基準のVNDFのPDF Dωi(ωo) = G1(ωi)|ωi・ωm|D(ωm) / |ωi・ωg| * (1 / 4|ωi・ωm|)
        = G1(ωi)D(ωm) / 4|ωi・ωg|
        = G2(ωi)D(ωm) / 2

        透過の場合のωo側から見たVNDFのPDFは以下のようにちょっと長い
        J = ηo^2 * |ωo・ωm| / (ηi(ωi・ωm) + ηo(ωo・ωm))^2 として
        Dωi(ωo) = J * G1(ωi) |ωi・ωm| D(ωm) / |ωi・ωg|
                = 2 * J * G2(ωi) |ωi・ωm| D(ωm)
    */
    //ほぼ完全鏡面反射の場合は1点だけ∞で残り0になる。そういうのは扱えないので
    if (any(a<1e-6))
        return 0;

    float3 wmTan = mul(TBN,wm);
    float a2 = sqr(wmTan.x*a.x) + sqr(wmTan.y*a.y);
    return SmithG2(a2, dot(TBN[2],wi)) * D_GGX(a, wmTan) / 2;
    //文献にある↓と同値
    //return D_VNDF(a, wi,wm,TBN) / (4*(abs(dot(wi,wm))));
}

//透過版VNDFPDF
float VNDFPDF_T(float2 a, float3 wi, float3 wo, float3x3 TBN, float etaI, float etaO)
{
    //仕方ないので
    if (any(a<1e-6))
        return 0;

    float3 wm = -normalize(etaI*wi + etaO*wo);
    float3 wmTan = mul(TBN,wm);
    float a2 = sqr(wmTan.x*a.x) + sqr(wmTan.y*a.y);

    float IoH = dot(wi,wm);
    float OoH = dot(wo,wm);
    float J = etaI * etaI * abs(IoH) / sqr(etaO*OoH + etaI*IoH);  //ヤコビヤン
    return 2 * J * SmithG2(a2, dot(TBN[2],wi)) * abs(dot(wi,wm)) * D_GGX(a, wmTan);
}

//高roughness時に鏡面反射分から失われるエネルギーを補填するMultiScattering項
//aはroughness^2, 返り値をBRDFに掛けるだけでOK
//Angelo Pesce氏のブログを参考に、ndotvをhdotvに変更
//http://c0de517e.blogspot.com/2019/08/misunderstanding-multiscattering.html
//http://c0de517e.blogspot.com/2019/08/misunderstanding-multilayering-diffuse.html
float3 GGXMultiScatteringTerm(float2 a, float3 F0, float3 V, float3 H, float3x3 TBN)
{
    float a2 = (sqr(dot(V,TBN[0])*a.x) + sqr(dot(V,TBN[1])*a.y));
    float3 ms = (1 + F0 * 2 * a2 * abs(dot(TBN[2],V)));
	return ms;
}

//要検証
float3 FastMSX(float2 a, float3 F, float3 V, float3 L, float3x3 TBN)
{
    //完全鏡面反射に近い場合は0を返す
    if (any(a < 1e-6)) {
        return 0;
    }

    float3 H = normalize(V+L);
    float3 C = normalize(TBN[2]+H);
    float cosVC = dot(C,V);
    float vc = acos(cosVC);
    float m = (PI-acos(dot(V,L)))/4;
    float OP = sin(vc-m) / sin(vc+m);

    float3 FI = F*F;
    float GI = 1 - max(0,OP);
    float DI = D_GGX(a,mul(TBN,H));
    return FI*DI*GI / (2*cosVC);
}

//HG関数。muはcosθ
float HenyeyGreenstein(float mu, float g)
{
	float nom = 1-g*g;
	float denom = (4*PI) * pow(1+g*g+2*g*mu , 1.5);
	return nom/denom;
}



/****************************************************************************
  色
****************************************************************************/
//sRGB ガンマ→リニア
float3 ToLinear(float3 gamma)
{
	return (gamma <= 0.04045) ? gamma / 12.92 : pow((gamma + 0.055) / 1.055, 2.4);
    //return select(gamma <= 0.04045, gamma / 12.92, pow((gamma + 0.055) / 1.055, 2.4));
}

//sRGB リニア→ガンマ
float3 ToGamma(float3 lin)
{
	return (lin <= 0.0031308) ? lin * 12.92 : 1.055 * pow(lin, 1.0 / 2.4) - 0.055;
    //return select(lin <= 0.0031308, lin * 12.92, 1.055 * pow(lin, 1.0 / 2.4) - 0.055);;
}

float3 Hue(float hue)
{
    float3 rgb = frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0));
    rgb = abs(rgb * 2.0 - 1.0);
    return clamp(rgb * 3.0 - 1.0, 0.0, 1.0);
}

//h=0..1
float3 HSVtoRGB(float3 hsv)
{
    return ((Hue(hsv.x) - 1.0) * hsv.y + 1.0) * hsv.z;
}

float3 RGBtoHSV(float3 rgb)
{
    const float third = 1.0 / 3.0;
    const float sixth = 1.0 / 6.0;
    float maxrgb = max(max(rgb.r, rgb.g), rgb.b);
	if (maxrgb == 0)
		return 0;

    float3 hsv;
    float minrgb = min(min(rgb.r, rgb.g), rgb.b);
    float sa = maxrgb - minrgb;
    
    hsv.b = maxrgb;
    if (sa) {
        if (rgb.r == maxrgb) {
            hsv.r = sixth * (rgb.g - rgb.b) / sa;
        } else if (rgb.g == maxrgb) {
            hsv.r = sixth * (rgb.b - rgb.r) / sa + third;
        } else {
            hsv.r = sixth * (rgb.r - rgb.g) / sa + third * 2;
        }
        hsv.r += 1;
        hsv.r = frac(hsv.r);
    } else {
        //彩度0の入力だったらh=0とする
        hsv.r = 0;
        hsv.g = 0;
    }

    if (maxrgb < 0.0001)
        hsv.g = 0;
    else
        hsv.g = (maxrgb - minrgb) / maxrgb;

    return hsv;
}

//CIE XYZ→D65 sRGB (リニア)
//mul(xyz, XYZ2RGB)という感じで使う
//出典:Ocean Optic Web Book(CC-BY 2.0)より
//https://www.oceanopticsbook.info/view/photometry-and-visibility/from-xyz-to-rgb
//↑のをHLSL向けに転置した
static float3x3 XYZ2RGB =
{
     3.2404542, -0.9692660,  0.0556434,
    -1.5371385,  1.8760108, -0.2040259,
    -0.4985314,  0.0415560,  1.0572252
};

static float3x3 RGB2XYZ =
{
    0.4124564, 0.2126729, 0.019339,
    0.3575761, 0.7151522, 0.1191920,
    0.1804375, 0.0721750, 0.9503041
};

//リニアrgb(カラースペースはsRGB D65)から輝度を計算
//XYZ刺激値のうちY刺激値で近似
//ITU-R BT.709 (1125/60/2:1)YCrCbのYと一致している
float RGBtoY(float3 rgb)
{
    return dot(rgb,RGB2XYZ._12_22_32);
}

//oklabカラースペース
//Bj?rn Ottosson氏 "A perceptual color space for image processing"
//https://bottosson.github.io/posts/oklab/ より
float3 RGBtoOKLAB(float3 c) 
{
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
	float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
	float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = pow(l,1/3.0);
    float m_ = pow(m,1/3.0);
    float s_ = pow(s,1/3.0);

    return float3(
        0.2104542553f*l_ + 0.7936177850f*m_ - 0.0040720468f*s_,
        1.9779984951f*l_ - 2.4285922050f*m_ + 0.4505937099f*s_,
        0.0259040371f*l_ + 0.7827717662f*m_ - 0.8086757660f*s_);
}

float3 OKLABtoRGB(float3 c) 
{
    float l_ = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;
    float m_ = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;
    float s_ = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;

    float l = l_*l_*l_;
    float m = m_*m_*m_;
    float s = s_*s_*s_;

    return float3(
		+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
		-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
		-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    );
}

//Ver4.00までの実装が誤っていたので修正＆名称をOKHSV→OKLChにした
//直交→極座標系OKLab
float3 RGBtoOKLCh(float3 c)
{
    float3 o = RGBtoOKLAB(c);
    float3 r;
    r.x = o.x;          //L
    r.y = length(o.yz); //C
    r.z = degrees(atan2(o.z,o.y))/360; //h
    return r;
}

float3 OKLChtoRGB(float3 c)
{
    float3 o;
    o.x = c.x;              //L
    sincos(radians(c.z*360), o.z, o.y);  //A,B
    o.yz *= c.y;
    return OKLABtoRGB(o);
}

float3 InverseACESTonemap(float3 x)
{
	x = saturate(x);
	float3 nom = 0.121399 * (x - 0.0508475) + 0.00205761 * sqrt(-10127 * x * x + 13702 * x + 9);
	float3 denom = 1.03292 - x;
	return nom / denom;
}

float3 ACESTonemap(float3 x)
{
	// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;

	return (x * (a * x + b)) / (x * (c * x + d) + e);
}

//TinyTexel氏作の物を少し改変 (CC0)
//https://www.shadertoy.com/view/WltSRB
float3 ACESCgTonemap(float3 x)
{
	// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
	const float3x3 ACESInputMat = float3x3
	(
		0.59719, 0.35458, 0.04823,
		0.07600, 0.90834, 0.01566,
		0.02840, 0.13383, 0.83777
	);
	x = mul(ACESInputMat, x);

	//ShaderToy TinyTexel氏によるフィッティングカーブの3本目(ToneTF2)
	//Tonemap_ACES(Narkowicz氏の近似ACES Lのみフィット版)の場合、元の輝度1→0.8にマッピングされているが
	//この関数では元の輝度2→0.8にマッピングされるので、輝度を2倍する
	//x*=2;	
    //float3 a = (x            + 0.0822192) * x;
    //float3 b = (x * 0.983521 + 0.5001330) * x + 0.274064;
    //float3 c =  a / b;

	//↑のカーブを使うとNarkowicz氏のカーブと少し雰囲気が変わってやや立ち上がりが速い。見た目に違和感を減らすため
	//カーブ自体はNarkowicz氏のカーブを使う
	float3 c = ACESTonemap(x);

	// ODT_SAT => XYZ => D60_2_D65 => sRGB
	const float3x3 ACESOutputMat = {
		1.60475, -0.53108, -0.07367,
		-0.10208,  1.10813, -0.00605,
		-0.00327, -0.07276,  1.07602
	};
    return max(mul(ACESOutputMat,c),0);
}


#define NUM_CIE1931 95
#define MIN_CIE1931 360
#define MAX_CIE1931 830

//http://www.cvrl.org/cie.htmより引用
#define SUM_CIE1931 float3(21.373139988621006,21.371407850470007,21.378664308989997)
static float4 CIE1931[95] = {
    360,0.000129900000,0.000003917000,0.000606100000,
    365,0.000232100000,0.000006965000,0.001086000000,
    370,0.000414900000,0.000012390000,0.001946000000,
    375,0.000741600000,0.000022020000,0.003486000000,
    380,0.001368000000,0.000039000000,0.006450001000,
    385,0.002236000000,0.000064000000,0.010549990000,
    390,0.004243000000,0.000120000000,0.020050010000,
    395,0.007650000000,0.000217000000,0.036210000000,
    400,0.014310000000,0.000396000000,0.067850010000,
    405,0.023190000000,0.000640000000,0.110200000000,
    410,0.043510000000,0.001210000000,0.207400000000,
    415,0.077630000000,0.002180000000,0.371300000000,
    420,0.134380000000,0.004000000000,0.645600000000,
    425,0.214770000000,0.007300000000,1.039050100000,
    430,0.283900000000,0.011600000000,1.385600000000,
    435,0.328500000000,0.016840000000,1.622960000000,
    440,0.348280000000,0.023000000000,1.747060000000,
    445,0.348060000000,0.029800000000,1.782600000000,
    450,0.336200000000,0.038000000000,1.772110000000,
    455,0.318700000000,0.048000000000,1.744100000000,
    460,0.290800000000,0.060000000000,1.669200000000,
    465,0.251100000000,0.073900000000,1.528100000000,
    470,0.195360000000,0.090980000000,1.287640000000,
    475,0.142100000000,0.112600000000,1.041900000000,
    480,0.095640000000,0.139020000000,0.812950100000,
    485,0.057950010000,0.169300000000,0.616200000000,
    490,0.032010000000,0.208020000000,0.465180000000,
    495,0.014700000000,0.258600000000,0.353300000000,
    500,0.004900000000,0.323000000000,0.272000000000,
    505,0.002400000000,0.407300000000,0.212300000000,
    510,0.009300000000,0.503000000000,0.158200000000,
    515,0.029100000000,0.608200000000,0.111700000000,
    520,0.063270000000,0.710000000000,0.078249990000,
    525,0.109600000000,0.793200000000,0.057250010000,
    530,0.165500000000,0.862000000000,0.042160000000,
    535,0.225749900000,0.914850100000,0.029840000000,
    540,0.290400000000,0.954000000000,0.020300000000,
    545,0.359700000000,0.980300000000,0.013400000000,
    550,0.433449900000,0.994950100000,0.008749999000,
    555,0.512050100000,1.000000000000,0.005749999000,
    560,0.594500000000,0.995000000000,0.003900000000,
    565,0.678400000000,0.978600000000,0.002749999000,
    570,0.762100000000,0.952000000000,0.002100000000,
    575,0.842500000000,0.915400000000,0.001800000000,
    580,0.916300000000,0.870000000000,0.001650001000,
    585,0.978600000000,0.816300000000,0.001400000000,
    590,1.026300000000,0.757000000000,0.001100000000,
    595,1.056700000000,0.694900000000,0.001000000000,
    600,1.062200000000,0.631000000000,0.000800000000,
    605,1.045600000000,0.566800000000,0.000600000000,
    610,1.002600000000,0.503000000000,0.000340000000,
    615,0.938400000000,0.441200000000,0.000240000000,
    620,0.854449900000,0.381000000000,0.000190000000,
    625,0.751400000000,0.321000000000,0.000100000000,
    630,0.642400000000,0.265000000000,0.000049999990,
    635,0.541900000000,0.217000000000,0.000030000000,
    640,0.447900000000,0.175000000000,0.000020000000,
    645,0.360800000000,0.138200000000,0.000010000000,
    650,0.283500000000,0.107000000000,0.000000000000,
    655,0.218700000000,0.081600000000,0.000000000000,
    660,0.164900000000,0.061000000000,0.000000000000,
    665,0.121200000000,0.044580000000,0.000000000000,
    670,0.087400000000,0.032000000000,0.000000000000,
    675,0.063600000000,0.023200000000,0.000000000000,
    680,0.046770000000,0.017000000000,0.000000000000,
    685,0.032900000000,0.011920000000,0.000000000000,
    690,0.022700000000,0.008210000000,0.000000000000,
    695,0.015840000000,0.005723000000,0.000000000000,
    700,0.011359160000,0.004102000000,0.000000000000,
    705,0.008110916000,0.002929000000,0.000000000000,
    710,0.005790346000,0.002091000000,0.000000000000,
    715,0.004109457000,0.001484000000,0.000000000000,
    720,0.002899327000,0.001047000000,0.000000000000,
    725,0.002049190000,0.000740000000,0.000000000000,
    730,0.001439971000,0.000520000000,0.000000000000,
    735,0.000999949300,0.000361100000,0.000000000000,
    740,0.000690078600,0.000249200000,0.000000000000,
    745,0.000476021300,0.000171900000,0.000000000000,
    750,0.000332301100,0.000120000000,0.000000000000,
    755,0.000234826100,0.000084800000,0.000000000000,
    760,0.000166150500,0.000060000000,0.000000000000,
    765,0.000117413000,0.000042400000,0.000000000000,
    770,0.000083075270,0.000030000000,0.000000000000,
    775,0.000058706520,0.000021200000,0.000000000000,
    780,0.000041509940,0.000014990000,0.000000000000,
    785,0.000029353260,0.000010600000,0.000000000000,
    790,0.000020673830,0.000007465700,0.000000000000,
    795,0.000014559770,0.000005257800,0.000000000000,
    800,0.000010253980,0.000003702900,0.000000000000,
    805,0.000007221456,0.000002607800,0.000000000000,
    810,0.000005085868,0.000001836600,0.000000000000,
    815,0.000003581652,0.000001293400,0.000000000000,
    820,0.000002522525,0.000000910930,0.000000000000,
    825,0.000001776509,0.000000641530,0.000000000000,
    830,0.000001251141,0.000000451810,0.000000000000
};

#define SUM_D65 7880.3178
static float2 D65Illuminant[95] = {
360, 46.638300,
365, 49.363700,
370, 52.089100,
375, 51.032300,
380, 49.975500,
385, 52.311800,
390, 54.648200,
395, 68.701500,
400, 82.754900,
405, 87.120400,
410, 91.486000,
415, 92.458900,
420, 93.431800,
425, 90.057000,
430, 86.682300,
435, 95.773600,
440, 104.865000,
445, 110.936000,
450, 117.008000,
455, 117.410000,
460, 117.812000,
465, 116.336000,
470, 114.861000,
475, 115.392000,
480, 115.923000,
485, 112.367000,
490, 108.811000,
495, 109.082000,
500, 109.354000,
505, 108.578000,
510, 107.802000,
515, 106.296000,
520, 104.790000,
525, 106.239000,
530, 107.689000,
535, 106.047000,
540, 104.405000,
545, 104.225000,
550, 104.046000,
555, 102.023000,
560, 100.000000,
565, 98.167100,
570, 96.334200,
575, 96.061100,
580, 95.788000,
585, 92.236800,
590, 88.685600,
595, 89.345900,
600, 90.006200,
605, 89.802600,
610, 89.599100,
615, 88.648900,
620, 87.698700,
625, 85.493600,
630, 83.288600,
635, 83.493900,
640, 83.699200,
645, 81.863000,
650, 80.026800,
655, 80.120700,
660, 80.214600,
665, 81.246200,
670, 82.277800,
675, 80.281000,
680, 78.284200,
685, 74.002700,
690, 69.721300,
695, 70.665200,
700, 71.609100,
705, 72.979000,
710, 74.349000,
715, 67.976500,
720, 61.604000,
725, 65.744800,
730, 69.885600,
735, 72.486300,
740, 75.087000,
745, 69.339800,
750, 63.592700,
755, 55.005400,
760, 46.418200,
765, 56.611800,
770, 66.805400,
775, 65.094100,
780, 63.382800,
785, 63.843400,
790, 64.304000,
795, 61.877900,
800, 59.451900,
805, 55.705400,
810, 51.959000,
815, 54.699800,
820, 57.440600,
825, 58.876500,
830, 60.312500
};

//XYZそれぞれの刺激値に比例した確率密度関数(数値を平均するとX,Y,Zとも1になる)
static float3 PDF_CIE1931[95] = {
0.0005773835761413646, 1.7411815010203755e-05, 0.0026933160635197917,
0.0010316453273472726, 3.096075862804931e-05, 0.004825839374661761,
0.0018441604752967833, 5.50759223835651e-05, 0.008647406466935348,
0.003296286836539153, 9.788311629427793e-05, 0.015490677771704328,
0.00608052911594601, 0.00017336246755117343, 0.028661757635734565,
0.009938642619338654, 0.0002844922544429513, 0.0468808076835063,
0.018859418888127866, 0.0005334229770805338, 0.08909588187878643,
0.0340029588720665, 0.0009646065502206317, 0.1609057493153797,
0.06360553483127734, 0.001760295824365761, 0.3015039132865509,
0.10307563611022512, 0.0028449225444295134, 0.4896938297308712,
0.19339460660439392, 0.005378681685562048, 0.9216197848110951,
0.34505224800503564, 0.00969051741696303, 1.649939373675794,
0.5972964200298427, 0.017780765902684455, 2.86884152880445,
0.9546164022161728, 0.032449897772399136, 4.617208917887883,
1.2618875848078013, 0.05156422111778492, 6.157166701225909,
1.4601270574475615, 0.07485702445030157, 7.211919218693418,
1.5480458190801727, 0.10223940394043562, 7.763380237473828,
1.5470679562106493, 0.1324667059749992, 7.921308719403366,
1.4943522578808834, 0.16891727607550233, 7.87469448824296,
1.4165677114415156, 0.21336919083221348, 7.750226936784141,
1.2925569202610379, 0.26671148854026683, 7.417395105143105,
1.1160971206243004, 0.3284996500520953, 6.790391481050311,
0.8683422281368512, 0.404423520456558, 5.721863547319954,
0.6316105170876667, 0.5005285601605675, 4.6298729691161045,
0.4251036583692079, 0.6179705189477983, 3.612492267233164,
0.2575780139432474, 0.7525709168311197, 2.7381972584406786,
0.1422790475156665, 0.9246887307691052, 2.0671123023067755,
0.06533901900906897, 1.1495265156085501, 1.5699530856979744,
0.021779673003022988, 1.4357968466417699, 1.2086816849981576,
0.010667594940256157, 1.8105264880408447, 0.9433938298717236,
0.04133693039349261, 2.2359313122625704, 0.7029905976717226,
0.1293445886506059, 2.7035654555031714, 0.49635935372902273,
0.28122447161250297, 3.1560859477264906, 0.3477181241334154,
0.4871535022716979, 3.5259258785023277, 0.25440087703294617,
0.7356195677551642, 3.8317550520285004, 0.18734566117471443,
1.0034202045847223, 4.0666838660368665, 0.13259949073656258,
1.290778987770995, 4.240712667790243, 0.09020675810831837,
1.5988057916708918, 4.3576212036003925, 0.059545347716821,
1.9266116500393906, 4.4227437032381225, 0.038882218878868356,
2.2759762732990247, 4.445191475671114, 0.02555117088256515,
2.6424521633259523, 4.4229655182927585, 0.01733036239519417,
3.015373503112407, 4.350064378091752, 0.012220122886261942,
3.387405876653841, 4.2318222848389, 0.009331733597412245,
3.744770307152422, 4.069128276829338, 0.007998628797781925,
4.072798851565299, 3.867316583833869, 0.007332080841649429,
4.3497118368894485, 3.6286098015903305, 0.006221155731608164,
4.561730286327039, 3.3650099470830335, 0.0048880509319778435,
4.696853155570284, 3.088963556443857, 0.004443682665434403,
4.721299727308371, 2.804915821148473, 0.0035549461323475225,
4.6475155289716, 2.519534528410387, 0.0026662095992606412,
4.45638778629201, 2.2359313122625704, 0.001510852106247697,
4.171029621640158, 1.9612184790660954, 0.0010664838397042567,
3.797885595809325, 1.6936179522306944, 0.0008442997064325366,
3.3398461825451986, 1.4269064636904276, 0.0004443682665434403,
2.8553595790085646, 1.1779757410528453, 0.0002221840888348935,
2.4086540408853385, 0.9646065502206318, 0.00013331047996303208,
1.9908399057253054, 0.777908508242445, 8.887365330868806e-05,
1.6036951060185092, 0.614325461937748, 4.443682665434403e-05,
1.2601096523177586, 0.4756354878968092, 0.0,
0.9720845889308424, 0.3627276244147629, 0.0,
0.7329526690201001, 0.27115668001593796, 0.0,
0.538713544482936, 0.19816663598541828, 0.0,
0.38847824907432843, 0.14224612722147564, 0.0,
0.28269126591678817, 0.10312844223556984, 0.0,
0.20788475639824186, 0.07556825508640895, 0.0,
0.1462349473060115, 0.05298668238999968, 0.0,
0.10089766880992283, 0.03649502201525985, 0.0,
0.07040612660569064, 0.025439830815265786, 0.0,
0.05048954905898339, 0.01823417543320291, 0.0,
0.036051652701017796, 0.013019965832240694, 0.0,
0.025737110704971866, 0.0092948953756283, 0.0,
0.01826584279183344, 0.006596664149895934, 0.0,
0.012887019181395027, 0.0046541154750276564, 0.0,
0.009108303698176466, 0.0032894416919966242, 0.0,
0.006400428064048167, 0.0023114995673489793, 0.0,
0.00444460587216362, 0.0016051586418648392, 0.0,
0.003067282909057939, 0.0011077417157372416, 0.0,
0.0021158343380558993, 0.0007641284146678645, 0.0,
0.0014770223054173148, 0.0005334229770805338, 0.0,
0.001043762381750036, 0.0003769522371369105, 0.0,
0.0007385109304670962, 0.0002667114885402669, 0.0,
0.0005218809686334567, 0.00018847611856845524, 0.0,
0.0003692555541301726, 0.00013335574427013344, 0.0,
0.0002609405732133529, 9.423805928422762e-05, 0.0,
0.00018450467746430698, 6.663342022031e-05, 0.0,
0.00013047028660667645, 4.711902964211381e-05, 0.0,
9.189168512654831e-05, 3.3186465999917836e-05, 0.0,
6.471572032637225e-05, 2.3371927740783583e-05, 0.0,
4.5577210485619935e-05, 1.646009951526257e-05, 0.0,
3.209815311953436e-05, 1.1592170330255131e-05, 0.0,
2.2605824893171125e-05, 8.164038664217568e-06, 0.0,
1.591983864706598e-05, 5.7494106546330185e-06, 0.0,
1.121219788611236e-05, 4.049258270933088e-06, 0.0,
7.896282674883137e-06, 2.85172368738729e-06, 0.0,
5.561110583811263e-06, 2.008381960622966e-06, 0.0
};


//↑の累積分布関数
static float3 CDF_CIE1931[95] = {
6.0777218541196274e-06, 1.8328226326530267e-07, 2.835069540547149e-05,
1.6937146352511972e-05, 5.091849856658217e-07, 7.91490046124374e-05,
3.63493618819518e-05, 1.0889315370717702e-06, 0.0001701743358433358,
7.104711805604815e-05, 2.1192801296431165e-06, 0.00033323410186127605,
0.0001350526876975851, 3.944148209129153e-06, 0.0006349368138163767,
0.00023966997842746567, 6.9388035190549565e-06, 0.0011284189999585482,
0.0004381901772498643, 1.2553782225165837e-05, 0.0020662703881563003,
0.0007961160601137222, 2.2707535385383015e-05, 0.003760015117791876,
0.0014656480057061152, 4.123696511554892e-05, 0.006933740520808201,
0.0025506547016032216, 7.118351821480695e-05, 0.012088412412712109,
0.004586387402702105, 0.00012780122016809166, 0.02178967330546048,
0.008218516329070901, 0.0002298066666624393, 0.039157456186258315,
0.014505847066227142, 0.000416972623532802, 0.06935578806841042,
0.024554440773765805, 0.0007585504948212139, 0.11795798720407233,
0.03783746798226898, 0.0013013317697452658, 0.1827702682696082,
0.053207226481716996, 0.0020893004481694925, 0.25868520741374945,
0.06950244562992934, 0.0031655047001740784, 0.3404049993871582,
0.08578737148477827, 0.00455989107885828, 0.4237871964335094,
0.10151739525194546, 0.006337967669126726, 0.5066787173623827,
0.11642863431975091, 0.008583959151571077, 0.5882600535390579,
0.13003449663828814, 0.011391448504626518, 0.6663378967510905,
0.14178288738170183, 0.014849339557806469, 0.737815701814778,
0.15092333188840554, 0.01910642924682287, 0.798045844418146,
0.1575718636472231, 0.024375150932723577, 0.8467813493562102,
0.1620466389984779, 0.030880103763753036, 0.8848075837481382,
0.16475798651366996, 0.038801902888291134, 0.9136307127843558,
0.16625566069804543, 0.04853546847533435, 0.9353897896507429,
0.1669434398455093, 0.06063574758700329, 0.9519156116054585,
0.1671726995613306, 0.07574939860428508, 0.9646385767107022,
0.16728499003438593, 0.09480757216260977, 0.9745690380777728,
0.16772011561747532, 0.11834369123905787, 0.9819689391058963,
0.1690816376032712, 0.14680227498119652, 0.987193774408307,
0.17204189519919227, 0.1800242323256859, 0.9908539651886588,
0.17716982680205226, 0.21713924157307882, 0.9935318691574266,
0.18491319067315926, 0.257473505278642, 0.9955039287487394,
0.19547550861615634, 0.3002807038685037, 0.9968997128617558,
0.20906265585585104, 0.3449197845820852, 0.9978492576839486,
0.22589219050501833, 0.39078948146208936, 0.9984760508178099,
0.24617231313701193, 0.43734467833828017, 0.9988853373323243,
0.2701299581191069, 0.48413616755587086, 0.9991542970258249,
0.29794524404885375, 0.5306936993273735, 0.9993367218931428,
0.32968601776582646, 0.5764838506757077, 0.9994653547656297,
0.3653429217306038, 0.6210293484108541, 0.9995635835403393,
0.4047615565427345, 0.6638622776406365, 0.9996477796329475,
0.44763312340131656, 0.7045708732599404, 0.9997249594312807,
0.49341956378962654, 0.7427667659082597, 0.9997904452810871,
0.5414377773299113, 0.7781879232459759, 0.9998418984487922,
0.59087833686223, 0.8107033291032797, 0.9998886740557968,
0.640576228728634, 0.8402287587995794, 0.9999260945414006,
0.6894974448230718, 0.8667501748881098, 0.9999541599056032,
0.7364067899419351, 0.8902862939645579, 0.9999700636119848,
0.7803123649065683, 0.9109306990073589, 0.999981289757666,
0.8202901080203506, 0.9287582563992609, 0.9999901771229969,
0.8554463836260895, 0.9437783244381076, 0.9999948546836973,
0.8855028002472323, 0.9561780690807692, 0.9999971934635798,
0.9108570533091831, 0.9663318222409862, 0.9999985967317899,
0.9318132628431337, 0.9745203328540647, 0.99999953224393,
0.948694263959118, 0.9809869166639358, 1.0,
0.9619585760887787, 0.9859936060102179, 1.0,
0.9721910454459454, 0.9898117915303735, 1.0,
0.9799063366987886, 0.9926660723726465, 1.0,
0.9855770055880827, 0.9947520369619667, 1.0,
0.9896662503151809, 0.9962493646169296, 1.0,
0.992641947851147, 0.9973349271667776, 1.0,
0.9948302084448127, 0.9981303824834766, 1.0,
0.9963695236796128, 0.9986881370349503, 1.0,
0.9974316044039279, 0.9990722951614266, 1.0,
0.998172721526093, 0.9993400828542189, 1.0,
0.9987041904635561, 0.9995320215429895, 1.0,
0.9990836815446195, 0.9996690738149079, 1.0,
0.9993545984994087, 0.9997669148188618, 1.0,
0.9995468705287963, 0.9998363533888608, 1.0,
0.9996825233622848, 0.9998853440780716, 1.0,
0.9997784002433182, 0.9999199697800927, 1.0,
0.9998457731703082, 0.9999443013544859, 1.0,
0.9998925584952784, 0.9999611977612423, 1.0,
0.9999248456837948, 0.9999728582003553, 1.0,
0.9999471176241953, 0.9999809016573519, 1.0,
0.9999626652274102, 0.9999865166360579, 1.0,
0.9999736521998497, 0.9999904845543436, 1.0,
0.9999814259991178, 0.9999932920436967, 1.0,
0.9999869194829982, 0.9999952760028396, 1.0,
0.999990806383568, 0.999996679747516, 1.0,
0.9999935531264439, 0.9999976717270874, 1.0,
0.9999954952809436, 0.9999983731315109, 1.0,
0.9999968686523816, 0.9999988691212965, 1.0,
0.9999978359332777, 0.9999992184525176, 1.0,
0.9999985171513864, 0.9999994644728096, 1.0,
0.9999989969114969, 0.9999996377370151, 1.0,
0.9999993347867928, 0.9999997597598606, 1.0,
0.9999995727428443, 0.9999998456971096, 1.0,
0.9999997403200932, 0.9999999062172218, 1.0,
0.9999998583432289, 0.9999999488409932, 1.0,
0.9999999414619938, 0.9999999788591373, 1.0,
1.0, 1.0, 1.0
};

//サイコロを投げると波長を選択する。返り値は波長番号で0～94
int SampleWaveChannel(float2 Xi)
{
    float3 Pch = 1/3.0;

    int ch = 2;   //X,Y,Zのどれでサンプリングする？
    if (Pch.x > Xi.x)
        ch = 0;
    else if (Pch.x + Pch.y > Xi.x)
        ch = 1;

    int idx = NUM_CIE1931-1;

    //CDFがXi以上になる最初のインデックスを探すとPDFに従った波長のサンプリングになる
    int l = 0;
    int r = NUM_CIE1931-1;
    while (l < r) {
        int mid = (l + r) / 2;
        if (CDF_CIE1931[mid][ch] <= Xi.y) {
            l = mid + 1;
        } else {
            r = mid;
        }
    }
    idx = l;


    return idx;
}

float3 WaveMISWeight(int idx)
{
    //各チャンネルのMISウェイト
    float3 pdf = PDF_CIE1931[idx];
    float3 weight = pdf / dot(pdf,1) * 3;  //3はX,Y,Zチャンネルのpdf
    //pdfとXYZ刺激値は比例しているので他に何も掛けなくてよいはず
    return weight;
}

float WaveChannelToLambda(int ch)
{
    return ch * 5 + MIN_CIE1931;
}

/*
int SampleWaveChannel(float Xi, out float3 weight)
{
    int ch = floor(Xi*NUM_CIE1931);
    weight = PDF_CIE1931[ch];
    return ch;
}
*/

//コーシーの分散公式(分散係数ABと波長l[nm]から屈折率を求める)
float Cauchy(float2 AB,float l)
{
    return AB.x + AB.y/(l*l);
}

//媒質の「アッベ数ν」と「波長lの時の屈折率n」と「アッベ数の根拠になった短波長lF、長波長lC」からコーシーの分散係数ABを求めるコード(自作)
//デフォルト値はレンズ業界の標準っぽいです(l=587.6nm(フラウンホーファーd線), lF=486.1nm(F線), lC=656.3(C線))
float2 AbbeToCauchy(float v, float n, float ld = 587.6, float lF = 486.1, float lC = 656.3)
{
    float nf_nc = (n-1)/v;    //F線とC線に対する屈折率nFとnCの差(nF-nC)
    float B = nf_nc * sqr(lF*lC) / ((lC+lF)*(lC-lF));
    float A = n - B / sqr(ld);
    return float2(A,B);
}

//コーシーの分散係数からアッベ数を求める
float CauchyToAbbe(float2 AB, float ld = 587.6, float lF = 486.1, float lC = 656.3)
{
    float n = Cauchy(AB,ld);
    float nF = Cauchy(AB, lF);
    float nC = Cauchy(AB, lC);
    return (n-1)/(nF-nC);
}

//宝石学での分散パラメータdと屈折率nD(フラウンホーファーD線)からコーシーの分散係数を求めるコード
float2 GemDispersionToCauchy(float d, float nD)
{
    float lG = 430.8;   //フラウンホーファーのG線
    float lB = 686.7;   //フラウンホーファーのB線
    float lD = 589.3;   //D線(D1線とD2線の間)
    float ng_nb = d;    //G線とB線に対する屈折率nGとnBの差(nG-nB)
    float B = ng_nb * sqr(lG*lB) / ((lB+lG)*(lB-lG));
    float A = nD - B / sqr(lD);
    return float2(A,B);
}

float CauchyToGemDispersion(float2 AB)
{
    float nG = Cauchy(AB, 430.8);
    float nB = Cauchy(AB, 686.7);
    return nG-nB;
}

//アッベ数から宝石学の分散パラメータを求める
float AbbeToGemDispersion(float v, float n, float ld = 587.6, float lF = 486.1, float lC = 656.3)
{
    float2 AB = AbbeToCauchy(v,n,ld,lF,lC);
    return CauchyToGemDispersion(AB);
}

//フラウンホーファーD線(589.3nm)に対する屈折率IOR,分散パラメータdの物質が 波長lの時は屈折率いくつになるか
float GemDispersedIOR(float IOR, float d, float l)
{
    float2 AB = GemDispersionToCauchy(d, IOR);
    return Cauchy(AB,l);
}

//↑をアッベ数から算出する(IORは波長ldの時の屈折率)
float GemDispersedIORFromAbbe(float IOR, float v, float l, float ld = 587.6, float lF = 486.1, float lC = 656.3)
{
    float2 AB = AbbeToCauchy(v,IOR,ld,lF,lC);
    return Cauchy(AB, l);
}

/*****************************************************************
    SH
******************************************************************/
//skyboxを2次までのSHをした結果格納用
//c[0]がl=0, c[1-3]がl=1,m=-1,0,+1、c[4-8]がl=2,m=-2～+2 に対応
struct SHCoeff {
	float4 c[9];
};

//畳み込み用ZH係数
//pre-integrated skinみたいに色付きでもOKなように、カラー対応。但し軸対称である事が前提
//ZHCosine[l]がl次のSHに対応
static float3x3 ZHCosine = {1,1,1, 0.66666667,0.66666667,0.66666667, 0.25,0.25,0.25};

//ZH込みでSHの係数からn方向からの照度を求める
float3 SHIrradiance(float3 n,  SHCoeff SH, float3x3 ZH = ZHCosine)
{
	float3 ret = 0;
    
    //係数はPeter-Pike Sloan氏の資料より引用
	ret = 0.282095 * SH.c[0].xyz * ZH[0];

	ret += -0.488603 * n.y * SH.c[1].xyz * ZH[1];
	ret += 0.488603 * n.z * SH.c[2].xyz * ZH[1];
	ret += -0.488603 * n.x * SH.c[3].xyz * ZH[1];

    ret += 1.092548 * n.x * n.y * SH.c[4].xyz * ZH[2];
    ret += -1.092548 * n.y * n.z * SH.c[5].xyz * ZH[2];
    ret += 0.315392 * (3 * n.z * n.z - 1) * SH.c[6].xyz * ZH[2];
    ret += -1.092548 * n.x * n.z * SH.c[7].xyz * ZH[2];
    ret += 0.546274 * (n.x * n.x - n.y * n.y) * SH.c[8].xyz * ZH[2];

	return max(0,ret);
}

#endif