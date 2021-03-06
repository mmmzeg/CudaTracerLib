#include "Samples.h"
#include "DifferentialGeometry.h"

namespace CudaTracerLib {

NormalizedT<Vec3f> BSDFSamplingRecord::getOutgoing() const
{
	return dg.sys.toWorld(wo);
}

BSDFSamplingRecord& BSDFSamplingRecord::operator=(const BSDFSamplingRecord& other)
{
	dg.bary = other.dg.bary;
	dg.dpdu = other.dg.dpdu;
	dg.dpdv = other.dg.dpdv;
	dg.dudx = other.dg.dudx;
	dg.dudy = other.dg.dudy;
	dg.dvdx = other.dg.dvdx;
	dg.dvdy = other.dg.dvdy;
	dg.extraData = other.dg.extraData;
	dg.hasUVPartials = other.dg.hasUVPartials;
	dg.n = other.dg.n;
	dg.P = other.dg.P;
	dg.sys = other.dg.sys;
	for (int i = 0; i < NUM_UV_SETS; i++)
		dg.uv[i] = other.dg.uv[i];

	wi = other.wi;
	wo = other.wo;
	eta = other.eta;
	mode = other.mode;
	typeMask = other.typeMask;
	sampledType = other.sampledType;
	return *this;
}

}