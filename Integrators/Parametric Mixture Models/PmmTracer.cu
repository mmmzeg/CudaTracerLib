#include "PmmTracer.h"
#include <Kernel/TraceHelper.h>
#include <Kernel/TraceAlgorithms.h>

namespace CudaTracerLib {

CUDA_DEVICE CudaStaticWrapper<SpatialLinkedMap<SpatialEntry>> g_sMap;
CUDA_DEVICE CudaStaticWrapper<SpatialSet<DirectionModel>> g_dMap;

__global__ void tracePhotons()
{
	auto rng = g_SamplerData();
	TraceResult r2;
	NormalizedT<Ray> r;
	g_SceneData.sampleEmitterRay(r, rng.randomFloat2(), rng.randomFloat2());
	int depth = 0;
	while ((r2 = traceRay(r)).hasHit() && depth++ < 7)
	{
		DifferentialGeometry dg;
		BSDFSamplingRecord bRec(dg);
		Spectrum fi = 1.0f;
		r2.getBsdfSample(r, bRec, ETransportMode::EImportance, &fi);
		Spectrum f = r2.getMat().bsdf.sample(bRec, rng.randomFloat2());
		if (f.isZero())
			break;
		Vec3f p = r(r2.m_fDist);
		g_sMap->store(p, bRec.wi);
		if (depth > 5)
			if (rng.randomFloat() >= f.max())
				break;
		r = NormalizedT<Ray>(p, bRec.getOutgoing());
		r2.Init();
	}
	g_SamplerData(rng);
}

template<int max_SAMPLES> __global__ void updateCache(float ny)
{
	Vec3u i = Vec3u(blockIdx.x * blockDim.x + threadIdx.x,
		blockIdx.y * blockDim.y + threadIdx.y,
		blockIdx.z * blockDim.z + threadIdx.z);
	if (i.x < g_dMap->m_gridSize.x && i.y < g_dMap->m_gridSize.y && i.z < g_dMap->m_gridSize.z)
	{
		Vec3f mi = g_dMap->hashMap.InverseTransform(i), ma = g_dMap->hashMap.InverseTransform(i + Vec3u(1));
		unsigned int idx = g_dMap->hashMap.Hash(i);
		g_dMap->operator()(idx).Update<max_SAMPLES>(g_sMap, mi, ma, ny);
	}
}

__global__ void visualize(Image I, int w, int h, float scale)
{
	int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x < w && y < h)
	{
		Ray r = g_SceneData.GenerateSensorRay(x, y);
		TraceResult r2 = traceRay(r);
		float num = 0;
		if (r2.hasHit())
		{
			Vec3f p = r(r2.m_fDist);
			num = g_dMap->operator()(p).numSamples;
			//uint3 i = g_dMap.hashMap.Transform(p);
			//float3 mi = g_dMap.hashMap.InverseTransform(i), ma = g_dMap.hashMap.InverseTransform(i + make_uint3(1));
			//for(SpatialLinkedMap<SpatialEntry>::iterator it = g_sMap.begin(mi, ma); it != g_sMap.end(mi, ma); ++it)
			//	num++;
		}
		I.AddSample(x, y, Spectrum(num / scale));
	}
}

__global__ void visualizePdf(Image I, int w, int h, int xoff, int yoff, DirectionModel model)
{
	int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x < w && y < h)
	{
		float a = float(x) / float(w), b = float(y) / float(h);
		float pdf = model.gmm.p(VEC<float, 2>() % a % b);
		Spectrum c(pdf);
		I.ClearSample(x + xoff, y + yoff);
		I.AddSample(x + xoff, y + yoff, c);
	}
}

static unsigned int* modelToShow = 0;

void PmmTracer::DoRender(Image* I)
{
	I->Clear();

	sMap.ResetBuffer();
	cudaMemcpyToSymbol(g_sMap, &sMap, sizeof(sMap));
	while (!sMap.isFull())
	{
		tracePhotons << < 10, 256 >> >();
		cudaMemcpyFromSymbol(&sMap, g_sMap, sizeof(sMap));
	}
	sMap.setOnGPU();
	cudaMemcpyToSymbol(g_dMap, &dMap, sizeof(dMap));
	dMap.setOnGPU();
	int l = 6;
	auto L = dMap.m_gridSize / l + 1;
	updateCache<16> << <dim3(L.x, L.y, L.z), dim3(l, l, l) >> >(ny(passIteration++));
	ThrowCudaErrors(cudaThreadSynchronize());
	unsigned int p = 16, w, h;
	I->getExtent(w, h);
	visualize << <dim3(w / p + 1, h / p + 1, 1), dim3(p, p, 1) >> >(*I, w, h, 20.0f * (float)passIteration);
	ThrowCudaErrors(cudaThreadSynchronize());
	int rectWidth = 64;
	if (modelToShow)
	{
		DirectionModel model;
		cudaMemcpy(&model, dMap.m_buffer.getDevicePtr() + *modelToShow, sizeof(model), cudaMemcpyDeviceToHost);
		visualizePdf << <dim3(8, 8, 1), dim3(8, 8, 1) >> >(*I, rectWidth, rectWidth, w - rectWidth, h - rectWidth, model);
	}

	ThrowCudaErrors(cudaThreadSynchronize());
}

void PmmTracer::StartNewTrace(Image* I)
{
	passIteration = 1;
	AABB box = this->GetEyeHitPointBox(m_pScene, true);
	//AABB box = m_pScene->getBox(m_pScene->getNodes());
	sMap.SetSceneDimensions(box);
	dMap.ResetBuffer();
	dMap.SetSceneDimensions(box);
	auto rng = g_SamplerData();
	DirectionModel* models = new DirectionModel[dMap.NumEntries()];
	for (unsigned int i = 0; i < dMap.NumEntries(); i++)
		models[i].Initialze(rng);
	cudaMemcpy(dMap.m_buffer.getDevicePtr(), models, dMap.NumEntries() * sizeof(DirectionModel), cudaMemcpyHostToDevice);
	delete[] models;
	g_SamplerData(rng);
}

void PmmTracer::DebugInternal(Image* I, const Vec2i& p)
{
	/*
	float3* deviceDirs;
	unsigned int* deviceNum;
	CUDA_MALLOC(&deviceDirs, sizeof(float3) * 10000);
	CUDA_MALLOC(&deviceNum, sizeof(unsigned int));
	copyDirections<<<1,1>>>(p.x, p.y, deviceDirs, deviceNum);
	unsigned int N;
	cudaMemcpy(&N, deviceNum, sizeof(unsigned int), cudaMemcpyDeviceToHost);
	float3* directions = (float3*)alloca(sizeof(float3) * N);
	cudaMemcpy(directions, deviceDirs, sizeof(float3) * N, cudaMemcpyDeviceToHost);
	CUDA_FREE(deviceDirs);
	CUDA_FREE(deviceNum);
	plotPoints(directions, N);*/

	Ray r = g_SceneData.GenerateSensorRay(p.x, p.y);
	TraceResult r2 = traceRay(r);
	if (!r2.hasHit())
	{
		modelToShow = 0;
		return;
	}
	Vec3f pa = r(r2.m_fDist);
	unsigned int idx = dMap.hashMap.Hash(pa);
	modelToShow = new unsigned int(idx);
	DirectionModel model;
	cudaMemcpy(&model, dMap.m_buffer.getDevicePtr() + idx, sizeof(model), cudaMemcpyDeviceToHost);
	plotModel(model);
}

}