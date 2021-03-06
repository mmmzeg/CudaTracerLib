#pragma once

#include <Kernel/TraceAlgorithms.h>
#include <Engine/Light.h>
#include "../PhotonMapHelper.h"
#include <Engine/SpatialGrid.h>

namespace CudaTracerLib {

struct k_MISPhoton : public PPPMPhoton
{
	float dVC, dVCM, dVM;
	CUDA_FUNC_IN k_MISPhoton(){}
	CUDA_FUNC_IN k_MISPhoton(const Spectrum& l, const NormalizedT<Vec3f>& wi, const NormalizedT<Vec3f>& n, PhotonType type, float dvc, float dvcm, float dvm)
		: PPPMPhoton(l, wi, n, type), dVC(dvc), dVCM(dvcm), dVM(dvm)
	{
	}
};

#define NUM_V_PER_PATH 5
#define MAX_SUB_PATH_LENGTH 10

CUDA_FUNC_IN float Mis(float pdf)
{
	return pdf;
}

CUDA_FUNC_IN float PdfWtoA(const float aPdfW, const float aDist2, const float aCosThere)
{
	return aPdfW * std::abs(aCosThere) / aDist2;
}

CUDA_FUNC_IN float PdfAtoW(const float aPdfA, const float aDist2, const float aCosThere)
{
	return aPdfA * aDist2 / std::abs(aCosThere);
}

CUDA_FUNC_IN float pdf(const Material& mat, BSDFSamplingRecord& bRec)
{
	bRec.typeMask = EAll;
	if (mat.bsdf.hasComponent(EDelta))
		return mat.bsdf.pdf(bRec, EDiscrete);
	return mat.bsdf.pdf(bRec) / math::abs(Frame::cosTheta(bRec.wo));
}

CUDA_FUNC_IN float revPdf(const Material& mat, BSDFSamplingRecord& bRec)
{
	bRec.typeMask = EAll;
	if (mat.bsdf.hasComponent(EDelta))
		return mat.bsdf.pdf(bRec, EDiscrete);
	swapk(bRec.wo, bRec.wi);
	float pdf = mat.bsdf.pdf(bRec) / math::abs(Frame::cosTheta(bRec.wo));
	swapk(bRec.wo, bRec.wi);
	return pdf;
}

struct BPTSubPathState
{
	NormalizedT<Ray> r;
	Spectrum throughput;
	bool delta;

	float dVCM;
	float dVC;
	float dVM;
};

struct BPTVertex
{
	const Material* mat;
	DifferentialGeometry dg;
	BSDFSamplingRecord bRec;
	Spectrum throughput;
	int subPathLength;

	float dVCM;
	float dVC;
	float dVM;

	CUDA_FUNC_IN BPTVertex()
		: bRec(dg)
	{

	}

