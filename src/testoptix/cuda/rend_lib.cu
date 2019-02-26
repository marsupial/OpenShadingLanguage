#include <optix.h>
#include <optix_device.h>
#include <optix_math.h>

#include "rend_lib.h"
#include <OSL/dual.h>


rtDeclareVariable (uint2, launch_index, rtLaunchIndex, );
rtDeclareVariable (uint2, launch_dim,   rtLaunchDim, );
rtDeclareVariable (char*, test_str_1, , );
rtDeclareVariable (char*, test_str_2, , );

__device__ constexpr int kNumSplineTypes = 6;
struct SplineBasis {
    int      basis_step;
    float    basis[4][4];
};
using CudaSplineBasis = SplineBasis[kNumSplineTypes];
struct SplineBasisArray {
    CudaSplineBasis splines;
};

//rtDeclareVariable(SplineBasisArray, gBasisSet, , );
rtBuffer<SplineBasis> gBasisSet;

namespace {

// ========================================================
//
// Interpolation bases for splines
//
// ========================================================

__device__ std::pair<const SplineBasis&, bool> getSplineBasis(const OSL::DeviceString& spline) {

/*
    if (spline == DeviceStrings::catmullrom)
        printf("CATMULL\n");
    if (spline == DeviceStrings::bezier)
        printf("BEZIER\n");
    if (spline == DeviceStrings::bspline)
        printf("BSPLINE\n");
    if (spline == DeviceStrings::hermite)
        printf("HERMITE\n");
    if (spline == DeviceStrings::constant)
        printf("CONSTANT\n");
    printf("LINEAR\n");
*/
    using namespace OSL_NAMESPACE;
    if (spline == DeviceStrings::catmullrom)
        return { gBasisSet[0], false };
    if (spline == DeviceStrings::bezier)
        return { gBasisSet[1], false };
    if (spline == DeviceStrings::bspline)
        return { gBasisSet[2], false };
    if (spline == DeviceStrings::hermite)
        return { gBasisSet[3], false };
    if (spline == DeviceStrings::constant)
        return { gBasisSet[5], true };

    return { gBasisSet[4], false };
}


// We need to know explicitly whether the knots have
// derivatives associated with them because of the way
// Dual2<T> forms of arrays are stored..  Arrays with 
// derivatives are stored:
//   T T T... T.dx T.dx T.dx... T.dy T.dy T.dy...
// This means, we need to explicitly construct the Dual2<T>
// form of the knots on the fly.
// if 'is_dual' == true, then OUTTYPE == Dual2<INTYPE>
// if 'is_dual' == false, then OUTTYPE == INTYPE

// This functor will extract a T or a Dual2<T> type from a VaryingRef array
template <class OUTTYPE, class INTYPE, bool is_dual>
struct extractValueFromArray
{
    __device__ OUTTYPE operator()(const INTYPE *value, int array_length, int idx);
};

template <class OUTTYPE, class INTYPE>
struct extractValueFromArray<OUTTYPE, INTYPE, true> 
{
    __device__ OUTTYPE operator()(const INTYPE *value, int array_length, int idx)
    {
        return OUTTYPE( value[idx + 0*array_length], 
                        value[idx + 1*array_length],
                        value[idx + 2*array_length] );
    }
};

template <class OUTTYPE, class INTYPE>
struct extractValueFromArray<OUTTYPE, INTYPE, false> 
{
    __device__ OUTTYPE operator()(const INTYPE *value, int array_length, int idx)
    {
        return OUTTYPE( value[idx] );
    }
};

template <class RTYPE, class XTYPE, class CTYPE, class KTYPE, bool knot_derivs >
__device__ void
spline_evaluate(const SplineBasis& spline, bool constant,
                RTYPE &result, XTYPE &xval, const KTYPE *knots,
                int knot_count, int knot_arraylen)
{
    using OIIO::clamp;
    XTYPE x = clamp(xval, XTYPE(0.0), XTYPE(1.0));
    int nsegs = ((knot_count - 4) / spline.basis_step) + 1;
    x = x*(float)nsegs;
    float seg_x = OSL::removeDerivatives(x);
    int segnum = (int)seg_x;
    if (segnum < 0)
        segnum = 0;
    if (segnum > (nsegs-1))
       segnum = nsegs-1;

    if (constant) {
        // Special case for "constant" basis
        RTYPE P = OSL::removeDerivatives (knots[segnum+1]);
        OSL::assignment (result, P);
        return;
    }

    // x is the position along segment 'segnum'
    x = x - float(segnum);
    int s = segnum * spline.basis_step;

    // create a functor so we can cleanly(!) extract
    // the knot elements
    extractValueFromArray<CTYPE, KTYPE, knot_derivs> myExtract;
    CTYPE P[4];
    for (int k = 0; k < 4; k++) {
        P[k] = myExtract(knots, knot_arraylen, s + k);
    }

    CTYPE tk[4];
    for (int k = 0; k < 4; k++) {
        tk[k] = spline.basis[k][0] * P[0] +
                spline.basis[k][1] * P[1] +
                spline.basis[k][2] * P[2] + 
                spline.basis[k][3] * P[3];
    }

    RTYPE tresult;
    // The following is what we want, but this gives me template errors
    // which I'm too lazy to decipher:
    //    tresult = ((tk[0]*x + tk[1])*x + tk[2])*x + tk[3];
    tresult = (tk[0]   * x + tk[1]);
    tresult = (tresult * x + tk[2]);
    tresult = (tresult * x + tk[3]);
    OSL::assignment(result, tresult);
}

}  // anonymous namespace

