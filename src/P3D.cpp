#include "libcvtx.h"
/*============================================================================
P3D.c

Vortex particle in 2D with CPU based code.

Copyright(c) 2019-2020 HJA Bird

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
============================================================================*/

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "GridParticleOcttree.h"
#include "array_methods.h"
#include "redistribution_helper_funcs.h"
#include "UIntKey96.h"

#ifdef CVTX_USING_OPENCL
#	include "ocl_P3D.h"
#endif
#ifdef CVTX_USING_OPENMP
#	include <omp.h>
#endif

#define CVTX_PI_F 3.14159265359f

/* The induced velocity for a particle excluding the constant
coefficient 1 / 4pi */
static inline bsv_V3f P3D_vel_inner(
	const cvtx_P3D * self,
	const bsv_V3f mes_point,
	const cvtx_VortFunc * kernel,
	float recip_reg_rad)
{
	bsv_V3f rad, num, ret;
	if (bsv_V3f_isequal(self->coord, mes_point)) {
		ret = bsv_V3f_zero();
	}
	else {
		float cor, den, rho, radd;
		rad = bsv_V3f_minus(mes_point, self->coord);
		radd = bsv_V3f_abs(rad);
		rho = radd * recip_reg_rad; /* Assume positive. */
		cor = -kernel->g_3D(rho);
		den = powf(radd, -3);
		num = bsv_V3f_cross(rad, self->vorticity);
		ret = bsv_V3f_mult(num, cor * den);
	}
	return ret;
}

CVTX_EXPORT bsv_V3f cvtx_P3D_S2S_vel(
	const cvtx_P3D * self,
	const bsv_V3f mes_point,
	const cvtx_VortFunc * kernel,
	float regularisation_radius)
{
	bsv_V3f ret;
	ret = P3D_vel_inner(self, mes_point, kernel, 
		1.f/fabsf(regularisation_radius));
	return bsv_V3f_mult(ret, 1.f / (4.f * CVTX_PI_F));
}

CVTX_EXPORT bsv_V3f cvtx_P3D_S2S_dvort(
	const cvtx_P3D * self,
	const cvtx_P3D * induced_particle,
	const cvtx_VortFunc * kernel,
	float regularisation_radius)
{
	bsv_V3f ret, rad, cross_om, t2, t21, t21n, t22;
	float g, f, radd, rho, t1, t21d, t221, t222, t223;
	if(bsv_V3f_isequal(self->coord, induced_particle->coord)){
		ret = bsv_V3f_zero();
	} else {
		rad = bsv_V3f_minus(induced_particle->coord, self->coord);
		radd = bsv_V3f_abs(rad);
		rho = fabsf(radd / regularisation_radius);
		kernel->combined_3D(rho, &g, &f);
		cross_om = bsv_V3f_cross(induced_particle->vorticity, self->vorticity);
		t1 = 1.f / (4.f * CVTX_PI_F * powf(regularisation_radius, 3));
		t21n = bsv_V3f_mult(cross_om, g);
		t21d = rho * rho * rho;
		t21 = bsv_V3f_div(t21n, t21d);
		t221 = -1.f / (radd * radd);
		t222 = (3 * g) / t21d - f;
		t223 = bsv_V3f_dot(rad, cross_om);
		t22 = bsv_V3f_mult(rad, t221 * t222 * t223);
		t2 = bsv_V3f_plus(t21, t22);
		ret = bsv_V3f_mult(t2, t1);
	}
	return ret;
}

CVTX_EXPORT bsv_V3f cvtx_P3D_S2S_visc_dvort(
	const cvtx_P3D * self,
	const cvtx_P3D * induced_particle,
	const cvtx_VortFunc * kernel,
	float regularisation_radius,
	float kinematic_visc)
{	
	bsv_V3f ret, rad, t211, t212, t21, t2;
	float radd, rho, t1, t22;
	assert(kernel->eta_3D != NULL && "Used vortex regularisation"
		"that did have a defined eta function");
	if(bsv_V3f_isequal(self->coord, induced_particle->coord)){
		ret = bsv_V3f_zero();
	} else {
		rad = bsv_V3f_minus(self->coord, induced_particle->coord);
		radd = bsv_V3f_abs(rad);
		rho = fabsf(radd / regularisation_radius);
		t1 =  2 * kinematic_visc / powf(regularisation_radius, 2);
		t211 = bsv_V3f_mult(self->vorticity, 
			induced_particle->volume);
		t212 = bsv_V3f_mult(induced_particle->vorticity, 
			-1 * self->volume);
		t21 = bsv_V3f_plus(t211, t212);
		t22 = kernel->eta_3D(rho);
		t2 = bsv_V3f_mult(t21, t22);
		ret = bsv_V3f_mult(t2, t1);
	}
	return ret;
}