	CUDA_FUNC_IN BPTVertex& operator=(const BPTVertex& other)
	{
		mat = other.mat;
		throughput = other.throughput;
		subPathLength = other.subPathLength;
		dVCM = other.dVCM;
		dVC = other.dVC;
		dVM = other.dVM;

		dg = other.dg;
		bRec.eta = other.bRec.eta;
		bRec.mode = other.bRec.mode;
		bRec.sampledType = other.bRec.sampledType;
		bRec.typeMask = other.bRec.typeMask;
		bRec.wi = other.bRec.wi;
		bRec.wo = other.bRec.wo;

		return *this;
	}
};

CUDA_FUNC_IN void sampleEmitter(BPTSubPathState& v, Sampler& rng, float mMisVcWeightFactor)
{
	PositionSamplingRecord pRec;
	DirectionSamplingRecord dRec;
	Spectrum Le = g_SceneData.sampleEmitterPosition(pRec, rng.randomFloat2());
	const Light* l = (const Light*)pRec.object;
	Le *= l->sampleDirection(dRec, pRec, rng.randomFloat2());
	float emitterPdf = g_SceneData.pdfEmitterDiscrete(l);

	v.delta = 0;
	v.throughput = Le;
	v.r = NormalizedT<Ray>(pRec.p, dRec.d);

	DirectSamplingRecord directRec;
	directRec.d = directRec.refN = NormalizedT<Vec3f>(1.0f, 0.0f, 0.0f);
	directRec.n = NormalizedT<Vec3f>(-1.0f, 0.0f, 0.0f);
	directRec.measure = EArea;

	float directPdfW = l->pdfDirect(directRec) * emitterPdf;
	float emissionPdfW = pRec.pdf * dRec.pdf;

	v.dVCM = Mis(directPdfW / emissionPdfW);
	if (!l->As()->IsDegenerate())
	{
		float usedCosLight = dot(pRec.n, dRec.d);
		v.dVC = Mis(usedCosLight / emissionPdfW);
	}
	else v.dVC = 0;
	v.dVM = v.dVC * mMisVcWeightFactor;
}

CUDA_FUNC_IN void sampleCamera(BPTSubPathState& v, Sampler& rng, const Vec2f& pixelPosition, float mLightSubPathCount)
{
	PositionSamplingRecord pRec;
	DirectionSamplingRecord dRec;

	Spectrum imp = g_SceneData.m_Camera.samplePosition(pRec, rng.randomFloat2(), &pixelPosition);
	imp *= g_SceneData.m_Camera.sampleDirection(dRec, pRec, rng.randomFloat2(), &pixelPosition);

	float cameraPdfW = pRec.pdf * dRec.pdf;

	v.r = NormalizedT<Ray>(pRec.p, dRec.d);
	v.throughput = imp;
	v.delta = 1;
	v.dVCM = Mis(mLightSubPathCount / cameraPdfW);
	v.dVC = 0;
	v.dVM = 0;

}

CUDA_FUNC_IN bool sampleScattering(BPTSubPathState& v, BSDFSamplingRecord& bRec, const Material& mat, Sampler& rng, float mMisVcWeightFactor, float mMisVmWeightFactor)
{
	float bsdfDirPdfW;
	Spectrum f = mat.bsdf.sample(bRec, bsdfDirPdfW, rng.randomFloat2());
	float cosThetaOut = math::abs(Frame::cosTheta(bRec.wo));
	float bsdfRevPdfW = bsdfDirPdfW;
	if ((bRec.sampledType & EDelta) == 0)
	{
		bsdfDirPdfW /= cosThetaOut;
		bsdfRevPdfW = revPdf(mat, bRec);
	}

	if (bRec.sampledType & EDelta)
	{
		v.dVCM = 0.f;
		v.dVC *= Mis(cosThetaOut);
		v.dVM *= Mis(cosThetaOut);
		v.delta &= 1;
	}
	else
	{
		v.dVC = Mis(cosThetaOut / bsdfDirPdfW) * (
			v.dVC * Mis(bsdfRevPdfW) +
			v.dVCM + mMisVmWeightFactor);

		v.dVM = Mis(cosThetaOut / bsdfDirPdfW) * (
			v.dVM * Mis(bsdfRevPdfW) +
			v.dVCM * mMisVcWeightFactor + 1.f);

		v.dVCM = Mis(1.f / bsdfDirPdfW);

		v.delta &= 0;
	}

	v.r = NormalizedT<Ray>(bRec.dg.P, bRec.getOutgoing());
	v.throughput *= f;
	return true;
}

CUDA_FUNC_IN Spectrum gatherLight(const BPTSubPathState& cameraState, BSDFSamplingRecord& bRec, const TraceResult& r2, Sampler& rng, int subPathLength, bool use_mis)
{
	const Light* l = g_SceneData.getLight(r2);
	float pdfLight = g_SceneData.pdfEmitterDiscrete(l);
	PositionSamplingRecord pRec(bRec.dg.P, bRec.dg.sys.n, 0);
	float directPdfA = l->pdfPosition(pRec);
	DirectionSamplingRecord dRec(-cameraState.r.dir());
	float emissionPdfW = l->pdfDirection(dRec, pRec) * directPdfA;
	Spectrum L = l->eval(bRec.dg.P, bRec.dg.sys, -cameraState.r.dir());

	if (L.isZero())
		return Spectrum(0.0f);

	if (subPathLength == 1)
		return L;

	directPdfA *= pdfLight;
	emissionPdfW *= pdfLight;

	const float wCamera = Mis(directPdfA) * cameraState.dVCM +
		Mis(emissionPdfW) * cameraState.dVC;

	const float misWeight = use_mis ? 1.f / (1.f + wCamera) : 1;

	return misWeight * L;
}

template<bool TEST_VISIBILITY> CUDA_FUNC_IN bool V(const Vec3f& a, const Vec3f& b, TraceResult* res = 0)
{
	if (!TEST_VISIBILITY)
		return true;
	else return CudaTracerLib::V(a, b, res);
}

template<bool TEST_VISIBILITY = true> CUDA_FUNC_IN void connectToCamera(const BPTSubPathState& lightState, BSDFSamplingRecord& bRec, const Material& mat, Image& g_Image, Sampler& rng, float mLightSubPathCount, float mMisVmWeightFactor, float scaleLight, bool use_mis)
{
	DirectSamplingRecord dRec(bRec.dg.P, bRec.dg.sys.n);
	Spectrum directFactor = g_SceneData.m_Camera.sampleDirect(dRec, rng.randomFloat2());
	bRec.wo = bRec.dg.toLocal(normalize((Vec3f)dRec.d));
	Spectrum bsdfFactor = mat.bsdf.f(bRec);
	float bsdfRevPdfW = revPdf(mat, bRec);
	float cameraPdfA = dRec.pdf;
	if (dRec.measure != EArea)
		cameraPdfA = PdfWtoA(cameraPdfA, distanceSquared(bRec.dg.P, dRec.p), dot(dRec.n, -dRec.d));
	const float wLight = Mis(cameraPdfA / mLightSubPathCount) * (
		mMisVmWeightFactor + lightState.dVCM + lightState.dVC * Mis(bsdfRevPdfW));
	float miWeight = use_mis ? 1.f / (wLight + 1.f) : 1;
	Spectrum contrib = miWeight * lightState.throughput * bsdfFactor * directFactor / mLightSubPathCount;
	if (!contrib.isZero() && V<TEST_VISIBILITY>(dRec.p, dRec.ref))
		g_Image.Splat(dRec.uv.x, dRec.uv.y, contrib * scaleLight);
}

template<bool TEST_VISIBILITY = true> CUDA_FUNC_IN Spectrum connectToLight(const BPTSubPathState& cameraState, BSDFSamplingRecord& bRec, const Material& mat, Sampler& rng, float mMisVmWeightFactor, bool use_mis)
{
	DirectSamplingRecord dRec(bRec.dg.P, bRec.dg.sys.n);
	const Spectrum directFactor = g_SceneData.sampleEmitterDirect(dRec, rng.randomFloat2());
	const Light* l = (const Light*)dRec.object;
	if (!l)
		return Spectrum(0.0f);
	float pdfLight = g_SceneData.pdfEmitterDiscrete(l);
	float directPdfW = dRec.pdf;
	DirectionSamplingRecord dirRec(-dRec.d);
	const float emissionPdfW = l->pdfPosition(dRec) * l->pdfDirection(dirRec, dRec) * pdfLight;
	const float cosAtLight = dot(dRec.n, -dRec.d);

	bRec.wo = bRec.dg.toLocal(dRec.d);
	const float cosToLight = math::abs(Frame::cosTheta(bRec.wo));
	const Spectrum bsdfFactor = mat.bsdf.f(bRec);
	const float bsdfDirPdfW = pdf(mat, bRec);
	const float bsdfRevPdfW = revPdf(mat, bRec);

	if (directFactor.isZero() || bsdfFactor.isZero())
		return Spectrum(0.0f);

	if (dRec.measure != ESolidAngle)
		directPdfW = PdfAtoW(directPdfW, distanceSquared(bRec.dg.P, dRec.p), cosAtLight);

	const float wLight = Mis(bsdfDirPdfW / directPdfW);
	const float wCamera = Mis(emissionPdfW * cosToLight / (directPdfW * cosAtLight)) * (
		mMisVmWeightFactor + cameraState.dVCM + cameraState.dVC * Mis(bsdfRevPdfW));

	const float misWeight = use_mis ? 1.f / (wLight + 1.f + wCamera) : 1;

	Spectrum contrib = misWeight * directFactor * bsdfFactor;

	if (!contrib.isZero() && V<TEST_VISIBILITY>(bRec.dg.P, dRec.p))
		return contrib;
	return Spectrum(0.0f);
}

template<bool TEST_VISIBILITY = true> CUDA_FUNC_IN Spectrum connectVertices(BPTVertex& emitterVertex, const BPTSubPathState& cameraState, BSDFSamplingRecord& bRec, const Material& mat, float mMisVcWeightFactor, float mMisVmWeightFactor, bool use_mis)
{
	const float dist2 = distanceSquared(emitterVertex.dg.P, bRec.dg.P);
	auto direction = normalize(emitterVertex.dg.P - bRec.dg.P);

	bRec.wo = bRec.dg.toLocal(direction);
	const Spectrum cameraBsdf = mat.bsdf.f(bRec);
	const float cosCamera = math::abs(Frame::cosTheta(bRec.wo));
	const float cameraBsdfDirPdfW = pdf(mat, bRec);
	const float cameraBsdfRevPdfW = revPdf(mat, bRec);

	emitterVertex.bRec.wo = emitterVertex.bRec.dg.toLocal(-direction);
	const Spectrum emitterBsdf = emitterVertex.mat->bsdf.f(emitterVertex.bRec);
	const float cosLight = math::abs(Frame::cosTheta(emitterVertex.bRec.wo));
	const float lightBsdfDirPdfW = pdf(*emitterVertex.mat, emitterVertex.bRec);
	const float lightBsdfRevPdfW = revPdf(*emitterVertex.mat, emitterVertex.bRec);

	if (cameraBsdf.isZero() || emitterBsdf.isZero())
		return Spectrum(0.0f);

	const float cameraBsdfDirPdfA = PdfWtoA(cameraBsdfDirPdfW, dist2, cosLight);
	const float lightBsdfDirPdfA = PdfWtoA(lightBsdfDirPdfW, dist2, cosCamera);

	const float wLight = Mis(cameraBsdfDirPdfA) * (
		mMisVmWeightFactor + emitterVertex.dVCM + emitterVertex.dVC * Mis(lightBsdfRevPdfW));

	// Partial eye sub-path MIS weight [tech. rep. (41)]
	const float wCamera = Mis(lightBsdfDirPdfA) * (
		mMisVmWeightFactor + cameraState.dVCM + cameraState.dVC * Mis(cameraBsdfRevPdfW));

	// Full path MIS weight [tech. rep. (37)]
	const float misWeight = use_mis ? 1.f / (wLight + 1.f + wCamera) : 1;

	const float geometryTerm = 1.0f / dist2;
	const Spectrum contrib = (misWeight * geometryTerm) * cameraBsdf * emitterBsdf;
	if (!contrib.isZero() && V<TEST_VISIBILITY>(bRec.dg.P, emitterVertex.dg.P))
		return contrib;
	return Spectrum(0.0f);
}

typedef SpatialLinkedMap<k_MISPhoton> VCMSurfMap;

template<bool F_IS_GLOSSY> CUDA_FUNC_IN Spectrum L_Surface2(VCMSurfMap& g_CurrentMap, BPTSubPathState& aCameraState, BSDFSamplingRecord& bRec, float r, const Material* mat, float mMisVcWeightFactor, float nPhotons, bool use_mis)
{
	Spectrum Lp = Spectrum(0.0f);
	Vec3f a = r*(-bRec.dg.sys.t - bRec.dg.sys.s) + bRec.dg.P, b = r*(bRec.dg.sys.t - bRec.dg.sys.s) + bRec.dg.P, c = r*(-bRec.dg.sys.t + bRec.dg.sys.s) + bRec.dg.P, d = r*(bRec.dg.sys.t + bRec.dg.sys.s) + bRec.dg.P;
#ifdef ISCUDA
	g_CurrentMap.ForAll<200>(min(a, b, c, d), max(a, b, c, d), [&](const Vec3u& cell_idx, unsigned int p_idx, const k_MISPhoton& ph)
	{
		float dist2 = distanceSquared(ph.getPos(g_CurrentMap.getHashGrid(), cell_idx), bRec.dg.P);
		Vec3f photonNormal = ph.getNormal();
		float wiDotGeoN = absdot(photonNormal, -aCameraState.r.dir());
		if (dist2 < r * r && dot(photonNormal, bRec.dg.sys.n) > 0.1f && wiDotGeoN > 1e-2f)
		{
			bRec.wo = bRec.dg.toLocal(ph.getWi());
			float cor_fac = math::abs(Frame::cosTheta(bRec.wi) / (wiDotGeoN * Frame::cosTheta(bRec.wo)));
			float ke = k_tr<2>(r, math::sqrt(dist2));
			Spectrum l = ph.getL();
			if (F_IS_GLOSSY)
				l *= mat->bsdf.f(bRec);

			const float cameraBsdfDirPdfW = pdf(*mat, bRec);
			const float cameraBsdfRevPdfW = revPdf(*mat, bRec);
			const float wLight = ph.dVCM * mMisVcWeightFactor + ph.dVM * cameraBsdfDirPdfW;
			const float wCamera = aCameraState.dVCM * mMisVcWeightFactor + aCameraState.dVM * cameraBsdfRevPdfW;
			const float misWeight = 1.f / (wLight + 1.f + wCamera);

			Lp += (use_mis ? misWeight : 1.0f) * ke * l / Frame::cosTheta(bRec.wo);//cor_fac;
		}
	});
	if (!F_IS_GLOSSY)
		Lp *= mat->bsdf.f(bRec) / Frame::cosTheta(bRec.wo);
	return Lp / nPhotons;
#else
	return 1.0f;
#endif

	/*Spectrum Lp = Spectrum(0.0f);
	const float r2 = a_rSurfaceUNUSED * a_rSurfaceUNUSED;
	Frame sys = bRec.dg.sys;
	sys.t *= a_rSurfaceUNUSED;
	sys.s *= a_rSurfaceUNUSED;
	sys.n *= a_rSurfaceUNUSED;
	Vec3f a = -1.0f * sys.t - sys.s, b = sys.t - sys.s, c = -1.0f * sys.t + sys.s, d = sys.t + sys.s;
	Vec3f low = min(min(a, b), min(c, d)) + bRec.dg.P, high = max(max(a, b), max(c, d)) + bRec.dg.P;
	uint3 lo = map.m_sSurfaceMap.m_sHash.Transform(low), hi = map.m_sSurfaceMap.m_sHash.Transform(high);
	for (unsigned int a = lo.x; a <= hi.x; a++)
	for (unsigned int b = lo.y; b <= hi.y; b++)
	for (unsigned int c = lo.z; c <= hi.z; c++)
	{
	unsigned int i0 = map.m_sSurfaceMap.m_sHash.Hash(Vec3u(a, b, c)), i = map.m_sSurfaceMap.m_pDeviceHashGrid[i0], count = 0;
	while (i != UINT_MAX && i != 0xffffff && count++ < 100)
	{
	k_MISPhoton e = map.m_pPhotons[i];
	Vec3f n = e.getNormal(), wi = e.getWi(), P = e.getPos(map.m_sSurfaceMap.m_sHash, Vec3u(a, b, c));
	Spectrum l = e.getL();
	float dist2 = distanceSquared(P, bRec.dg.P);
	if (dist2 < r2 && dot(n, bRec.dg.sys.n) > 0.8f)
	{
	bRec.wo = bRec.dg.toLocal(wi);
	const float cameraBsdfDirPdfW = pdf(*mat, bRec);
	Spectrum bsdfFactor = mat->bsdf.f(bRec);
	const float cameraBsdfRevPdfW = revPdf(*mat, bRec);
	const float wLight = e.dVCM * mMisVcWeightFactor + e.dVM * cameraBsdfDirPdfW;
	const float wCamera = aCameraState.dVCM * mMisVcWeightFactor + aCameraState.dVM * cameraBsdfRevPdfW;
	const float misWeight = 1.f / (wLight + 1.f + wCamera);

	float ke = k_tr(a_rSurfaceUNUSED, math::sqrt(dist2));
	Lp += (use_mis ? misWeight : 1.0f) * ke * l * bsdfFactor / Frame::cosTheta(bRec.wo);
	}
	i = e.getNext();
	}
	}
	return Lp / float(map.m_uPhotonNumEmitted);*/
}

}