// These functions are declared extern to prevent name mangling.
extern "C" {

    __device__
    void* closure_component_allot (void* pool, int id, size_t prim_size, const float3& w)
    { return 0; }


    __device__
    void* closure_mul_allot (void* pool, const float3& w, ClosureColor* c)
    { return 0; }


    __device__
    void* closure_mul_float_allot (void* pool, const float& w, ClosureColor* c)
    { return 0; }


    __device__
    void* closure_add_allot (void* pool, ClosureColor* a, ClosureColor* b)
    { return 0; }


    __device__
    void* osl_allocate_closure_component (void* sg_, int id, int size)
    { return 0; }


    __device__
    void* osl_allocate_weighted_closure_component (void* sg_, int id, int size, const float3* w)
    { return 0; }


    __device__
    void* osl_mul_closure_color (void* sg_, ClosureColor* a, float3* w)
    { return 0; }


    __device__
    void* osl_mul_closure_float (void* sg_, ClosureColor* a, float w)
    { return 0; }


    __device__
    void* osl_add_closure_closure (void* sg_, ClosureColor* a, ClosureColor* b)
    { return 0; }


    __device__
    int rend_get_userdata (char* name, void* data, int data_size,
                           long long type, int index)
    {
        // Perform a userdata lookup using the parameter name, type, and
        // userdata index. If there is a match, memcpy the value into data and
        // return 1.

        // TODO: This is temporary code for initial testing and demonstration.
        if (IS_STRING(type) && HDSTR(name) == HDSTR(test_str_1)) {
            memcpy (data, &test_str_2, 8);
            return 1;
        }

        return 0;
    }


    __device__
    int osl_bind_interpolated_param (void *sg_, const void *name, long long type,
                                     int userdata_has_derivs, void *userdata_data,
                                     int symbol_has_derivs, void *symbol_data,
                                     int symbol_data_size,
                                     char *userdata_initialized, int userdata_index)
    {
        int status = rend_get_userdata ((char*)name, userdata_data, symbol_data_size,
                                        type, userdata_index);
        return status;
    }


    __device__
    int osl_strlen_is (const char *str)
    {
        return HDSTR(str).length();
    }


    __device__
    int osl_hash_is (const char *str)
    {
        return HDSTR(str).hash();
    }


    __device__
    int osl_getchar_isi (const char *str, int index)
    {
        return (str && unsigned(index) < HDSTR(str).length())
            ? str[index] : 0;
    }


    __device__
    void osl_printf (void* sg_, char* fmt_str, void* args)
    {
        printf (fmt_str, args);
    }

    __device__
    void* osl_get_texture_options (void *sg_)
    {
        return 0;
    }

    __device__
    void osl_texture_set_interp_code(void *opt, int mode)
    {
        // ((TextureOpt *)opt)->interpmode = (TextureOpt::InterpMode)mode;
    }

    __device__
    void osl_texture_set_stwrap_code (void *opt, int mode)
    {
        //((TextureOpt *)opt)->swrap = (TextureOpt::Wrap)mode;
        //((TextureOpt *)opt)->twrap = (TextureOpt::Wrap)mode;
    }

    __device__
    void osl_spline_vfv(float3 *out, const char *spline_, float *x, 
                        float3 *knots, int knot_count, int knot_arraylen)
    {
        const auto spline = getSplineBasis(HDSTR(spline_));
        spline_evaluate<float3, float, float3, float3, false>(spline.first, spline.second, *out,
                                                              *x, knots, knot_count, knot_arraylen);

    }

    __device__
    int osl_texture (void *sg_, const char *name, void *handle,
             void *opt_, float s, float t,
             float dsdx, float dtdx, float dsdy, float dtdy,
             int chans, void *result, void *dresultdx, void *dresultdy,
             void *alpha, void *dalphadx, void *dalphady,
             void *ustring_errormessage)
    {
        if (!handle)
            return 0;
        int64_t texID = int64_t(handle);
        *((float3*)result) = make_float3(optix::rtTex2D<float4>(texID, s, t));
        return 1;
    }
}