CVTX_EXPORT bsv_V3f cvtx_P3D_S2S_vort(
	const cvtx_P3D* self,
	const bsv_V3f mes_point,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	bsv_V3f rad, ret;
	float radd, coeff, divisor;
	rad = bsv_V3f_minus(self->coord, mes_point);
	radd = bsv_V3f_abs(rad);
	coeff = kernel->zeta_3D(radd / regularisation_radius);
	divisor = 4.f * CVTX_PI_F * 
		regularisation_radius * regularisation_radius * regularisation_radius;
	coeff = coeff / divisor;
	ret = bsv_V3f_mult(self->vorticity, coeff);
	return ret;
}

CVTX_EXPORT void cvtx_P3D_S2M_vel(
	const cvtx_P3D* self,
	const bsv_V3f* mes_start,
	const int num_mes,
	bsv_V3f* result_array,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	int i;
#pragma omp parallel for
	for (i = 0; i < num_mes; ++i) {
		result_array[i] = cvtx_P3D_S2S_vel(
			self, mes_start[i], kernel, regularisation_radius);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_S2M_dvort(
	const cvtx_P3D* self,
	const cvtx_P3D** induced_start,
	const int num_induced,
	bsv_V3f* result_array,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	int i;
#pragma omp parallel for
	for (i = 0; i < num_induced; ++i) {
		result_array[i] = cvtx_P3D_S2S_dvort(
			self, induced_start[i], kernel, regularisation_radius);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_S2M_visc_dvort(
	const cvtx_P3D* self,
	const cvtx_P3D** induced_start,
	const int num_induced,
	bsv_V3f* result_array,
	const cvtx_VortFunc* kernel,
	float regularisation_radius,
	float kinematic_visc) {
	int i;
#pragma omp parallel for
	for (i = 0; i < num_induced; ++i) {
		result_array[i] = cvtx_P3D_S2S_visc_dvort(
			self, induced_start[i], 
			kernel, regularisation_radius, kinematic_visc);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_S2M_vort(
	const cvtx_P3D* self,
	const bsv_V3f* mes_start,
	const int num_mes,
	bsv_V3f* result_array,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	int i;
#pragma omp parallel for
	for (i = 0; i < num_mes; ++i) {
		result_array[i] = cvtx_P3D_S2S_vort(
			self, mes_start[i],
			kernel, regularisation_radius);
	}
	return;
}

CVTX_EXPORT bsv_V3f cvtx_P3D_M2S_vel(
	const cvtx_P3D **array_start,
	const int num_particles,
	const bsv_V3f mes_point,
	const cvtx_VortFunc *kernel,
	float regularisation_radius)
{
	double rx = 0, ry = 0, rz = 0;
	long i;
	float recip_reg_rad = 1.f / fabsf(regularisation_radius);
	assert(num_particles >= 0);
#pragma omp parallel for reduction(+:rx, ry, rz)
	for (i = 0; i < num_particles; ++i) {
		bsv_V3f vel = P3D_vel_inner(array_start[i],
			mes_point, kernel, recip_reg_rad);
		rx += vel.x[0];
		ry += vel.x[1];
		rz += vel.x[2];
	}
	bsv_V3f ret = {(float)rx, (float)ry, (float)rz};
	return bsv_V3f_mult(ret, 1.f / (4.f * CVTX_PI_F));
}

CVTX_EXPORT bsv_V3f cvtx_P3D_M2S_dvort(
	const cvtx_P3D **array_start,
	const int num_particles,
	const cvtx_P3D *induced_particle,
	const cvtx_VortFunc *kernel,
	float regularisation_radius)
{
	bsv_V3f dvort;
	double rx = 0, ry = 0, rz = 0;
	long i;
	assert(num_particles >= 0);
	for (i = 0; i < num_particles; ++i) {
		dvort = cvtx_P3D_S2S_dvort(array_start[i],
			induced_particle, kernel, regularisation_radius);
		rx += dvort.x[0];
		ry += dvort.x[1];
		rz += dvort.x[2];
	}
	bsv_V3f ret = {(float)rx, (float)ry, (float)rz};
	return ret;
}

CVTX_EXPORT bsv_V3f cvtx_P3D_M2S_visc_dvort(
	const cvtx_P3D **array_start,
	const int num_particles,
	const cvtx_P3D *induced_particle,
	const cvtx_VortFunc *kernel,
	float regularisation_radius,
	float kinematic_visc)
{
	bsv_V3f dvort;
	double rx = 0, ry = 0, rz = 0;
	long i;
	assert(num_particles >= 0);
	for (i = 0; i < num_particles; ++i) {
		dvort = cvtx_P3D_S2S_visc_dvort(array_start[i],
			induced_particle, kernel, regularisation_radius, kinematic_visc);
		rx += dvort.x[0];
		ry += dvort.x[1];
		rz += dvort.x[2];
	}
	bsv_V3f ret = {(float)rx, (float)ry, (float)rz};
	return ret;
}

CVTX_EXPORT bsv_V3f cvtx_P3D_M2S_vort(
	const cvtx_P3D** array_start,
	const int num_particles,
	const bsv_V3f mes_point,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	float cutoff, rsigma, radd, coeff;
	bsv_V3f rad, sum = bsv_V3f_zero();
	long i;
	cutoff = 5.f * regularisation_radius;
	rsigma = 1 / regularisation_radius;
	assert(num_particles > 0);
	for (i = 0; i < num_particles; ++i) {
		rad = bsv_V3f_minus(array_start[i]->coord, mes_point);
		if (fabsf(rad.x[0]) < cutoff && fabsf(rad.x[1]) < cutoff
			&& fabsf(rad.x[2]) < cutoff) {
			radd = bsv_V3f_abs(rad);
			coeff = kernel->zeta_3D(radd * rsigma);
			sum = bsv_V3f_plus(bsv_V3f_mult(array_start[i] ->vorticity, coeff), sum);
		}
	}
	sum = bsv_V3f_div(sum, 4.f * CVTX_PI_F 
		* regularisation_radius * regularisation_radius * regularisation_radius);
	return sum;
} 

static void cpu_brute_force_P3D_M2M_vel(
	const cvtx_P3D **array_start,
	const int num_particles,
	const bsv_V3f *mes_start,
	const int num_mes,
	bsv_V3f *result_array,
	const cvtx_VortFunc *kernel,
	float regularisation_radius)
{
	long i;
#pragma omp parallel for schedule(static)
	for(i = 0; i < num_mes; ++i){
		result_array[i] = cvtx_P3D_M2S_vel(
			array_start, num_particles, mes_start[i], 
			kernel, regularisation_radius);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_M2M_vel(
	const cvtx_P3D **array_start,
	const int num_particles,
	const bsv_V3f *mes_start,
	const int num_mes,
	bsv_V3f *result_array,
	const cvtx_VortFunc *kernel,
	float regularisation_radius)
{
#ifdef CVTX_USING_OPENCL
	if (num_particles < 256
		|| num_mes < 256
		|| !strcmp(kernel->cl_kernel_name_ext, "")
		|| opencl_brute_force_P3D_M2M_vel(
			array_start, num_particles, mes_start,
			num_mes, result_array, kernel, regularisation_radius) != 0)
#endif
	{
		cpu_brute_force_P3D_M2M_vel(
			array_start, num_particles, mes_start,
			num_mes, result_array, kernel, regularisation_radius);
	}
	return;
}

void cpu_brute_force_P3D_M2M_dvort(
	const cvtx_P3D **array_start,
	const int num_particles,
	const cvtx_P3D **induced_start,
	const int num_induced,
	bsv_V3f *result_array,
	const cvtx_VortFunc *kernel,
	float regularisation_radius)
{
	long i;
#pragma omp parallel for schedule(static)
	for (i = 0; i < num_induced; ++i) {
		result_array[i] = cvtx_P3D_M2S_dvort(
			array_start, num_particles, induced_start[i], 
			kernel, regularisation_radius);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_M2M_dvort(
	const cvtx_P3D **array_start,
	const int num_particles,
	const cvtx_P3D **induced_start,
	const int num_induced,
	bsv_V3f *result_array,
	const cvtx_VortFunc *kernel,
	float regularisation_radius)
{
#ifdef CVTX_USING_OPENCL
	if (	num_particles < 256
		||	num_induced < 256
		||	!strcmp(kernel->cl_kernel_name_ext, "")
		||	opencl_brute_force_P3D_M2M_dvort(
				array_start, num_particles, induced_start,
				num_induced, result_array, kernel, regularisation_radius) != 0)
#endif
	{
		cpu_brute_force_P3D_M2M_dvort(
			array_start, num_particles, induced_start,
			num_induced, result_array, kernel, regularisation_radius);
	}
	return;
}

void cpu_brute_force_P3D_M2M_visc_dvort(
	const cvtx_P3D **array_start,
	const int num_particles,
	const cvtx_P3D **induced_start,
	const int num_induced,
	bsv_V3f *result_array,
	const cvtx_VortFunc *kernel,
	float regularisation_radius,
	float kinematic_visc)
{
	long i;
#pragma omp parallel for schedule(static)
	for (i = 0; i < num_induced; ++i) {
		result_array[i] = cvtx_P3D_M2S_visc_dvort(
			array_start, num_particles, induced_start[i],
			kernel, regularisation_radius, kinematic_visc);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_M2M_visc_dvort(
	const cvtx_P3D **array_start,
	const int num_particles,
	const cvtx_P3D **induced_start,
	const int num_induced,
	bsv_V3f *result_array,
	const cvtx_VortFunc *kernel,
	float regularisation_radius,
	float kinematic_visc)
{
#ifdef CVTX_USING_OPENCL
	if (	num_particles < 256
		||	num_induced < 256
		||	!strcmp(kernel->cl_kernel_name_ext, "")
		||	opencl_brute_force_P3D_M2M_visc_dvort(
				array_start, num_particles, induced_start,
				num_induced, result_array, kernel, regularisation_radius, kinematic_visc) != 0)
#endif
	{
		cpu_brute_force_P3D_M2M_visc_dvort(
			array_start, num_particles, induced_start,
			num_induced, result_array, kernel, regularisation_radius, kinematic_visc);
	}
	return;
}

void cpu_brute_force_P3D_M2M_vort(
	const cvtx_P3D** array_start,
	const int num_particles,
	const bsv_V3f* mes_start,
	const int num_mes,
	bsv_V3f* result_array,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	long i;
#pragma omp parallel for schedule(guided)
	for (i = 0; i < num_mes; ++i) {
		result_array[i] = cvtx_P3D_M2S_vort(
			array_start, num_particles, mes_start[i],
			kernel, regularisation_radius);
	}
	return;
}

CVTX_EXPORT void cvtx_P3D_M2M_vort(
	const cvtx_P3D** array_start,
	const int num_particles,
	const bsv_V3f* mes_start,
	const int num_mes,
	bsv_V3f* result_array,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
#ifdef CVTX_USING_OPENCL
	if (num_particles < 256
		|| num_mes < 256
		|| !strcmp(kernel->cl_kernel_name_ext, "")
		|| opencl_brute_force_P3D_M2M_vort(
			array_start, num_particles, mes_start,
			num_mes, result_array, kernel, regularisation_radius) != 0)
#endif
	{
		cpu_brute_force_P3D_M2M_vort(
			array_start, num_particles, mes_start,
			num_mes, result_array, kernel, regularisation_radius);
	}
	return;
}


/* Particle redistribution -------------------------------------------------*/

/* Modifies io_arr to remove particles under a strength threshold,
returning the number of particles. */
static int cvtx_remove_particles_under_str_threshold(
	cvtx_P3D *io_arr, float* strs, int n_inpt_partices, float threshold,
	int max_keepable);

CVTX_EXPORT int cvtx_P3D_redistribute_on_grid(
	const cvtx_P3D** input_array_start,
	const int n_input_particles,
	cvtx_P3D* output_particles,		/* input is &(*cvtx_P3D) to write to */
	int max_output_particles,		/* Set to resultant num particles.   */
	const cvtx_RedistFunc* redistributor,
	const float grid_density,
	float negligible_vort) {

	assert(n_input_particles >= 0);
	assert(max_output_particles >= 0);
	assert(grid_density > 0.f);
	assert(negligible_vort >= 0.f);
	assert(negligible_vort < 1.f);
	size_t n_created_particles;
	int grid_radius;
	bsv_V3f min, mean;				/* Bounds of the particle box.		*/
	const float recip_grid_density = 1.f / grid_density;
	/* For particle removal: */
	float min_keepable_particle;
#ifdef CVTX_USING_OPENMP
	unsigned int nthreads = omp_get_num_procs();
	omp_set_dynamic(0);
	omp_set_num_threads(nthreads);
#else
	unsigned int nthreads = 1;
#endif

	/* Generate grid keys for existing particles. */
	minmax_xyz_posn(input_array_start, n_input_particles,
		&min, NULL);
	mean = mean_xyz_posn(input_array_start, n_input_particles);
	grid_radius = (int)roundf(redistributor->radius);
	min = bsv_V3f_minus(min, 
		bsv_V3f_mult({ 1.f,1.f,1.f }, grid_radius * grid_density));
	bsv_V3f dcorner = bsv_V3f_div(bsv_V3f_minus(mean, min), grid_density);
	dcorner.x[0] = roundf(dcorner.x[0]) + 5;
	dcorner.x[1] = roundf(dcorner.x[1]) + 5;
	dcorner.x[2] = roundf(dcorner.x[2]) + 5;
	min = bsv_V3f_minus(mean, bsv_V3f_mult(dcorner, grid_density));

	/* Create an octtree on a grid and add all the new particles to it. 
	We spread the work across multiple threads and then merge the results. */
	std::vector<GridParticleOcttree> ptree(nthreads);
#pragma omp parallel for schedule(static)
	for (long long  threadid = 0; threadid < nthreads; threadid++) {
		std::vector<UIntKey96> key_buffer;
		std::vector<bsv_V3f> str_buffer;
		size_t key_buffer_sz = UIntKey96::num_nearby_keys(grid_radius);
		key_buffer.resize(key_buffer_sz);
		str_buffer.resize(key_buffer_sz);
		size_t istart, iend; /* Particles for this thread. */
		istart = threadid * (n_input_particles / nthreads);
		iend = threadid == nthreads - 1 ? n_input_particles :
			(threadid + 1) * (n_input_particles / nthreads);
		for (long long  i = istart; i < (long long) iend; ++i) {
			bsv_V3f tparticle_pos = input_array_start[i]->coord;
			bsv_V3f tparticle_str = input_array_start[i]->vorticity;
			UIntKey96 key = UIntKey96::nearest_key_min(
				input_array_start[i]->coord, recip_grid_density, min);
			key.nearby_keys(grid_radius, key_buffer.data(), key_buffer.size());
			for (size_t j = 0; j < key_buffer_sz; ++j) {
				bsv_V3f npos = key_buffer[j].to_position_min(grid_density, min);
				float U, W, V, vortfrac;
				bsv_V3f dx = bsv_V3f_minus(tparticle_pos, npos);
				U = fabsf(dx.x[0] * recip_grid_density);
				W = fabsf(dx.x[1] * recip_grid_density);
				V = fabsf(dx.x[2] * recip_grid_density);
				vortfrac = redistributor->func(U) * redistributor->func(W)
					* redistributor->func(V);
				str_buffer[j] = bsv_V3f_mult(tparticle_str, vortfrac);
			}
			ptree[threadid].add_particles(key_buffer, str_buffer);
		}
	}
	for (int threadid = 1; threadid < (int)nthreads; threadid++) {
		ptree[0].merge_in(ptree[threadid]);
	}
	ptree.resize(1);
	GridParticleOcttree &tree = ptree[0];
	/* Go back to array of particles. */
	n_created_particles = tree.number_of_particles();
	std::vector<UIntKey96> new_particle_keys(n_created_particles);
	std::vector<bsv_V3f> new_particle_strs(n_created_particles);
	std::vector<cvtx_P3D> new_particles(n_created_particles);
	tree.flatten_tree(new_particle_keys.data(), 
		new_particle_strs.data(), (int)n_created_particles);
	float np_vol = grid_density * grid_density * grid_density;
	for (int i = 0; i < n_created_particles; ++i) {
		new_particles[i].volume = np_vol;
		new_particles[i].vorticity = new_particle_strs[i];
		new_particles[i].coord = 
			new_particle_keys[i].to_position_min(grid_density, min);
	}
	new_particle_keys.clear();
	new_particle_strs.clear();
	/* Remove particles with neglidgible vorticity. */
	std::vector<float> strengths(n_created_particles);
#pragma omp parallel for
	for (long long i = 0 ; i < n_created_particles; ++i) {
		strengths[i] = bsv_V3f_abs(new_particles[i].vorticity);
	}
	farray_info(strengths.data(), n_created_particles, &min_keepable_particle, NULL, NULL);
	min_keepable_particle = min_keepable_particle * negligible_vort;
	n_created_particles = cvtx_remove_particles_under_str_threshold(
		new_particles.data(), strengths.data(), n_created_particles,
		min_keepable_particle, n_created_particles);
	new_particles.resize(n_created_particles);
	/* The strengths are modified to keep total vorticity constant. */
#pragma omp parallel for
	for (long long  i = 0; i < n_created_particles; ++i) {
		strengths[i] = bsv_V3f_abs(new_particles[i].vorticity);
	}
	/* Now to handle what we return to the caller */
	if (output_particles != NULL) {
		if (n_created_particles > max_output_particles) {
			min_keepable_particle = get_strength_threshold(
				strengths.data(), n_created_particles, max_output_particles);
			n_created_particles = cvtx_remove_particles_under_str_threshold(new_particles.data(),
				strengths.data(), n_created_particles, min_keepable_particle, max_output_particles);
		}
		/* And now make an array to return to our caller. */
		memcpy(output_particles, new_particles.data(), sizeof(cvtx_P3D) * n_created_particles);
	}
	return n_created_particles;
}

int cvtx_remove_particles_under_str_threshold(
	cvtx_P3D* io_arr, float* strs, 
	int n_inpt_partices, float min_keepable_str,
	int max_keepable) {

	bsv_V3f vorticity_deficit;
	int n_output_particles;
	int i, j;

	j = 0; 
	vorticity_deficit = bsv_V3f_zero();

	for (i = 0; i < n_inpt_partices; ++i) {
		if (strs[i] > min_keepable_str && i < max_keepable) {
			io_arr[j] = io_arr[i];
			++j;
		}
		else {
			/* For vorticity conservation. */
			vorticity_deficit = bsv_V3f_plus(io_arr[i].vorticity, vorticity_deficit);
		}
	}

	n_output_particles = j;
	vorticity_deficit = bsv_V3f_div(vorticity_deficit, (float)n_output_particles);
	for (i = 0; i < n_output_particles; ++i) {
		io_arr[i].vorticity = bsv_V3f_plus(io_arr[i].vorticity, vorticity_deficit);
	}
	return n_output_particles;
}

/* Relaxation --------------------------------------------------------------*/

CVTX_EXPORT void cvtx_P3D_pedrizzetti_relaxation(
	cvtx_P3D** input_array_start,
	const int n_input_particles,
	float fdt,
	const cvtx_VortFunc* kernel,
	float regularisation_radius) {
	/* Pedrizzetti relaxation scheme: 
		alpha_new =	(1-fq * dt) * alpha_old
					+ fq * dt * omega(x) * abs(alpha_old) / abs(omega(x)) */
	bsv_V3f *mes_posns = NULL, *omegas = NULL;
	int i;
	float tmp;
	mes_posns = (bsv_V3f*) malloc(sizeof(bsv_V3f) * n_input_particles);
#pragma omp parallel for
	for (i = 0; i < n_input_particles; ++i) {
		mes_posns[i] = input_array_start[i]->coord;
	}
	omegas = (bsv_V3f*) malloc(sizeof(bsv_V3f) * n_input_particles);
	cvtx_P3D_M2M_vort((const cvtx_P3D**) input_array_start, n_input_particles, 
		mes_posns, n_input_particles, omegas, kernel, regularisation_radius);

	tmp = 1.f - fdt;
#pragma omp parallel for
	for (i = 0; i < n_input_particles; ++i) {
		bsv_V3f ovort, nvort;	/* original & new vorts*/
		float coeff, absomega;
		ovort = input_array_start[i]->vorticity;
		absomega = bsv_V3f_abs(omegas[i]);
		coeff = bsv_V3f_abs(ovort) / absomega;
		nvort = bsv_V3f_mult(ovort, tmp);
		nvort = bsv_V3f_plus(nvort, bsv_V3f_mult(omegas[i], coeff * fdt)); 
		nvort = absomega != 0.f ?  nvort : bsv_V3f_zero();
		input_array_start[i]->vorticity = nvort;
	}

	free(mes_posns);
	free(omegas);
	return;
}
