#include "yrz.hlsli"

struct VSO {
	float4 position:SV_POSITION;
	float2 uv:TEXCOORD;
};
sampler samp : register(s0);


Texture2D<float4> Screen : register(t0);
//Texture2D<float4> Credit : register(t1);
//Texture2D<float4> Telop : register(t2);
//Texture2D<float4> Hanko : register(t3);


VSO VS( float4 pos : POSITION, float2 uv:TEXCOORD )
{
	VSO vso = (VSO)0;
	vso.position = pos;
	vso.position.w = 1;
	vso.uv = uv;
	return vso;
}


#define HALIGN_LEFT 0
#define HALIGN_CENTER 1
#define HALIGN_RIGHT 2
#define VALIGN_TOP 0
#define VALIGN_MIDDLE 1
#define VALIGN_BOTTOM 2

//画面destの位置margin(X+は右、Y+は下)にテクスチャを書き込んだ場合の座標Pにおけるピクセル値を返す
//元々描きこもうとしていた色をCに指定すべし
//colは文字の色。sRGBガンマで指定
//座標はピクセル単位
float4 PutFontSprite(Texture2D<float4>dest, float4 C, Texture2D<float4>tex, float4 col, int2 P, int2 margin, int halign = 0, int valign = 0)
{
    float4 o = C;

    int W,H;
    dest.GetDimensions(W,H);

    int SW,SH;
    tex.GetDimensions(SW,SH);
    int2 SS = {SW,SH};

    if (halign == HALIGN_CENTER) {
        P.x -= (W-SW)/2;
    } else if (halign == HALIGN_RIGHT) {
        P.x -= (W-SW);
    }
    if (valign == VALIGN_MIDDLE) {
        P.y -= (H-SH)/2;
    } else if (valign == VALIGN_BOTTOM) {
        P.y -= (H-SH);
    }

    P-=margin;
    if (all(P == clamp(P,0,SS-1))) {
        col.rgb = ToLinear(col.rgb);
        o.rgb = lerp(o.rgb, col.rgb, tex[P].r * col.a);
    }
    return o;
}

float4 PutSprite(Texture2D<float4>dest, float4 C, Texture2D<float4>tex, int2 P, int2 margin, int halign = 0, int valign = 0)
{
    float4 o = C;

    int W,H;
    dest.GetDimensions(W,H);

    int SW,SH;
    tex.GetDimensions(SW,SH);
    int2 SS = {SW,SH};

    if (halign == HALIGN_CENTER) {
        P.x -= (W-SW)/2;
    } else if (halign == HALIGN_RIGHT) {
        P.x -= (W-SW);
    }
    if (valign == VALIGN_MIDDLE) {
        P.y -= (H-SH)/2;
    } else if (valign == VALIGN_BOTTOM) {
        P.y -= (H-SH);
    }

    P-=margin;
    if (all(P == clamp(P,0,SS-1))) {
        float4 t = tex[P];
        o.rgb = lerp(o.rgb, ToLinear(t.rgb), t.a);
    }
    return o;
}

//影付き
float4 PutFontSpriteShadow(Texture2D<float4>dest, float4 C, Texture2D<float4>tex, float4 col, int2 P, int2 margin, int halign = 0, int valign = 0)
{
    float4 o = C;

    //影
    for (int y=-4; y<=4; y++) {
        for (int x=-4; x<=4; x++) {
            float w = exp(-(x*x+y*y)/16.0) * 0.1;
            o = PutFontSprite(dest, o, tex, float4(0,0,0,col.a*w), P, margin + int2(x+4,y+4), halign, valign);
        }
    }
    //本体
    o = PutFontSprite(dest, o, tex, col, P, margin, halign, valign);
    return o;
}


float4 PSTelop(VSO vso) : SV_TARGET
{
    uint2 vpos = vso.position.xy;
    float4 s = Screen[vpos];
    s.rgb = ToLinear(s.rgb);
    float4 o = s;

    /*
    o = PutSprite(Screen, o, Hanko, vpos, -16, HALIGN_RIGHT, VALIGN_BOTTOM);
    o = PutFontSpriteShadow(Screen, o, Credit, 1, vpos, uint2(32,-32), HALIGN_LEFT, VALIGN_BOTTOM);
    o = PutFontSpriteShadow(Screen, o, Telop, float4(1,0.5,0.2,1), vpos, uint2(32,32));
    */
    o.rgb = ToGamma(o.rgb);

    return o;
}
