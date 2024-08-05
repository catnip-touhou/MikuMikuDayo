
struct MNEEVertex
{
    float3 p;           // Vertex position
    float3 dpdu, dpdv; // Tangent vectors
    float3 n;          // Normal vector
    float3 dndu, dndv; // Normal derivatives
    float eta;         // Relative index of refraction
    float2x2 A, B, C; // Matrix blocks of ∇C in the row
    // associated with the current vertex
};


/*[Jakob 2013]より引用
    オリジナルと違い、hlslの配列は-1スタートできないので配列の要素を0,1,2と書き換えてある
    v[1]のABCをv[0]～v[2]に入っているABC以外のパラメータから算出する
    v[1]でのc = c1についての微分がA,B,Cに入り
    Aは∂c1/∂v[0], Bは∂c1/∂v[1], Cは∂c1/∂v[2]という事になる
    
    A = ∂c1/∂v[0] : v[0]を1動かすとc1はいくつ動くか？
    c自体はfloat2で、v[0].u,v[0].vをそれぞれ動かしたときの変位が要るので2x2行列になる
    v[0]はu,vで幾つ動かすか？という値であってワールド座標そのままではない?
*/

void ComputeDerivatives(inout MNEEVertex v[3])
{
    /* Compute relevant directions and a few useful projections */
    float3 wi = v[0].p - v[1].p;
    float3 wo = v[2].p - v[1].p;
    float ili = 1 / length(wi);
    float ilo = 1 / length(wo);
    wi *= ili;
    wo *= ilo;
    float3 H = wi + v[1].eta * wo;
    float ilh = 1 / length(H);
    H *= ilh;
    float dot_H_n = dot(v[1].n, H),
          dot_H_dndu = dot(v[1].dndu, H),
          dot_H_dndv = dot(v[1].dndv, H),
          dot_u_n = dot(v[1].dpdu, v[1].n),
          dot_v_n = dot(v[1].dpdv, v[1].n);
    /* Local shading tangent frame */
    float3 s = v[1].dpdu - dot_u_n * v[1].n;
    float3 t = v[1].dpdv - dot_v_n * v[1].n;
    ilo *= v[1].eta * ilh;
    ili *= ilh;
    /* Derivatives of C with respect to x_{i-1} */
    float3
        dH_du = (v[0].dpdu - wi * dot(wi, v[0].dpdu)) * ili,
        dH_dv = (v[0].dpdv - wi * dot(wi, v[0].dpdv)) * ili;
    dH_du -= H * dot(dH_du, H);
    dH_dv -= H * dot(dH_dv, H);
    v[1].A = float2x2(
        dot(dH_du, s), dot(dH_dv, s),
        dot(dH_du, t), dot(dH_dv, t));
    /* Derivatives of C with respect to x_i */
    dH_du = -v[1].dpdu * (ili + ilo) + wi * (dot(wi, v[1].dpdu) * ili) + wo * (dot(wo, v[1].dpdu) * ilo);
    dH_dv = -v[1].dpdv * (ili + ilo) + wi * (dot(wi, v[1].dpdv) * ili) + wo * (dot(wo, v[1].dpdv) * ilo);
    dH_du -= H * dot(dH_du, H);
    dH_dv -= H * dot(dH_dv, H);
    v[1].B = float2x2(
        dot(dH_du, s) - dot(v[1].dpdu, v[1].dndu) * dot_H_n - dot_u_n * dot_H_dndu,
        dot(dH_dv, s) - dot(v[1].dpdu, v[1].dndv) * dot_H_n - dot_u_n * dot_H_dndv,
        dot(dH_du, t) - dot(v[1].dpdv, v[1].dndu) * dot_H_n - dot_v_n * dot_H_dndu,
        dot(dH_dv, t) - dot(v[1].dpdv, v[1].dndv) * dot_H_n - dot_v_n * dot_H_dndv);
    /* Derivatives of C with respect to x_{i+1} */
    dH_du = (v[2].dpdu - wo * dot(wo, v[2].dpdu)) * ilo;
    dH_dv = (v[2].dpdv - wo * dot(wo, v[2].dpdv)) * ilo;
    dH_du -= H * dot(dH_du, H);
    dH_dv -= H * dot(dH_dv, H);
    v[1].C = float2x2(
        dot(dH_du, s), dot(dH_dv, s),
        dot(dH_du, t), dot(dH_dv, t));
}
