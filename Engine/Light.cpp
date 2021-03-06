#include <stdafx.h>
#include "Light.h"
#include "MIPMap.h"
#include <Math/Sampling.h>
#include <Math/Warp.h>
#include <Engine/Buffer.h>

namespace CudaTracerLib {

	InfiniteLight::InfiniteLight(Stream<char>* a_Buffer, BufferReference<MIPMap, KernelMIPMap>& mip, const Spectrum& scale, const AABB* scenBox)
		: LightBase(false), radianceMap(mip->getKernelData()), m_scale(scale), m_pSceneBox(scenBox)
	{
		m_size = Vec2f((float)radianceMap.m_uWidth, (float)radianceMap.m_uHeight);
		unsigned int nEntries = (unsigned int)(m_size.x + 1) * (unsigned int)m_size.y;
		StreamReference<char> m1 = a_Buffer->malloc_aligned<float>(nEntries * sizeof(float)),
			m2 = a_Buffer->malloc_aligned<float>((radianceMap.m_uHeight + 1) * sizeof(float)),
			m3 = a_Buffer->malloc_aligned<float>(radianceMap.m_uHeight * sizeof(float));
		m_cdfCols = m1.AsVar<float>();
		m_cdfRows = m2.AsVar<float>();
		m_rowWeights = m3.AsVar<float>();
		unsigned int colPos = 0, rowPos = 0;
		float rowSum = 0.0f;
		m_cdfRows[rowPos++] = 0;
		for (int y = 0; y < m_size.y; ++y)
		{
			float colSum = 0;

			m_cdfCols[colPos++] = 0;
			for (int x = 0; x < m_size.x; ++x)
			{
				Spectrum value = radianceMap.Sample(0, x, y);

				colSum += value.getLuminance();
				m_cdfCols[colPos++] = (float)colSum;
			}

			float normalization = 1.0f / (float)colSum;
			for (int x = 1; x < m_size.x; ++x)
				m_cdfCols[colPos - x - 1] *= normalization;
			m_cdfCols[colPos - 1] = 1.0f;

			float weight = sinf((y + 0.5f) * PI / float(m_size.y));
			m_rowWeights[y] = weight;
			rowSum += colSum * weight;
			m_cdfRows[rowPos++] = (float)rowSum;
		}
		float normalization = 1.0f / (float)rowSum;
		for (int y = 1; y < m_size.y; ++y)
			m_cdfRows[rowPos - y - 1] *= normalization;
		m_cdfRows[rowPos - 1] = 1.0f;
		m_normalization = 1.0f / (rowSum * (2 * PI / m_size.x) * (PI / m_size.y));
		m_pixelSize = Vec2f(2 * PI / m_size.x, PI / m_size.y);
		m1.Invalidate(); m2.Invalidate(); m3.Invalidate();

		float lvl = 0.65f, qpdf;
		unsigned int INDEX = MonteCarlo::sampleReuse(m_cdfRows.operator->(), radianceMap.m_uHeight, lvl, qpdf);

		m_worldTransform = NormalizedT<OrthogonalAffineMap>::Identity();
	}

}