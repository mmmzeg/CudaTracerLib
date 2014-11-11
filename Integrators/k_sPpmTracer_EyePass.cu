#include "k_sPpmTracer.h"
#include "..\Kernel\k_TraceHelper.h"
#include "..\Kernel\k_TraceAlgorithms.h"
/*

//Adaptive Progressive Photon Mapping Implementation
			k_AdaptiveEntry ent = A.E[y * w + x];
			float rSqr = ent.r * ent.r, maxr = MAX(ent.r, ent.rd), rd2 = ent.rd * ent.rd, rd = ent.rd;
			Frame sys = bRec.map.sys;
			sys.t *= maxr / length(sys.t);
			sys.s *= maxr / length(sys.s);
			sys.n *= maxr / length(sys.n);
			float3 ur = normalize(bRec.map.sys.t) * rd, vr = normalize(bRec.map.sys.s) * rd;
			float3 a = -1.0f * sys.t - sys.s, b = sys.t - sys.s, c = -1.0f * sys.t + sys.s, d = sys.t + sys.s;
			float3 low = fminf(fminf(a, b), fminf(c, d)) + p, high = fmaxf(fmaxf(a, b), fmaxf(c, d)) + p;
			uint3 lo = g_Map2.m_sSurfaceMap.m_sHash.Transform(low), hi = g_Map2.m_sSurfaceMap.m_sHash.Transform(high);
			Spectrum Lp = make_float3(0), gamma = dif * PI*PI;//only diffuse
			float I_tmp = 0, psi_tmp = 0, pl_tmp = 0;
			for(int a = lo.x; a <= hi.x; a++)
				for(int b = lo.y; b <= hi.y; b++)
					for(int c = lo.z; c <= hi.z; c++)
					{
						unsigned int i0 = g_Map2.m_sSurfaceMap.m_sHash.Hash(make_uint3(a,b,c)), i = g_Map2.m_sSurfaceMap.m_pDeviceHashGrid[i0];
						while(i != 0xffffffff)
						{
							k_pPpmPhoton e = g_Map2.m_pPhotons[i];
							float3 nor = e.getNormal(), wi = e.getWi(), P = e.Pos;
							Spectrum l = e.getL();
							float dist2 = dot(P - p, P - p);
							if(dot(nor, bRec.map.sys.n) > 0.95f)
							{
								bRec.wo = bRec.map.sys.toLocal(wi);
								float psi = Spectrum(throughput * gamma * l).getLuminance();
								if(dist2 < rd2)
								{
									const float3 e_l = p - P;
									float aa = k_tr(rd, e_l + ur), ab = k_tr(rd, e_l - ur);
									float ba = k_tr(rd, e_l + vr), bb = k_tr(rd, e_l - vr);
									float cc = k_tr(rd, e_l);
									float laplu = psi / rd2 * (aa + ab - 2.0f * cc);
									float laplv = psi / rd2 * (ba + bb - 2.0f * cc);
									I_tmp += laplu + laplv;
								}
								if(dist2 < rSqr)
								{
									float kri = k_tr(ent.r, sqrtf(dist2));
									Lp += kri * gamma * l;
									psi_tmp += kri * psi;
									pl_tmp += kri;
								}
							}
							i = e.next;
						}
					}
#define UPD(tar, val, pow) tar = scale0 * tar + scale1 * powf(val, pow);
			UPD(ent.I, I_tmp / float(g_Map2.m_uPhotonNumEmitted), 1)
			UPD(ent.I2, I_tmp / float(g_Map2.m_uPhotonNumEmitted), 2)
			UPD(ent.psi, psi_tmp / float(g_Map2.m_uPhotonNumEmitted), 1)
			UPD(ent.psi2, psi_tmp / float(g_Map2.m_uPhotonNumEmitted), 2)
			UPD(ent.pl, pl_tmp / float(g_Map2.m_uPhotonNumEmitted), 1)
#undef UPD
			float VAR_Lapl = ent.I2 - ent.I * ent.I;
			float VAR_Phi = ent.psi2 - ent.psi * ent.psi;

			if(VAR_Lapl)
			{
				ent.rd = 1.9635f * sqrtf(VAR_Lapl) * powf(a_PassIndex, -1.0f / 8.0f);
				ent.rd = clamp(ent.rd, A.r_min, A.r_max);
			}

			float k_2 = 10.0f * PI / 168.0f, k_22 = k_2 * k_2;
			float ta = (2.0f * sqrtf(VAR_Phi)) / (PI * float(g_Map2.m_uPhotonNumEmitted) * ent.pl * k_22 * ent.I * ent.I);

			if(VAR_Lapl && VAR_Phi)
			{
				ent.r = powf(ta, 1.0f / 6.0f) * powf(a_PassIndex, -1.0f / 6.0f);
				ent.r = clamp(ent.r, A.r_min, A.r_max);
			}
			A.E[y * w + x] = ent;

			L += throughput * Lp / float(g_Map2.m_uPhotonNumEmitted) * dif;

*/

