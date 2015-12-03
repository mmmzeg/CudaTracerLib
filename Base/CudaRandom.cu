#include "CudaRandom.h"
#include <MathTypes.h>
#include <CudaMemoryManager.h>

namespace CudaTracerLib {

float CudaRNG::randomFloat()
{
	float f;
#ifdef ISCUDA
	f = curand_uniform(&state);
#else
	f = curand_uniform2(curand2(&state));
#endif
	return f * (1 - 1e-5f);//curand_uniform := (0, 1] -> [0, 1)
}

unsigned long CudaRNG::randomUint()
{
#ifdef ISCUDA
	return curand(&state);
#else
	return curand2(&state);
#endif
}

void CudaRNG::Initialize(unsigned int a_Index, unsigned int a_Spacing, unsigned int a_Offset)
{
#ifdef ISCUDA
	curand_init(1234, a_Index, 0, &state);
#else
	curand_init2(1234, a_Index, 0, &state);
#endif
}

CudaRNGBuffer::CudaRNGBuffer(unsigned int a_Length, unsigned int a_Spacing, unsigned int a_Offset)
{
	m_uNumGenerators = a_Length;
	CUDA_MALLOC(&m_pDeviceGenerators, a_Length * sizeof(CudaRNG));
	m_pHostGenerators = new CudaRNG[a_Length];
	createGenerators(a_Spacing, a_Offset);
}

void CudaRNGBuffer::Free()
{
	CUDA_FREE(m_pDeviceGenerators);
	delete[] m_pHostGenerators;
}

void CudaRNGBuffer::createGenerators(unsigned int a_Spacing, unsigned int a_Offset)
{
	for (unsigned int i = 0; i < m_uNumGenerators; i++)
	{
		(m_pHostGenerators + i)->Initialize(i, a_Spacing, a_Offset);
	}
	CUDA_MEMCPY_TO_DEVICE(m_pDeviceGenerators, m_pHostGenerators, sizeof(CudaRNG) * m_uNumGenerators);
}

CUDA_FUNC_IN unsigned int getGlobalIdx_2D_2D()
{
#ifdef ISCUDA
	unsigned int blockId = blockIdx.x + blockIdx.y * gridDim.x;
	unsigned int threadId = blockId * (blockDim.x * blockDim.y) + (threadIdx.y * blockDim.x) + threadIdx.x;
	return threadId;
#else
	return 0u;
#endif
}


CudaRNG CudaRNGBuffer::operator()()
{
	unsigned int idx = getGlobalIdx_2D_2D();
	unsigned int i = idx % m_uNumGenerators;
#ifdef ISCUDA
	CudaRNG rng = m_pDeviceGenerators[i];
	if (idx >= m_uNumGenerators)
	{
		//skipahead_sequence(idx - m_uNumGenerators, &rng.state);
	}
#else
	CudaRNG rng = m_pHostGenerators[i];
#endif
	return rng;
}

void CudaRNGBuffer::operator()(CudaRNG& val)
{
	unsigned int i = getGlobalIdx_2D_2D();
#ifdef ISCUDA
	if(i < m_uNumGenerators)
		m_pDeviceGenerators[i] = val;
#else
	m_pHostGenerators[i % m_uNumGenerators] = val;
#endif
}

void CudaRNGBuffer::NextPass()
{

}

}