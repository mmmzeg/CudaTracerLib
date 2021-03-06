#include <StdAfx.h>
#include <Engine/Buffer.h>
#include "PPPMTracer.h"
#include <Base/Timer.h>
#include <Engine/DynamicScene.h>
#include <Engine/Node.h>
#include <Engine/Mesh.h>
#include <Engine/Material.h>

namespace CudaTracerLib {

unsigned int ComputePhotonBlocksPerPass()
{
#ifdef NDEBUG
	return 180;
#else
	return 1;
#endif
}

PPPMTracer::PPPMTracer()
	: m_adpBuffer(0), m_fLightVisibility(1), k_Intial(10),
	m_fProbSurface(1), m_fProbVolume(1), m_uBlocksPerLaunch(ComputePhotonBlocksPerPass()),
	m_sSurfaceMap(Vec3u(250), (ComputePhotonBlocksPerPass() + 2) * PPM_slots_per_block), m_sSurfaceMapCaustic(0),
	m_debugScaleVal(1)
{
	m_sParameters
		<< KEY_Direct()			  << CreateSetBool(true)
		<< KEY_PerPixelRadius()   << CreateSetBool(false)
		<< KEY_FinalGathering()   << CreateSetBool(false)
		<< KEY_AdaptiveAccProb() << CreateSetBool(false)
		<< KEY_VolRadiusScale()   << CreateInterval(1.0f, 0.0f, FLT_MAX);

	m_uTotalPhotonsEmitted = -1;
	unsigned int numPhotons = (m_uBlocksPerLaunch + 2) * PPM_slots_per_block;
	if (m_sParameters.getValue(KEY_FinalGathering()))
		m_sSurfaceMapCaustic = new SurfaceMapT(Vec3u(250), numPhotons);
	m_pVolumeEstimator = new PointStorage(150, numPhotons);
	//m_pVolumeEstimator = new BeamGrid(150, numPhotons, 10, 2);
	//m_pVolumeEstimator = new BeamBVHStorage(100);
	//m_pVolumeEstimator = new BeamBeamGrid(10, 10000, 1000);
	if (m_sParameters.getValue(KEY_AdaptiveAccProb()))
	{
		size_t volLength, volCount;
		m_pVolumeEstimator->getStatusInfo(volLength, volCount);
		m_fProbVolume = math::clamp01(volLength / (float)m_sSurfaceMap.getNumEntries());
	}
}

PPPMTracer::~PPPMTracer()
{
	m_sSurfaceMap.Free();
	if (m_adpBuffer)
	{
		m_adpBuffer->Free();
		delete m_adpBuffer;
	}
	delete m_pVolumeEstimator;
}

void PPPMTracer::PrintStatus(std::vector<std::string>& a_Buf) const
{
	a_Buf.push_back(GET_PERF_BLOCKS().ToString());
	double pC = math::floor((float)((double)m_uTotalPhotonsEmitted / 1000000.0));
	a_Buf.push_back(format("Photons emitted : %d[Mil]", (int)pC));
	double pCs = m_uTotalPhotonsEmitted / m_fAccRuntime / 1000000.0f;
	double pCsLastSurf = m_uPhotonEmittedPassSurface / m_fLastRuntime / 1000000.0f, pCsLastVol = m_uPhotonEmittedPassVolume / m_fLastRuntime / 1000000.0f;
	a_Buf.push_back(format("Photons/Sec avg : %f", (float)pCs));
	a_Buf.push_back(format("Photons Surf/Sec lst : %f", (float)pCsLastSurf));
	a_Buf.push_back(format("Photons Vol/Sec lst : %f", (float)pCsLastVol));
	a_Buf.push_back(format("Light Visibility : %f", m_fLightVisibility));
	a_Buf.push_back(format("Photons per pass : %d*100,000", m_sSurfaceMap.getNumEntries() / 100000));
	a_Buf.push_back(format("%.2f%% Surf Photons", float(m_sSurfaceMap.getNumStoredEntries()) / m_sSurfaceMap.getNumEntries() * 100));
	if (m_sParameters.getValue(KEY_FinalGathering()))
		a_Buf.push_back(format("Caustic surf map : %.2f%%", float(m_sSurfaceMapCaustic->getNumStoredEntries()) / m_sSurfaceMapCaustic->getNumEntries() * 100));
	a_Buf.push_back("Volumeric Estimator : ");
	m_pVolumeEstimator->PrintStatus(a_Buf);
}

void PPPMTracer::Resize(unsigned int _w, unsigned int _h)
{
	Tracer<true, true>::Resize(_w, _h);
	CreateOrResize(m_adpBuffer, _w * _h);
}

void PPPMTracer::DoRender(Image* I)
{
	//I->Clear();
	{
		auto timer = START_PERF_BLOCK("Photon Pass");
		doPhotonPass();
	}
	m_uTotalPhotonsEmitted += max(m_uPhotonEmittedPassSurface, m_uPhotonEmittedPassVolume);
	setNumSequences();
	{
		auto timer = START_PERF_BLOCK("Camera Pass");
		Tracer<true, true>::DoRender(I);
		//DebugInternal(I, Vec2i(481, 240));
		//DebugInternal(I, Vec2i(323, 309));
		//DebugInternal(I, Vec2i(573, 508));
		//std::cout << getCurrentRadius(2) << "\n";
	}
}

void PPPMTracer::getRadiusAt(int x, int y, float& r, float& rd) const
{
	k_AdaptiveEntry e;
	ThrowCudaErrors(cudaMemcpy(&e, m_adpBuffer->getDevicePtr() + w * y + x, sizeof(e), cudaMemcpyDeviceToHost));
	r = e.compute_r((int)m_uPassesDone, (int)m_sSurfaceMap.getNumEntries(), (int)m_uTotalPhotonsEmitted);
	rd = e.compute_rd(m_uPassesDone, m_uPhotonEmittedPassSurface, m_uPhotonEmittedPassSurface * (m_uPassesDone - 1));
}

void PPPMTracer::StartNewTrace(Image* I)
{
	m_useDirectLighting = !m_pScene->getVolumes().hasElements() && m_sParameters.getValue(KEY_Direct());
#ifndef _DEBUG
	m_fLightVisibility = Tracer::GetLightVisibility(m_pScene, 1);
#endif
	m_useDirectLighting &= m_fLightVisibility > 0.5f;
	//m_bDirect = 0;
	Tracer<true, true>::StartNewTrace(I);
	m_uTotalPhotonsEmitted = 0;
#ifndef _DEBUG
	AABB m_sEyeBox = GetEyeHitPointBox(m_pScene, true);
#else
	AABB m_sEyeBox = m_pScene->getSceneBox();
#endif
	float r = (m_sEyeBox.maxV - m_sEyeBox.minV).sum() / float(w);
	m_sEyeBox.minV -= Vec3f(r);
	m_sEyeBox.maxV += Vec3f(r);
	m_fInitialRadiusSurf = r;
	m_fInitialRadiusVol = m_fInitialRadiusSurf * m_sParameters.getValue(KEY_VolRadiusScale());
	AABB volBox = m_pScene->getKernelSceneData().m_sVolume.box;
	for (auto it : m_pScene->getNodes())
	{
		StreamReference<Material> mats = m_pScene->getMaterials(it);
		for (unsigned int j = 0; j < mats.getLength(); j++)
		{
			const VolumeRegion* bssrdf;
			DifferentialGeometry dg;
			ZERO_MEM(dg);
			if (mats(j)->GetBSSRDF(dg, &bssrdf))
			{
				volBox = volBox.Extend(m_pScene->getNodeBox(it));
			}
		}
	}
	m_sSurfaceMap.SetSceneDimensions(m_sEyeBox);
	if (m_sParameters.getValue(KEY_FinalGathering()))
		m_sSurfaceMapCaustic->SetSceneDimensions(m_sEyeBox);
	m_pVolumeEstimator->StartNewRendering(volBox);

	float r_scene = m_sEyeBox.Size().length() / 2;
	r_min = 1e-6f * r_scene;
	r_max = 1e-1f * r_scene;
}

}