CUDA_DEVICE k_PhotonMapCollection g_Map2;

CUDA_FUNC_IN float k(float t)
{
	//float t2 = t * t;
	//return 1.0f + t2 * t * (-6.0f * t2 + 15.0f * t - 10.0f);
	return 1.0f + t * t * t * (-6.0f * t * t + 15.0f * t - 10.0f);
}

CUDA_FUNC_IN float k_tr(float r , float t)
{
	//return k(t / r) / (PI * r * r);
	float a = 1.0f - t * t / (r * r);
	return a * a * 3.0f / (r * r * PI);
}

CUDA_FUNC_IN float k_tr(float r, const float3& t)
{
	return k_tr(r, length(t));
}

template<typename HASH> template<bool VOL> CUDA_ONLY_FUNC Spectrum k_PhotonMap<HASH>::L_Volume(float a_r, float a_NumPhotonEmitted, CudaRNG& rng, const Ray& r, float tmin, float tmax, const Spectrum& sigt) const
{
	float Vs = 1.0f / ((4.0f / 3.0f) * PI * a_r * a_r * a_r * a_NumPhotonEmitted), r2 = a_r * a_r;
	Spectrum L_n = Spectrum(0.0f);
	float a,b;
	if(!m_sHash.getAABB().Intersect(r, &a, &b))
		return L_n;//that would be dumb
	a = clamp(a, tmin, tmax);
	b = clamp(b, tmin, tmax);
	float d = 2.0f * a_r;
	while(b > a)
	{
		Spectrum L = Spectrum(0.0f);
		float3 x = r(b);
		uint3 lo = m_sHash.Transform(x - make_float3(a_r)), hi = m_sHash.Transform(x + make_float3(a_r));
		for(unsigned int ac = lo.x; ac <= hi.x; ac++)
			for(unsigned int bc = lo.y; bc <= hi.y; bc++)
				for(unsigned int cc = lo.z; cc <= hi.z; cc++)
				{
					unsigned int i0 = m_sHash.Hash(make_uint3(ac,bc,cc)), i = m_pDeviceHashGrid[i0];
					while(i != 0xffffffff)
					{
						k_pPpmPhoton e = m_pDevicePhotons[i];
						float3 wi = e.getWi(), P = e.Pos;
						Spectrum l = e.getL();
						if(dot(P - x, P - x) < r2)
						{
							float p = VOL ? g_SceneData.m_sVolume.p(x, r.direction, wi, rng) : 1.f / (4.f * PI);
							L += p * l * Vs;
						}
						i = e.next;
					}
				}
		if(VOL)
			L_n = L * d + L_n * (-g_SceneData.m_sVolume.tau(r, b - d, b)).exp() + g_SceneData.m_sVolume.Lve(x, -1.0f * r.direction) * d;
		else L_n = L * d + L_n * (sigt * -d).exp();
		b -= d;
	}
	return L_n;
}

CUDA_ONLY_FUNC Spectrum L_Surface(BSDFSamplingRecord& bRec, float a_rSurfaceUNUSED, const float3& p, const float3& wo, const e_KernelMaterial* mat)
{
	Frame sys = bRec.map.sys;
	sys.t *= a_rSurfaceUNUSED;
	sys.s *= a_rSurfaceUNUSED;
	sys.n *= a_rSurfaceUNUSED;
	float3 a = -1.0f * sys.t - sys.s, b = sys.t - sys.s, c = -1.0f * sys.t + sys.s, d = sys.t + sys.s;
	float3 low = fminf(fminf(a, b), fminf(c, d)) + p, high = fmaxf(fmaxf(a, b), fmaxf(c, d)) + p;
	Spectrum Lp = Spectrum(0.0f);
	uint3 lo = g_Map2.m_sSurfaceMap.m_sHash.Transform(low), hi = g_Map2.m_sSurfaceMap.m_sHash.Transform(high);
	for(unsigned int a = lo.x; a <= hi.x; a++)
		for(unsigned int b = lo.y; b <= hi.y; b++)
			for(unsigned int c = lo.z; c <= hi.z; c++)
			{
				unsigned int i0 = g_Map2.m_sSurfaceMap.m_sHash.Hash(make_uint3(a,b,c)), i = g_Map2.m_sSurfaceMap.m_pDeviceHashGrid[i0];
				while(i != 0xffffffff)
				{
					k_pPpmPhoton e = g_Map2.m_sSurfaceMap.m_pDevicePhotons[i];
					float3 n = e.getNormal(), wi = e.getWi(), P = e.Pos;
					Spectrum l = e.getL();
					float dist2 = dot(P - p, P - p);
					if(dist2 < a_rSurfaceUNUSED * a_rSurfaceUNUSED )//&& AbsDot(n, bRec.map.sys.n) > 0.95f
					{
						float ke = k_tr(a_rSurfaceUNUSED, sqrtf(dist2));
						Lp += ke * l;
					}
					i = e.next;
				}
			}
	return Lp / float(g_Map2.m_uPhotonNumEmitted);
}

