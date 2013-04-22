/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#include <core/DataIO.h>
#include <core/DataMultiplet.h>
#include <electronic/NonlocalPCM.h>
#include <electronic/PCM_internal.h>
#include <electronic/Everything.h>
#include <electronic/SphericalHarmonics.h>
#include <electronic/operators.h>

struct MultipoleResponse
{	int l; //angular momentum
	RealKernel w; //weight function in fourier space (with G^l factored out)
	double e; //prefactor (defined so that 1+e*(w[0]^2) would be dielectric constant for l=1)
	
	MultipoleResponse(const GridInfo& gInfo, int l, double e) : l(l), w(gInfo), e(e)
	{
	}
};


inline void initWaterNPCMkernels(int i, double G2, double* nFluid,
	double* rot0, double* rot1, double* rot2, double* el0, double* el1, double* el2)
{
	double G = sqrt(G2);
	//Model electron density:
	nFluid[i] = 7.56 / pow(1 + pow(0.339,2)*G2, 3);
	
	//Rotational:
	const double aO = 0.339, ZO = 7.56, ZnucO = -6.36, sigmaO = (1.52/0.5291772) / 6;
	const double aH = 0.247, ZH = 0.40, ZnucH = -1.00, sigmaH = (1.10/0.5291772) / 6;
	const double rOH = 1.888572;
	double SfO = ZnucO*exp(-0.5*pow(sigmaO,2)*G2) + ZO/pow(1+pow(aO,2)*G2,3);
	double SfH = ZnucH*exp(-0.5*pow(sigmaH,2)*G2) + ZH/pow(1+pow(aH,2)*G2,3);
	rot0[i] = sqrt(4*M_PI)*(SfO + 2*SfH *bessel_jl(0,rOH*G));
	rot1[i] = (SfH/(ZnucH+ZH)) * (G ?  3*bessel_jl(1,rOH*G)/(rOH*G) : 1.);
	rot2[i] = (SfH/(ZnucH+ZH)) * (G ? 15*bessel_jl(2,rOH*G)/pow(rOH*G,2) : 1.);

	//Electronic:
	el0[i] = G2*(2.88*exp(-0.5*pow(0.94,2)*G2) + 1.24e-3*G2*exp(-0.5*pow(0.393,2)*G2));
	el1[i] = exp(-0.5*pow(1.388,2)*G2) + 5.63e-2*G2*exp(-0.5*pow(0.660,2)*G2);
	el2[i] = exp(-0.5*pow(1.458,2)*G2) + 2.45e-2*G2*exp(-0.5*pow(0.644,2)*G2);
}

static const double GzeroTol = 1e-12;

//Initialize preconditioner kernel to inverse square root of the diagonal part of Vc^-1 + chi
inline void setPreconditionerKernel(int i, double G2, double* Kkernel,
	const std::vector< std::shared_ptr<MultipoleResponse> >* response)
{	//Compute diagonal part of the hessian ( 4pi(Vc^-1 + chi) ):
	double diagH = G2;
	for(const std::shared_ptr<MultipoleResponse>& resp: *response)
		diagH += resp->e * pow(G2,resp->l) * pow(resp->w.data[i],2);
	//Set the inverse square-root as the preconditioner:
	Kkernel[i] = (diagH>GzeroTol) ? 1./sqrt(diagH) : 0.;
}

