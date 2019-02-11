#include <optix.h>
#include <optix_device.h>
#include <optix_math.h>

#include "rend_lib.h"


rtDeclareVariable (uint2, launch_index, rtLaunchIndex, );
rtDeclareVariable (uint2, launch_dim,   rtLaunchDim, );


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
    int rend_get_userdata (char* name, void* data, int data_size)
    {
        return 0;
    }


    __device__
    int osl_bind_interpolated_param (void *sg_, const void *name, long long type,
                                     int userdata_has_derivs, void *userdata_data,
                                     int symbol_has_derivs, void *symbol_data,
                                     int symbol_data_size,
                                     char *userdata_initialized, int userdata_index)
    {
        int layer = 0;
        return rend_get_userdata ((char*)name, symbol_data, symbol_data_size);
    }


    __device__
    int osl_strlen_is (const char *str)
    {
        return DEVSTR(str).length();
    }


    __device__
    int osl_hash_is (const char *str)
    {
        return DEVSTR(str).hash();
    }


    __device__
    int osl_getchar_isi (const char *str, int index)
    {
        return (str && unsigned(index) < DEVSTR(str).length())
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
