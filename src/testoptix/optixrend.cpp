/*
Copyright (c) 2009-2018 Sony Pictures Imageworks Inc., et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOTSS
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include "optixrend.h"
#include "raytracer.h"

#include "../liboslexec/splineimpl.h"

OSL_NAMESPACE_ENTER

/// Return true if the texture handle (previously returned by
/// get_texture_handle()) is a valid texture that can be subsequently
/// read or sampled.
bool OptixRenderer::good(TextureHandle *handle) {
    return intptr_t(handle) != RT_TEXTURE_ID_NULL;
}

/// Given the name of a texture, return an opaque handle that can be
/// used with texture calls to avoid the name lookups.
RendererServices::TextureHandle* OptixRenderer::get_texture_handle (ustring filename) {
    auto itr = m_samplers.find(filename);
    if (itr == m_samplers.end()) {
        optix::TextureSampler sampler = m_context->createTextureSampler();
        sampler->setWrapMode(0, RT_WRAP_REPEAT);
        sampler->setWrapMode(1, RT_WRAP_REPEAT);
        sampler->setWrapMode(2, RT_WRAP_REPEAT);

        sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
        sampler->setIndexingMode(false ? RT_TEXTURE_INDEX_ARRAY_INDEX : RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
        sampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);
        sampler->setMaxAnisotropy(1.0f);


        OIIO::ImageBuf image;
        if (!image.init_spec(filename, 0, 0)) {
            std::cerr << "Could not load:" << filename << "\n";
            return (TextureHandle*)(intptr_t(RT_TEXTURE_ID_NULL));
        }
        int nchan = image.spec().nchannels;

        OIIO::ROI roi = OIIO::get_roi_full(image.spec());
        int width = roi.width(), height = roi.height();
        std::vector<float> pixels(width * height * nchan);
        image.get_pixels(roi, OIIO::TypeDesc::FLOAT, pixels.data());

        optix::Buffer buffer = m_context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT4, width, height);

        float* device_ptr = static_cast<float*>(buffer->map());
        unsigned int pixel_idx = 0;
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                memcpy(device_ptr, &pixels[pixel_idx], sizeof(float) * nchan);
                device_ptr += 4;
                pixel_idx += nchan;
            }
        }
        buffer->unmap();
        sampler->setBuffer(buffer);
        itr = m_samplers.emplace(std::move(filename), std::move(sampler)).first;

    }
    return (RendererServices::TextureHandle*) intptr_t(itr->second->getId());
}

bool loadPtxFromFile (const std::string& progName, std::string& ptx_string)
{
    std::string filepath = std::string(PTX_PATH) + "/" + progName;
    if (OIIO::Filesystem::read_text_file (filepath, ptx_string))
        return true;

    std::cerr << "Unable to load '" << filepath << "'\n";
    return false;
}

bool OptixRenderer::init(const std::string& progName, int xres, int yres, Scene* scene)
{
    // Set up the OptiX context
    m_context = optix::Context::create();
    m_width  = xres;
    m_height = yres;

    ASSERT ((m_context->getEnabledDeviceCount() == 1) &&
            "Only one CUDA device is currently supported");

    m_context->setRayTypeCount (2);
    m_context->setEntryPointCount (1);
    m_context->setStackSize (2048);
    m_context->setPrintEnabled (true);

    // Load the renderer CUDA source and generate PTX for it
    std::string rendererPTX;
    if (! loadPtxFromFile(progName, rendererPTX))
        return false;

    // Create the OptiX programs and set them on the optix::Context
    m_program = m_context->createProgramFromPTXString(rendererPTX, "raygen");
    m_context->setRayGenerationProgram (0, m_program);

    // Set up the string table. This allocates a block of CUDA device memory to
    // hold all of the static strings used by the OSL shaders. The strings can
    // be accessed via OptiX variables that hold pointers to the table entries.
    m_str_table.init(m_context);

    {
        optix::Buffer buffer = m_context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_USER);
        buffer->setElementSize(sizeof(pvt::Spline::SplineBasis));
        buffer->setSize(sizeof(pvt::Spline::gBasisSet)/sizeof(pvt::Spline::SplineBasis));

        std::cout << "A: " << sizeof(pvt::Spline::SplineBasis) << "\n";
        std::cout << "B: " << sizeof(pvt::Spline::gBasisSet) << "\n";
        std::cout << "C: " << sizeof(pvt::Spline::gBasisSet)/sizeof(pvt::Spline::SplineBasis) << "\n";

        pvt::Spline::SplineBasis* basis = (pvt::Spline::SplineBasis*) buffer->map();
        ::memcpy(basis, &pvt::Spline::gBasisSet[0], sizeof(pvt::Spline::gBasisSet));
        buffer->unmap();
        m_context["gBasisSet"]->setBuffer(buffer);
    }

    if (scene) {
        m_context["radiance_ray_type"]->setUint  (0u);
        m_context["shadow_ray_type"  ]->setUint  (1u);
        m_context["bg_color"         ]->setFloat (0.0f, 0.0f, 0.0f);
        m_context["bad_color"        ]->setFloat (1.0f, 0.0f, 1.0f);

        // Create the OptiX programs and set them on the optix::Context
        m_context->setMissProgram          (0, m_context->createProgramFromPTXString (rendererPTX, "miss"));
        m_context->setExceptionProgram     (0, m_context->createProgramFromPTXString (rendererPTX, "exception"));

        // Load the PTX for the wrapper program. It will be used to create OptiX
        // Materials from the OSL ShaderGroups
        if (! loadPtxFromFile("wrapper.ptx", m_materials_ptx))
            return false;

        std::string sphere_ptx, quad_ptx;
        if (! loadPtxFromFile("sphere.ptx", sphere_ptx))
            return false;
        if (! loadPtxFromFile("quad.ptx", quad_ptx))
            return false;

        // Create the sphere and quad intersection programs, and save them on the
        // Scene so that they don't need to be regenerated for each primitive in the
        // scene
        scene->create_geom_programs (m_context, sphere_ptx, quad_ptx);
    }

    return static_cast<bool>(m_program);
}


bool OptixRenderer::finalize(ShadingSystem* shadingsys, bool saveptx, Scene* scene)
{
    int curMtl = 0;
    optix::Program closest_hit, any_hit;
    if (scene) {
        closest_hit = m_context->createProgramFromPTXString(m_materials_ptx, "closest_hit_osl");
        any_hit = m_context->createProgramFromPTXString(m_materials_ptx, "any_hit_shadow");
    }

    const char* outputs = "Cout";

    // Optimize each ShaderGroup in the scene, and use the resulting PTX to create
    // OptiX Programs which can be called by the closest hit program in the wrapper
    // to execute the compiled OSL shader.
    for (auto&& groupref : m_shaders) {
        if (!scene && outputs) {
            shadingsys->attribute (groupref.get(), "renderer_outputs", TypeDesc(TypeDesc::STRING, 1), &outputs);
        }

        shadingsys->optimize_group (groupref.get(), nullptr);

        if (!scene && outputs) {
            if (!shadingsys->find_symbol (*groupref.get(), ustring(outputs))) {
                std::cout << "Requested output '" << outputs << "', which wasn't found\n";
            }
        }

        std::string group_name, init_name, entry_name;
        shadingsys->getattribute (groupref.get(), "groupname",        group_name);
        shadingsys->getattribute (groupref.get(), "group_init_name",  init_name);
        shadingsys->getattribute (groupref.get(), "group_entry_name", entry_name);

        // Retrieve the compiled ShaderGroup PTX
        std::string osl_ptx;
        shadingsys->getattribute (groupref.get(), "ptx_compiled_version",
                                  OSL::TypeDesc::PTR, &osl_ptx);

        if (osl_ptx.empty()) {
            std::cerr << "Failed to generate PTX for ShaderGroup "
                      << group_name << std::endl;
            return false;
        }

        if (saveptx) {
            std::ofstream out (group_name + "_" + std::to_string(curMtl++) + ".ptx");
            out << osl_ptx;
            out.close();
        }

        if (scene) {
            // Create a new Material using the wrapper PTX
            optix::Material mtl = m_context->createMaterial();
            mtl->setClosestHitProgram (0, closest_hit);
            mtl->setAnyHitProgram (1, any_hit);

            scene->optix_mtls.push_back(mtl);
        }


        // Create Programs from the init and group_entry functions
        optix::Program osl_init = m_context->createProgramFromPTXString(osl_ptx, init_name);
        optix::Program osl_group = m_context->createProgramFromPTXString(osl_ptx, entry_name);

        // Set the OSL functions as Callable Programs so that they can be
        // executed by the closest hit program in the wrapper
        m_program["osl_init_func" ]->setProgramId (osl_init );
        m_program["osl_group_func"]->setProgramId (osl_group);
    }

    if (scene) {
        // Create a GeometryGroup to contain the scene geometry
        optix::GeometryGroup geom_group = m_context->createGeometryGroup();

        m_context["top_object"  ]->set (geom_group);
        m_context["top_shadower"]->set (geom_group);

        // NB: Since the scenes in the test suite consist of only a few primitives,
        //     using 'NoAccel' instead of 'Trbvh' might yield a slight performance
        //     improvement. For more complex scenes (e.g., scenes with meshes),
        //     using 'Trbvh' is recommended to achieve maximum performance.
        geom_group->setAcceleration (m_context->createAcceleration ("Trbvh"));

        // Translate the primitives parsed from the scene description into OptiX scene
        // objects
        for (const auto& sphere : scene->spheres) {
            optix::Geometry sphere_geom = m_context->createGeometry();
            sphere.setOptixVariables (sphere_geom, scene->sphere_bounds, scene->sphere_intersect);

            optix::GeometryInstance sphere_gi = m_context->createGeometryInstance (
                sphere_geom, &scene->optix_mtls[sphere.shaderid()], &scene->optix_mtls[sphere.shaderid()]+1);

            geom_group->addChild (sphere_gi);
        }

        for (const auto& quad : scene->quads) {
            optix::Geometry quad_geom = m_context->createGeometry();
            quad.setOptixVariables (quad_geom, scene->quad_bounds, scene->quad_intersect);

            optix::GeometryInstance quad_gi = m_context->createGeometryInstance (
                quad_geom, &scene->optix_mtls[quad.shaderid()], &scene->optix_mtls[quad.shaderid()]+1);

            geom_group->addChild (quad_gi);
        }

        // Set the camera variables on the OptiX Context, to be used by the ray gen program
        m_context["eye" ]->setFloat (vec3_to_float3 (scene->camera.eye));
        m_context["dir" ]->setFloat (vec3_to_float3 (scene->camera.dir));
        m_context["cx"  ]->setFloat (vec3_to_float3 (scene->camera.cx));
        m_context["cy"  ]->setFloat (vec3_to_float3 (scene->camera.cy));
        m_context["invw"]->setFloat (scene->camera.invw);
        m_context["invh"]->setFloat (scene->camera.invh);

        // Make some device strings to test userdata parameters
        uint64_t addr1 = register_string ("ud_str_1", "");
        uint64_t addr2 = register_string ("userdata string", "");
        m_context["test_str_1"]->setUserData (sizeof(char*), &addr1);
        m_context["test_str_2"]->setUserData (sizeof(char*), &addr2);
    }

printf("Rendering at %u x %u\n", m_width, m_height);

    // Create the output buffer
    optix::Buffer buffer = m_context->createBuffer(RT_BUFFER_OUTPUT, RT_FORMAT_FLOAT3, m_width, m_height);
    m_context["output_buffer"]->set(buffer);

    m_context["invw"]->setFloat (1.f / float(m_width));
    m_context["invh"]->setFloat (1.f / float(m_height));
    m_context->validate();

    return true;
}

std::vector<OSL::Color3>
OptixRenderer::getPixelBuffer(const std::string& buffer_name, int width, int height)
{
    const OSL::Color3* buffer_ptr =
        static_cast<OSL::Color3*>(m_context[buffer_name]->getBuffer()->map());

    if (! buffer_ptr) {
        std::cerr << "Unable to map buffer " << buffer_name << std::endl;
        exit (EXIT_FAILURE);
    }

    std::vector<OSL::Color3> pixels;
    std::copy (&buffer_ptr[0], &buffer_ptr[width * height], back_inserter(pixels));

    m_context[buffer_name]->getBuffer()->unmap();

    return pixels;
}

bool
OptixRenderer::saveImage(const std::string& buffer_name, int width, int height,
                         const std::string& imagefile, OIIO::ErrorHandler* errHandler)
{
    std::vector<OSL::Color3> pixels = getPixelBuffer("output_buffer", width, height);

    // Make an ImageBuf that wraps it ('pixels' still owns the memory)
    OIIO::ImageBuf pixelbuf(OIIO::ImageSpec(width, height, 3, TypeDesc::FLOAT), pixels.data());
    pixelbuf.set_write_format(TypeDesc::HALF);

    // Write image to disk
    if (OIIO::Strutil::iends_with(imagefile, ".jpg") ||
        OIIO::Strutil::iends_with(imagefile, ".jpeg") ||
        OIIO::Strutil::iends_with(imagefile, ".gif") ||
        OIIO::Strutil::iends_with(imagefile, ".png")) {
        // JPEG, GIF, and PNG images should be automatically saved as sRGB
        // because they are almost certainly supposed to be displayed on web
        // pages.
        OIIO::ImageBufAlgo::colorconvert(pixelbuf, pixelbuf, "linear", "sRGB", false, "", "");
    }

    pixelbuf.set_write_format (TypeDesc::HALF);
    if (pixelbuf.write(imagefile))
        return true;

    if (errHandler)
        errHandler->error("Unable to write output image: %s", pixelbuf.geterror().c_str());
    return false;
}

void
OptixRenderer::clear() {
    m_shaders.clear();
}

OSL_NAMESPACE_EXIT