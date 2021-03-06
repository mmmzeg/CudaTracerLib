#pragma once

#include <Kernel/TraceHelper.h>
#include <Base/CudaRandom.h>
#include <Engine/Light.h>

namespace CudaTracerLib {

/*

//Example particle process handler
struct ParticleProcessHandler
{
	CUDA_FUNC_IN void handleEmission(const Spectrum& weight, const PositionSamplingRecord& pRec)
	{

	}

	CUDA_FUNC_IN void handleSurfaceInteraction(const Spectrum& weight, const TraceResult& res, BSDFSamplingRecord& bRec, const TraceResult& r2, bool lastBssrdf)
	{

	}

	template<bool BSSRDF> CUDA_FUNC_IN void handleMediumSampling(const Spectrum& weight, const NormalizedT<Ray>& r, const TraceResult& r2, const MediumSamplingRecord& mRec, bool sampleInMedium)
	{

	}

	template<bool BSSRDF> CUDA_FUNC_IN void handleMediumInteraction(const Spectrum& weight, const MediumSamplingRecord& mRec, const NormalizedT<Vec3f>& wi, const TraceResult& r2)
	{

	}
};

*/

template<bool PARTICIPATING_MEDIA = true, bool SUBSURFACE_SCATTERING = true, typename PROCESS> CUDA_FUNC_IN void ParticleProcess(int maxDepth, int rrStartDepth, Sampler& rng, PROCESS& P)
{
	PositionSamplingRecord pRec;
	Spectrum power = g_SceneData.sampleEmitterPosition(pRec, rng.randomFloat2()), throughput = Spectrum(1.0f);

	P.handleEmission(power, pRec);

	DirectionSamplingRecord dRec;
	power *= ((const Light*)pRec.object)->sampleDirection(dRec, pRec, rng.randomFloat2());

	NormalizedT<Ray> r(pRec.p, dRec.d);
	int depth = -1;
	DifferentialGeometry dg;
	BSDFSamplingRecord bRec(dg);

	KernelAggregateVolume& V = g_SceneData.m_sVolume;
	MediumSamplingRecord mRec;
	const VolumeRegion* bssrdf = 0;

	while (++depth < maxDepth && !throughput.isZero())
	{
		TraceResult r2 = traceRay(r);
		float minT, maxT;
		bool distInMedium = false, sampledDistance = false;
		if (PARTICIPATING_MEDIA && !bssrdf && V.HasVolumes() && V.IntersectP(r, 0, r2.m_fDist, &minT, &maxT))
		{
			sampledDistance = true;
			distInMedium = V.sampleDistance(r, 0, r2.m_fDist, rng.randomFloat(), mRec);
		}
		else if (bssrdf)
		{
			sampledDistance = true;
			distInMedium = bssrdf->sampleDistance(r, 0, r2.m_fDist, rng.randomFloat(), mRec);
		}

		if (sampledDistance)
		{
			if (bssrdf != 0)
				P.template handleMediumSampling<true>(power * throughput, r, r2, mRec, distInMedium);
			else P.template handleMediumSampling<false>(power * throughput, r, r2, mRec, distInMedium);
		}

		if (distInMedium)
		{
			if (bssrdf != 0)
				P.template handleMediumInteraction<true>(power * throughput, mRec, -r.dir(), r2);
			else P.template handleMediumInteraction<false>(power * throughput, mRec, -r.dir(), r2);
			throughput *= mRec.sigmaS * mRec.transmittance / mRec.pdfSuccess;
			PhaseFunctionSamplingRecord pfRec(-r.dir());
			if (bssrdf)
				throughput *= bssrdf->As()->Func.Sample(pfRec, rng.randomFloat2());
			else throughput *= V.Sample(mRec.p, pfRec, rng.randomFloat2());
			r.dir() = pfRec.wo;
			r.ori() = mRec.p;
		}
		else if (!r2.hasHit())
			break;
		else
		{
			if (sampledDistance)
				throughput *= mRec.transmittance / mRec.pdfFailure;
			auto wo = bssrdf ? -r.dir() : r.dir();
			Spectrum f_i = power * throughput;
			r2.getBsdfSample(wo, r(r2.m_fDist), bRec, ETransportMode::EImportance, &f_i);
			P.handleSurfaceInteraction(power * throughput, r, r2, bRec, !!bssrdf);
			Spectrum f = r2.getMat().bsdf.sample(bRec, rng.randomFloat2());
			if (SUBSURFACE_SCATTERING && !bssrdf && r2.getMat().GetBSSRDF(bRec.dg, &bssrdf))
				bRec.wo.z *= -1.0f;
			else
			{
				if (!bssrdf)
					throughput *= f;
				bssrdf = 0;
			}
			if (throughput.isZero())
				break;

			if (depth >= rrStartDepth)
			{
				float q = min(throughput.max(), 0.95f);
				if (rng.randomFloat() >= q)
					break;
				throughput /= q;
			}

			r = NormalizedT<Ray>(bRec.dg.P, bRec.getOutgoing());
		}
	}
}

}