NonlocalPCM::NonlocalPCM(const Everything& e, const FluidSolverParams& fsp)
: PCM(e, fsp), nFluid(e.gInfo), Kkernel(e.gInfo)
{	
	logPrintf("\tInitializing non-local Polarizable Continuum Model\n");
	
	//TODO: Setup fluid parameters from commands (currently hard-coded for water)
	//Water:
	const double Nbulk = 4.95e-3 * (1. - 4.74e-6*pow(fsp.T/Kelvin-277,2)); //Fit to 0-100C range at ambient pressure
	auto rot0 = std::make_shared<MultipoleResponse>(e.gInfo, 0, Nbulk/fsp.T);
	auto rot1 = std::make_shared<MultipoleResponse>(e.gInfo, 1, (3.3e4*Kelvin)/fsp.T - 34.5);
	auto rot2 = std::make_shared<MultipoleResponse>(e.gInfo, 2, 3.0553 * Nbulk/fsp.T);
	auto el0 = std::make_shared<MultipoleResponse>(e.gInfo, 0, 4*Nbulk);
	auto el1 = std::make_shared<MultipoleResponse>(e.gInfo, 1, 163*Nbulk);
	auto el2 = std::make_shared<MultipoleResponse>(e.gInfo, 2, 82*Nbulk);
	applyFuncGsq(e.gInfo, initWaterNPCMkernels, nFluid.data,
		rot0->w.data, rot1->w.data, rot2->w.data, el0->w.data, el1->w.data, el2->w.data);
	nFluid.set();
	rot0->w.set(); rot1->w.set(); rot2->w.set(); el0->w.set(); el1->w.set(); el2->w.set();
	if(fsp.npcmParams.lMax >= 0) { response.push_back(rot0); response.push_back(el0); }
	if(fsp.npcmParams.lMax >= 1) { response.push_back(rot1); response.push_back(el1); }
	if(fsp.npcmParams.lMax >= 2) { response.push_back(rot2); response.push_back(el2); }
	
	//Ionic (monopole response)
	if(fsp.ionicConcentration)
	{	auto ionResponse = std::make_shared<MultipoleResponse>(e.gInfo, 0,
			(8*M_PI/fsp.T) * fsp.ionicConcentration * pow(fsp.ionicZelectrolyte,2));
		initGaussianKernel(ionResponse->w, 1.0); //arbitrary gaussian for now
		response.push_back(ionResponse);
	}
	
	//Compute bulk properties and print summary:
	epsBulk = 1.; double k2factor = 0.; std::map<int,int> lCount;
	for(const std::shared_ptr<MultipoleResponse>& resp: response)
	{	lCount[resp->l]++;
		double respGzero = resp->e * pow(resp->w.data[0], 2);
		if(resp->l==0) k2factor += respGzero;
		if(resp->l==1) epsBulk += respGzero;
	}
	for(auto lInfo: lCount)
		logPrintf("\tl: %d  #weight-functions: %d\n", lInfo.first, lInfo.second);
	logPrintf("\tBulk dielectric-constant: %lg", epsBulk);
	if(k2factor > GzeroTol) logPrintf("   screening-length: %lg bohrs.\n", sqrt(epsBulk/k2factor));
	else logPrintf("\n");
	
	//Initialize preconditioner:
	applyFuncGsq(Kkernel.gInfo, setPreconditionerKernel, Kkernel.data, &response);
	Kkernel.set();
}

DataGptr NonlocalPCM::chi(const DataGptr& phiTilde) const
{	DataGptr rhoTilde;
	for(const std::shared_ptr<MultipoleResponse>& resp: response)
	{	switch(resp->l)
		{	case 0:
			{	rhoTilde += (-resp->e/(4*M_PI))
					* (resp->w * J(shape*I(resp->w * phiTilde)));
				break;
			}
			case 1:
			{	rhoTilde += (resp->e/(4*M_PI))
					* (resp->w * divergence(J(shape*I(gradient(resp->w * phiTilde)))));
				break;
			}
			case 2:
			{	rhoTilde += (-resp->e * 1.5/(4*M_PI))
					* (resp->w * tensorDivergence(J(shape*I(tensorGradient(resp->w * phiTilde)))));
				break;
			}
			default:
				die("NonlocalPCM: Angular momentum l=%d not yet implemented.\n", resp->l);
		}
	}
	return rhoTilde;
}


DataGptr NonlocalPCM::hessian(const DataGptr& phiTilde) const
{	return (-1./(4*M_PI*e.gInfo.detR)) * L(phiTilde) - chi(phiTilde);
}

DataGptr NonlocalPCM::precondition(const DataGptr& rTilde) const
{	return Kkernel*(J(epsInv*I(Kkernel*rTilde)));
}


void NonlocalPCM::set(const DataGptr& rhoExplicitTilde, const DataGptr& nCavityTilde)
{
	this->rhoExplicitTilde = clone(rhoExplicitTilde); zeroNyquist(this->rhoExplicitTilde);
	
	//Compute cavity shape function (0 to 1)
	nCavity = I(nFluid * nCavityTilde);
	updateCavity();
	
	logPrintf("\tNonlocalPCM fluid occupying %lf of unit cell:", integral(shape)/e.gInfo.detR);
	logFlush();

	//Update the inhomogeneity factor of the preconditioner
	epsInv = inv(1. + epsBulk*shape);
	
	//Initialize the state if it hasn't been loaded:
	if(!state) nullToZero(state, e.gInfo);
}