CUDA_ONLY_FUNC Spectrum L_FinalGathering(TraceResult& r2, BSDFSamplingRecord& bRec, CudaRNG& rng, float a_rSurfaceUNUSED)
{
	Spectrum L(0.0f);
	const int N = 3;
	for (int i = 0; i < N; i++)
	{
		Spectrum f = r2.getMat().bsdf.sample(bRec, rng.randomFloat2());
		Ray r(bRec.map.P, bRec.getOutgoing());
		TraceResult r3 = k_TraceRay(r);
		if (r3.hasHit())
		{
			BSDFSamplingRecord bRec2;
			r3.getBsdfSample(r, rng, &bRec2);
			Spectrum dif = r3.getMat().bsdf.getDiffuseReflectance(bRec2) / PI;
			L += f * L_Surface(bRec, a_rSurfaceUNUSED, bRec2.map.P, -r.direction, &r3.getMat()) * dif;
		}
	}
	return L / float(N);
}

template<bool DIRECT, bool DEBUGKERNEL> CUDA_ONLY_FUNC void k_EyePassF(int x, int y, int w, int h, float a_PassIndex, float a_rSurfaceUNUSED, float a_rVolume, k_AdaptiveStruct A, float scale0, float scale1, e_Image g_Image)
{
	CudaRNG rng = g_RNGData();
	BSDFSamplingRecord bRec;
	Ray r;
	Spectrum importance = g_SceneData.sampleSensorRay(r, make_float2(x, y) + rng.randomFloat2() - make_float2(0.5f), rng.randomFloat2());
	TraceResult r2;
	r2.Init();
	int depth = -1;
	Spectrum L(0.0f), throughput(1.0f);
	while(k_TraceRay(r.direction, r.origin, &r2) && depth++ < 5)
	{
		float3 p = r(r2.m_fDist);
		r2.getBsdfSample(r, rng, &bRec);
		if(g_SceneData.m_sVolume.HasVolumes())
		{
			float tmin, tmax;
			g_SceneData.m_sVolume.IntersectP(r, 0, r2.m_fDist, &tmin, &tmax);
			L += throughput * g_Map2.L<true>(a_rVolume, rng, r, tmin, tmax, make_float3(0));
			throughput = throughput * (-g_SceneData.m_sVolume.tau(r, tmin, tmax)).exp();
		}
		if(DIRECT)
			L += throughput * UniformSampleAllLights(bRec, r2.getMat(), 1);
		L += throughput * r2.Le(p, bRec.map.sys, -r.direction);//either it's the first bounce -> account or it's a specular reflection -> ...
		const e_KernelBSSRDF* bssrdf;
		if(r2.getMat().GetBSSRDF(bRec.map, &bssrdf))
		{
			r = BSSRDF_Entry(bssrdf, bRec.map.sys, p, r.direction);
			TraceResult r3 = k_TraceRay(r);
			L += throughput * g_Map2.L<false>(a_rVolume, rng, r, 0, r3.m_fDist, bssrdf->sigp_s + bssrdf->sig_a);
			//normally one would go to the other side but due to photon mapping the path is terminated
			break;
			//Frame sys;
			//r3.lerpFrame(sys);
			//r = BSSRDF_Exit(bssrdf, sys, r(r3), r.direction);
			//r2.Init();
		}
		bool hasDiffuse = r2.getMat().bsdf.hasComponent(EDiffuse), hasSpecGlossy = r2.getMat().bsdf.hasComponent(EDelta | EGlossy);
		if(hasDiffuse && !DEBUGKERNEL)
		{
			Spectrum dif = r2.getMat().bsdf.getDiffuseReflectance(bRec) / PI;
			L += throughput * L_Surface(bRec, a_rSurfaceUNUSED, p, -r.direction, &r2.getMat()) * dif;
			//L += throughput * L_FinalGathering(r2, bRec, rng, a_rSurfaceUNUSED);
			if(!hasSpecGlossy)
				break;
		}
		if(hasSpecGlossy)
		{
			bRec.sampledType = 0;
			bRec.typeMask = EDelta | EGlossy;
			Spectrum t_f = r2.getMat().bsdf.sample(bRec, rng.randomFloat2());
			if(!bRec.sampledType)
				break;
			throughput = throughput * t_f;
			r = Ray(p, bRec.getOutgoing());
			r2.Init();
		}
		//else break;
	}
	if(!r2.hasHit())
	{
		if(g_SceneData.m_sVolume.HasVolumes())
		{
			float tmin, tmax;
			g_SceneData.m_sVolume.IntersectP(r, 0, r2.m_fDist, &tmin, &tmax);
			L += throughput * g_Map2.L<true>(a_rVolume, rng, r, tmin, tmax, make_float3(0));
		}
		L += throughput * g_SceneData.EvalEnvironment(r);
	}
	g_Image.AddSample(x, y, importance * L);
	//Spectrum qs;
	//float t = A.E[y * w + x].r / a_rSurfaceUNUSED;
	//t = (A.E[y * w + x].r - A.r_min) / (A.r_max - A.r_min);
	//qs.fromHSL(1.0f / 3.0f - t / 3.0f, 1, 0.5f);
	//g_Image.SetSample(x, y, qs.toRGBCOL());
	g_RNGData(rng);
}

