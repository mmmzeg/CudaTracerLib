#include "TraceAlgorithms.h"
#include "TraceHelper.h"
#include <Engine/Samples.h>
#include <Engine/Material.h>
#include <Engine/Light.h>

namespace CudaTracerLib {

bool V(const Vec3f& a, const Vec3f& b, TraceResult* res)
{
	Vec3f d = b - a;
	float l = length(d);
	return !g_SceneData.Occluded(Ray(a, d / l), 0, l, res);
}

float G(const NormalizedT<Vec3f>& N_x, const NormalizedT<Vec3f>& N_y, const Vec3f& x, const Vec3f& y)
{
	auto theta = normalize(y - x);
	return absdot(N_x, theta) * absdot(N_y, -theta) / distanceSquared(x, y);
}

Spectrum Transmittance(const Ray& r, float tmin, float tmax)
{
	if (g_SceneData.m_sVolume.HasVolumes())
	{
		float a, b;
		g_SceneData.m_sVolume.IntersectP(r, tmin, tmax, &a, &b);
		return (-g_SceneData.m_sVolume.tau(r, a, b)).exp();
	}
	return Spectrum(1.0f);
}

CUDA_FUNC_IN Spectrum EstimateDirect(BSDFSamplingRecord bRec, const Material& mat, const Light* light, EBSDFType flags, Sampler& rng, bool attenuated)
{
	DirectSamplingRecord dRec(bRec.dg.P, bRec.dg.sys.n);
	Spectrum value = light->sampleDirect(dRec, rng.randomFloat2());
	Spectrum retVal(0.0f);
	if (!value.isZero())
	{
		auto oldWo = bRec.wo;
		bRec.wo = bRec.dg.toLocal(dRec.d);
		bRec.typeMask = flags;
		Spectrum bsdfVal = mat.bsdf.f(bRec);
		if (!bsdfVal.isZero() && !g_SceneData.Occluded(Ray(dRec.ref, dRec.d), 0, dRec.dist))
		{
			const float bsdfPdf = mat.bsdf.pdf(bRec);
			const float weight = MonteCarlo::PowerHeuristic(1, dRec.pdf, 1, bsdfPdf);
			retVal = value * bsdfVal * weight;
			if (attenuated)
				retVal *= Transmittance(Ray(dRec.ref, dRec.d), 0, dRec.dist);
		}
		bRec.typeMask = EAll;
		bRec.wo = oldWo;
	}
	return retVal;
}

Spectrum UniformSampleAllLights(const BSDFSamplingRecord& bRec, const Material& mat, int nSamples, Sampler& rng, bool attenuated)
{
	//only sample the relevant lights and assume the others emit the same
	Spectrum L = Spectrum(0.0f);
	for (unsigned int i = 0; i < g_SceneData.m_numLights; i++)
	{
		const Light* light = g_SceneData.getLight(i);
		Spectrum Ld = Spectrum(0.0f);
		for (int j = 0; j < nSamples; j++)
		{
			Ld += EstimateDirect((BSDFSamplingRecord&)bRec, mat, light, EBSDFType(EAll & ~EDelta), rng, attenuated);
		}
		L += Ld / float(nSamples);
	}
	return L;
}

Spectrum UniformSampleOneLight(const BSDFSamplingRecord& bRec, const Material& mat, Sampler& rng, bool attenuated)
{
	if (!g_SceneData.m_numLights)
		return Spectrum(0.0f);
	Vec2f sample = rng.randomFloat2();
	float pdf;
	const Light* light = g_SceneData.sampleEmitter(pdf, sample);
	if (light == 0) return Spectrum(0.0f);
	return EstimateDirect((BSDFSamplingRecord&)bRec, mat, light, EBSDFType(EAll & ~EDelta), rng, attenuated) / pdf;
}

}