void NonlocalPCM::minimizeFluid()
{
	fprintf(e.fluidMinParams.fpLog, "\n\tWill stop at %d iterations, or sqrt(|r.z|)<%le\n",
		e.fluidMinParams.nIterations, e.fluidMinParams.knormThreshold);
	int nIter = solve(rhoExplicitTilde, e.fluidMinParams);
	logPrintf("\tCompleted after %d iterations.\n", nIter);
}

double NonlocalPCM::get_Adiel_and_grad(DataGptr& Adiel_rhoExplicitTilde, DataGptr& Adiel_nCavityTilde, IonicGradient& extraForces) const
{
	EnergyComponents& Adiel = ((NonlocalPCM*)this)->Adiel;
	const DataGptr& phiTilde = state; // that's what we solved for in minimize

	//The "electrostatic" gradient is the potential due to the bound charge alone:
	Adiel_rhoExplicitTilde = phiTilde - (-4*M_PI)*Linv(O(rhoExplicitTilde));
	Adiel["Electrostatic"] = 0.5*dot(Adiel_rhoExplicitTilde, O(rhoExplicitTilde)) //True energy if phi was an exact solution
		+ 0.5*dot(O(phiTilde), rhoExplicitTilde - hessian(phiTilde)); //First order residual correction (remaining error is second order)
	

	//The "cavity" gradient is computed by chain rule via the gradient w.r.t to the shape function:
	DataRptr Adiel_shape;
	for(const std::shared_ptr<MultipoleResponse>& resp: response)
	{	switch(resp->l)
		{	case 0:
			{	DataRptr Iwphi = I(resp->w * phiTilde);
				Adiel_shape += (0.5 * -resp->e/(4*M_PI)) * (Iwphi * Iwphi);
				break;
			}
			case 1:
			{	DataRptrVec Igradwphi = I(gradient(resp->w * phiTilde));
				Adiel_shape += (0.5 * -resp->e/(4*M_PI))
					* (Igradwphi[0]*Igradwphi[0] + Igradwphi[1]*Igradwphi[1] + Igradwphi[2]*Igradwphi[2]);
				break;
			}
			case 2:
			{	DataRptrTensor Itgradwphi = I(tensorGradient(resp->w * phiTilde));
				Adiel_shape += (0.5 * -resp->e * 1.5/(4*M_PI))
					* 2*( Itgradwphi[0]*Itgradwphi[0] + Itgradwphi[1]*Itgradwphi[1] + Itgradwphi[2]*Itgradwphi[2]
						+ Itgradwphi[3]*Itgradwphi[3] + Itgradwphi[4]*Itgradwphi[4] + Itgradwphi[3]*Itgradwphi[4]);
				break;
			}
			default:
				die("NonlocalPCM: Angular momentum l=%d not yet implemented.\n", resp->l);
		}
	}

	//Propagate shape gradients to A_nCavity:
	DataRptr Adiel_nCavity;
	propagateCavityGradients(Adiel_shape, Adiel_nCavity);
	Adiel_nCavityTilde = nFluid * J(Adiel_nCavity);
	
	if(vdwForces) extraForces = *vdwForces;
	return Adiel;
}

void NonlocalPCM::loadState(const char* filename)
{	DataRptr Istate(DataR::alloc(e.gInfo));
	loadRawBinary(Istate, filename); //saved data is in real space
	state = J(Istate);
}

void NonlocalPCM::saveState(const char* filename) const
{	saveRawBinary(I(state), filename); //saved data is in real space
}

void NonlocalPCM::dumpDensities(const char* filenamePattern) const
{	string filename(filenamePattern);
	filename.replace(filename.find("%s"), 2, "Shape");
	logPrintf("Dumping '%s'... ", filename.c_str());  logFlush();
	saveRawBinary(shape, filename.c_str());
	logPrintf("done.\n"); logFlush();
}