template<bool DIRECT, bool DEBUGKERNEL> __global__ void k_EyePass(int2 off, int w, int h, float a_PassIndex, float a_rSurfaceUNUSED, float a_rVolume, k_AdaptiveStruct A, float scale0, float scale1, e_Image g_Image)
{
	int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
	x += off.x;
	y += off.y;
	if(x < w && y < h)
		k_EyePassF<DIRECT, DEBUGKERNEL>(x, y, w, h, a_PassIndex, a_rSurfaceUNUSED, a_rVolume, A, scale0, scale1, g_Image);
}

#define TN(r) (r * powf(float(m_uPassesDone), -1.0f/6.0f))
void k_sPpmTracer::doEyePass(e_Image* I)
{
	k_AdaptiveStruct A(TN(r_min), TN(r_max), m_pEntries);
	cudaMemcpyToSymbol(g_Map2, &m_sMaps, sizeof(k_PhotonMapCollection));
	k_INITIALIZE(m_pScene, g_sRngs);
	float s1 = float(m_uPassesDone - 1) / float(m_uPassesDone), s2 = 1.0f / float(m_uPassesDone);
	if(m_pScene->getVolumes().getLength() || m_bLongRunning || w * h > 800 * 800)
	{
		unsigned int q = 8, p = 16, pq = p * q;
		int nx = w / pq + 1, ny = h / pq + 1;
		for(int i = 0; i < nx; i++)
			for(int j = 0; j < ny; j++)
				if(m_bDirect)
					k_EyePass<true, false><<<dim3( q, q, 1), dim3(p, p, 1)>>>(make_int2(pq * i, pq * j), w, h, m_uPassesDone, getCurrentRadius(2), getCurrentRadius(3), A, s1,s2, *I);
				else k_EyePass<false, false><<<dim3( q, q, 1), dim3(p, p, 1)>>>(make_int2(pq * i, pq * j), w, h, m_uPassesDone, getCurrentRadius(2), getCurrentRadius(3), A, s1,s2, *I);
	}
	else
	{
		const unsigned int p = 16;
		if(m_bDirect)
			k_EyePass<true, false><<<dim3( w / p + 1, h / p + 1, 1), dim3(p, p, 1)>>>(make_int2(0,0), w, h, m_uPassesDone, getCurrentRadius(2), getCurrentRadius(3), A, s1,s2, *I);
		else k_EyePass<false, false><<<dim3( w / p + 1, h / p + 1, 1), dim3(p, p, 1)>>>(make_int2(0,0), w, h, m_uPassesDone, getCurrentRadius(2), getCurrentRadius(3), A, s1,s2, *I);
	}
}

void k_sPpmTracer::Debug(int2 pixel)
{
	if(m_uPhotonsEmitted == (unsigned long long)-1)
		return;
	k_AdaptiveStruct A(TN(r_min), TN(r_max), m_pEntries);
	cudaMemcpyToSymbol(g_Map2, &m_sMaps, sizeof(k_PhotonMapCollection));
	k_INITIALIZE(m_pScene, g_sRngs);
	//k_EyePassF<false, true>(pixel.x, pixel.y, w, h, m_uPassesDone, getCurrentRadius(2), getCurrentRadius(3), A, 1,1, e_Image());
}

__global__ void k_StartPass(int w, int h, float r, float rd, k_AdaptiveEntry* E)
{
	int i = threadId, x = i % w, y = i / w;
	if(x < w && y < h)
	{
		E[i].r = r;
		E[i].rd = rd;
		E[i].psi = E[i].psi2 = E[i].I = E[i].I2 = E[i].pl = 0.0f;
	}
}

void k_sPpmTracer::doStartPass(float r, float rd)
{
	int p = 32;
	k_StartPass<<<dim3(w / p + 1, h / p + 1, 1), dim3(p,p,1)>>>(w, h, r, rd, m_pEntries);
}