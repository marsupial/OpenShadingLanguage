#include <optix.h>
#include <optix_device.h>
#include <optix_math.h>

#include "rend_lib.h"

#include <OSL/dual.h>
#include "../liboslexec/splineimpl.h"

rtBuffer<OSL_NAMESPACE::pvt::Spline::SplineBasis> gBasisSet;

rtDeclareVariable (uint2, launch_index, rtLaunchIndex, );
rtDeclareVariable (uint2, launch_dim,   rtLaunchDim, );
rtDeclareVariable (char*, test_str_1, , );
rtDeclareVariable (char*, test_str_2, , );


// These functions are declared extern to prevent name mangling.
extern "C" {

    __device__
    void* closure_component_allot (void* pool, int id, size_t prim_size, const float3& w)
    {
        ((ClosureComponent*) pool)->id = id;
        ((ClosureComponent*) pool)->w  = w;

        size_t needed   = (sizeof(ClosureComponent) - sizeof(void*) + prim_size + 0x7) & ~0x7;
        char*  char_ptr = (char*) pool;

        return (void*) &char_ptr[needed];
    }


    __device__
    void* closure_mul_allot (void* pool, const float3& w, ClosureColor* c)
    {
        ((ClosureMul*) pool)->id      = ClosureColor::MUL;
        ((ClosureMul*) pool)->weight  = w;
        ((ClosureMul*) pool)->closure = c;

        size_t needed   = (sizeof(ClosureMul) + 0x7) & ~0x7;
        char*  char_ptr = (char*) pool;

        return &char_ptr[needed];
    }


    __device__
    void* closure_mul_float_allot (void* pool, const float& w, ClosureColor* c)
    {
        ((ClosureMul*) pool)->id       = ClosureColor::MUL;
        ((ClosureMul*) pool)->weight.x = w;
        ((ClosureMul*) pool)->weight.y = w;
        ((ClosureMul*) pool)->weight.z = w;
        ((ClosureMul*) pool)->closure  = c;

        size_t needed   = (sizeof(ClosureMul) + 0x7) & ~0x7;
        char*  char_ptr = (char*) pool;

        return &char_ptr[needed];
    }


    __device__
    void* closure_add_allot (void* pool, ClosureColor* a, ClosureColor* b)
    {
        ((ClosureAdd*) pool)->id       = ClosureColor::ADD;
        ((ClosureAdd*) pool)->closureA = a;
        ((ClosureAdd*) pool)->closureB = b;

        size_t needed   = (sizeof(ClosureAdd) + 0x7) & ~0x7;
        char*  char_ptr = (char*) pool;

        return &char_ptr[needed];
    }


    __device__
    void* osl_allocate_closure_component (void* sg_, int id, int size)
    {
        ShaderGlobals* sg_ptr = (ShaderGlobals*) sg_;

        float3 w   = make_float3 (1.0f);
        void*  ret = sg_ptr->renderstate;

        size = max (4, size);

        sg_ptr->renderstate = closure_component_allot (sg_ptr->renderstate, id, size, w);

        return ret;
    }


    __device__
    void* osl_allocate_weighted_closure_component (void* sg_, int id, int size, const float3* w)
    {
        ShaderGlobals* sg_ptr = (ShaderGlobals*) sg_;

        if (w->x == 0.0f && w->y == 0.0f && w->z == 0.0f) {
            return NULL;
        }

        size = max (4, size);

        void* ret = sg_ptr->renderstate;
        sg_ptr->renderstate = closure_component_allot (sg_ptr->renderstate, id, size, *w);

        return ret;
    }


    __device__
    void* osl_mul_closure_color (void* sg_, ClosureColor* a, float3* w)
    {
        ShaderGlobals* sg_ptr = (ShaderGlobals*) sg_;

        if (a == NULL) {
            return NULL;
        }

        if (w->x == 0.0f && w->y == 0.0f && w->z == 0.0f) {
            return NULL;
        }

        if (w->x == 1.0f && w->y == 1.0f && w->z == 1.0f) {
            return a;
        }

        void* ret = sg_ptr->renderstate;
        sg_ptr->renderstate = closure_mul_allot (sg_ptr->renderstate, *w, a);

        return ret;
    }


    __device__
    void* osl_mul_closure_float (void* sg_, ClosureColor* a, float w)
    {
        ShaderGlobals* sg_ptr = (ShaderGlobals*) sg_;

        if (a == NULL || w == 0.0f) {
            return NULL;
        }

        if (w == 1.0f) {
            return a;
        }

        void* ret = sg_ptr->renderstate;
        sg_ptr->renderstate = closure_mul_float_allot (sg_ptr->renderstate, w, a);

        return ret;
    }


    __device__
    void* osl_add_closure_closure (void* sg_, ClosureColor* a, ClosureColor* b)
    {
        ShaderGlobals* sg_ptr = (ShaderGlobals*) sg_;

        if (a == NULL) {
            return b;
        }

        if (b == NULL) {
            return a;
        }

        void* ret = sg_ptr->renderstate;
        sg_ptr->renderstate = closure_add_allot (sg_ptr->renderstate, a, b);

        return ret;
    }


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

    using namespace OSL_NAMESPACE::pvt;

    __device__
    void osl_spline_vfv(float3 *out, const char *spline_, float *x, 
                        float3 *knots, int knot_count, int knot_arraylen)
    {
        Spline::SplineInterp::create(HDSTR(spline_))
            .evaluate<float3, float, float3, float3, false>
                (*out, *x, knots, knot_count, knot_arraylen);

    }
/*
    __device__
    void osl_spline_fff(float *out, const char *spline_, float *x, 
                        float *knots, int knot_count, int knot_arraylen)
    {
        Spline::SplineInterp::create(HDSTR(spline_))
            .evaluate<float, float, float, float, false>
                (*out, *x, knots, knot_count, knot_arraylen);
    }

    __device__
    void osl_splineinverse_fff(float *out, const char *spline_, float *x, 
                               float *knots, int knot_count, int knot_arraylen)
    {
        // Version with no derivs
        Spline::SplineInterp::create(HDSTR(spline_))
            .inverse<float>
                (*out, *x, knots, knot_count, knot_arraylen);
    }

    __device__
    void osl_splineinverse_dffdf(float *out, const char *spline_, float *x, 
                                 float *knots, int knot_count, int knot_arraylen)
    {
        // Ignore knot derivs
        float outtmp = 0;
        osl_splineinverse_fff (&outtmp, spline_, x, knots, knot_count, knot_arraylen);
        *out = outtmp;
    }
*/
}
