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
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
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


#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/argparse.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/filesystem.h>

#include <pugixml.hpp>

#ifdef USING_OIIO_PUGI
namespace pugi = OIIO::pugi;
#endif

#include <OSL/oslexec.h>
#include <OSL/oslcomp.h>
#include <OSL/device_string.h>
#include <optix_world.h>

#include "../liboslexec/splineimpl.h"

using namespace optix;
#include "stringtable.h"

// The pre-compiled renderer support library LLVM bitcode is embedded into the
// testoptix executable and made available through these variables.
extern int rend_llvm_compiled_ops_size;
extern char rend_llvm_compiled_ops_block[];

using namespace OSL;

namespace { // anonymous namespace

class OptixRenderer : public RendererServices
{
public:
    // Just use 4x4 matrix for transformations
    typedef Matrix44 Transformation;

    OptixRenderer () { }
    ~OptixRenderer () { }

    // Function stubs
    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
                    TransformationPtr xform, float time) override
    {
        return 0;
    }

    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
                    ustring from, float time) override
    {
        return 0;
    }

    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
                    TransformationPtr xform) override
    {
        return 0;
    }

    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
                    ustring from) override
    {
        return 0;
    }

    bool get_inverse_matrix(ShaderGlobals *sg, Matrix44 &result,
                            ustring to, float time) override
    {
        return 0;
    }

    bool get_array_attribute(ShaderGlobals *sg, bool derivatives,
                             ustring object, TypeDesc type, ustring name,
                             int index, void *val) override
    {
        return 0;
    }

    bool get_attribute(ShaderGlobals *sg, bool derivatives, ustring object,
                       TypeDesc type, ustring name, void *val) override
    {
        return 0;
    }

    bool get_userdata(bool derivatives, ustring name, TypeDesc type,
                      ShaderGlobals *sg, void *val) override
    {
        return 0;
    }

    uint64_t register_string(const std::string& str, const std::string& var_name) override
    {
        return mStrTable.addString (ustring(str), ustring(var_name));
    }

    int supports(string_view feature) const override
    {
        if (feature == "OptiX")
            return true;

        return false;
    }

    /// Return true if the texture handle (previously returned by
    /// get_texture_handle()) is a valid texture that can be subsequently
    /// read or sampled.
    bool good(TextureHandle *handle) override {
        return int64_t(handle) != RT_TEXTURE_ID_NULL;
    }

    /// Given the name of a texture, return an opaque handle that can be
    /// used with texture calls to avoid the name lookups.
    TextureHandle * get_texture_handle(ustring filename) override;

    optix::Program init(int xres, int yres);

    void finalize();

    ShaderGroupRef loadShader(ShadingSystem* shadingsys, std::string shadername);
    void makeShader(ShadingSystem* shadingsys, ShaderGroupRef& groupref, bool saveptx);

    // Copies the specified device buffer into an output vector, assuming that
    // the buffer is in FLOAT3 format (and that Vec3 and float3 have the same
    // underlying representation).
    std::vector<OSL::Color3>
    getPixelBuffer(const std::string& buffer_name, int width, int height);

    bool
    saveImage(const std::string& buffer_name, int width, int height,
              const std::string& imagefile, OIIO::ErrorHandler* errHandler);

    optix::Context& operator -> () { return mContext; }

private:
    optix::Context        mContext;
    optix::Program        mProgram;
    std::unordered_map<OIIO::ustring, optix::TextureSampler, OIIO::ustringHash> mSamplers;
    StringTable           mStrTable;
    unsigned              mWidth, mHeight;
};

/// Given the name of a texture, return an opaque handle that can be
/// used with texture calls to avoid the name lookups.
RendererServices::TextureHandle* OptixRenderer::get_texture_handle (ustring filename) {
    auto itr = mSamplers.find(filename);
    if (itr == mSamplers.end()) {
        optix::TextureSampler sampler = mContext->createTextureSampler();
        sampler->setWrapMode(0, RT_WRAP_REPEAT);
        sampler->setWrapMode(1, RT_WRAP_REPEAT);
        sampler->setWrapMode(2, RT_WRAP_REPEAT);

        sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
        sampler->setIndexingMode(false ? RT_TEXTURE_INDEX_ARRAY_INDEX : RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
        sampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);
        sampler->setMaxAnisotropy(1.0f);


        OIIO::ImageBuf image;
        if (!image.init_spec(filename, 0, 0))
            return 0;
        int nchan = image.spec().nchannels;
        OIIO::ROI roi = OIIO::get_roi_full(image.spec());
        int width = roi.width(), height = roi.height();
        std::vector<float> pixels(width * height * nchan);
        image.get_pixels(roi, OIIO::TypeDesc::FLOAT, pixels.data());

        optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT4, width, height);

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
        itr = mSamplers.emplace(std::move(filename), std::move(sampler)).first;
    }
    return (RendererServices::TextureHandle*) intptr_t(itr->second->getId());
}

optix::Program OptixRenderer::init(int xres, int yres)
{
    // Set up the OptiX context
    mContext = optix::Context::create();
    mWidth  = xres;
    mHeight = yres;

    ASSERT ((mContext->getEnabledDeviceCount() == 1) &&
            "Only one CUDA device is currently supported");

    mContext->setRayTypeCount (2);
    mContext->setEntryPointCount (1);
    mContext->setStackSize (2048);
    mContext->setPrintEnabled (true);

    // Load the renderer CUDA source and generate PTX for it
    std::string rendererPTX;
    std::string filepath = std::string(PTX_PATH) + "/rast_renderer.ptx";
    if (! OIIO::Filesystem::read_text_file(filepath, rendererPTX)) {
        std::cerr << "Unable to load " << filepath << std::endl;
        exit (EXIT_FAILURE);
    }

    // Create the OptiX programs and set them on the optix::Context
    mProgram = mContext->createProgramFromPTXString(rendererPTX, "raygen");
    mContext->setRayGenerationProgram (0, mProgram);

    // Set up the string table. This allocates a block of CUDA device memory to
    // hold all of the static strings used by the OSL shaders. The strings can
    // be accessed via OptiX variables that hold pointers to the table entries.
    mStrTable.init(mContext);

    {
        optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_USER);
        buffer->setElementSize(sizeof(pvt::Spline::SplineBasis));
        buffer->setSize(sizeof(pvt::Spline::gBasisSet)/sizeof(pvt::Spline::SplineBasis));

        std::cout << "A: " << sizeof(pvt::Spline::SplineBasis) << "\n";
        std::cout << "B: " << sizeof(pvt::Spline::gBasisSet) << "\n";
        std::cout << "C: " << sizeof(pvt::Spline::gBasisSet)/sizeof(pvt::Spline::SplineBasis) << "\n";

        pvt::Spline::SplineBasis* basis = (pvt::Spline::SplineBasis*) buffer->map();
        ::memcpy(basis, &pvt::Spline::gBasisSet[0], sizeof(pvt::Spline::gBasisSet));
        buffer->unmap();
        mContext["gBasisSet"]->setBuffer(buffer);
    }

    return mProgram;
}

void OptixRenderer::finalize()
{
printf("Rendering at %u x %u\n", mWidth, mHeight);

    // Create the output buffer
    optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_OUTPUT, RT_FORMAT_FLOAT3, mWidth, mHeight);
    mContext["output_buffer"]->set(buffer);

    mContext["invw"]->setFloat (1.f / float(mWidth));
    mContext["invh"]->setFloat (1.f / float(mHeight));
    mContext->validate();
}

ShaderGroupRef OptixRenderer::loadShader(ShadingSystem* shadingsys, std::string shadername) {

    std::string oslfilename = shadername;
    if (! OIIO::Strutil::ends_with (oslfilename, ".osl"))
        oslfilename += ".osl";

    std::string sourcecode;
    if (! OIIO::Filesystem::read_text_file (oslfilename, sourcecode)) {
        std::cerr << "Could not open \"" << oslfilename << "\"\n";
        return ShaderGroupRef();
    }

    std::string osobuffer;
    OSLCompiler compiler;
    std::vector<std::string> options;

    if (! compiler.compile_buffer (sourcecode, osobuffer, options)) {
        std::cerr << "Could not compile \"" << shadername << "\"\n";
        return ShaderGroupRef();
    }

    if (! shadingsys->LoadMemoryCompiledShader (shadername, osobuffer)) {
        std::cerr << "Could not load compiled buffer from '" << shadername << "'\n";
        return ShaderGroupRef();
    }

    std::string group = shadername + "_group";
    ShaderGroupRef shader = shadingsys->ShaderGroupBegin(group);
    shadingsys->Shader(group, shadername, shadername + "_layer");
    return shader;
}

void OptixRenderer::makeShader(ShadingSystem* shadingsys, ShaderGroupRef& groupref, bool saveptx)
{
    // Optimize each ShaderGroup in the scene, and use the resulting PTX to create
    // OptiX Programs which can be called by the closest hit program in the wrapper
    // to execute the compiled OSL shader.

    const char* outputs[] = { "Cout" };

    shadingsys->attribute (groupref.get(), "renderer_outputs", TypeDesc(TypeDesc::STRING, 1), outputs);
//groupref.get()->param_storage (0);

    shadingsys->optimize_group (groupref.get(), nullptr);

std::cout << "Symbol: " << shadingsys->find_symbol (*groupref.get(), ustring("Cout")) << "\n";

    std::string group_name, init_name, entry_name;
    shadingsys->getattribute (groupref.get(), "groupname",        group_name);
    shadingsys->getattribute (groupref.get(), "group_init_name",  init_name);
    shadingsys->getattribute (groupref.get(), "group_entry_name", entry_name);

std::cout << "group_name: " << group_name << "\n";
std::cout << "group_init_name: " << init_name << "\n";

    // Retrieve the compiled ShaderGroup PTX
    std::string osl_ptx;
    shadingsys->getattribute (groupref.get(), "ptx_compiled_version",
                              OSL::TypeDesc::PTR, &osl_ptx);

    if (osl_ptx.empty()) {
        std::cerr << "Failed to generate PTX for ShaderGroup "
                  << group_name << std::endl;
        exit (EXIT_FAILURE);
    }

    if (saveptx) {
        static int sFileID = 0;
        std::ofstream out (group_name + "_" + std::to_string(sFileID++) + ".ptx");
        out << osl_ptx;
        out.close();
    }

    // Create Programs from the init and group_entry functions
    optix::Program osl_init = mContext->createProgramFromPTXString (
        osl_ptx, init_name);

    optix::Program osl_group = mContext->createProgramFromPTXString (
        osl_ptx, entry_name);

    // Set the OSL functions as Callable Programs so that they can be
    // executed by the closest hit program in the wrapper
    mProgram["osl_init_func" ]->setProgramId (osl_init );
    mProgram["osl_group_func"]->setProgramId (osl_group);
}

std::vector<OSL::Color3>
OptixRenderer::getPixelBuffer(const std::string& buffer_name, int width, int height)
{
    const OSL::Color3* buffer_ptr =
        static_cast<OSL::Color3*>(mContext[buffer_name]->getBuffer()->map());

    if (! buffer_ptr) {
        std::cerr << "Unable to map buffer " << buffer_name << std::endl;
        exit (EXIT_FAILURE);
    }

    std::vector<OSL::Color3> pixels;
    std::copy (&buffer_ptr[0], &buffer_ptr[width * height], back_inserter(pixels));

    mContext[buffer_name]->getBuffer()->unmap();

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

bool debug1 = false;
bool debug2 = false;
bool saveptx = false;
/*
bool verbose = false;
bool O0 = false, O1 = false, O2 = false;
bool debugnan = false;
int num_threads = 0;
*/

bool runstats = false;
bool warmup = false;
int profile = 0;
static std::string extraoptions;

std::vector<ShaderGroupRef> shaders;
static std::string shaderpath;

std::vector<std::string> gShaderFiles;

} // anonymous namespace

int add_shader(int argc, const char *argv[]) {
    ASSERT (argc == 1);
    gShaderFiles.emplace_back(argv[0]);
    return 0;
}

int main (int argc, const char *argv[])
{
    try {
        using namespace OIIO;
        Timer timer;
        ErrorHandler  errhandler;

        OptixRenderer rend;
        std::unique_ptr<ShadingSystem> shadingsys(new ShadingSystem (&rend, NULL, &errhandler));

        shadingsys->attribute ("lockgeom",           1);
        shadingsys->attribute ("debug",              0);
        shadingsys->attribute ("optimize",           2);
        shadingsys->attribute ("opt_simplify_param", 1);
        shadingsys->attribute ("range_checking",     0);

        // Setup common attributes
        shadingsys->attribute ("debug", debug2 ? 2 : (debug1 ? 1 : 0));
        shadingsys->attribute ("compile_report", debug1|debug2);

        std::vector<char> lib_bitcode;
        std::copy (&rend_llvm_compiled_ops_block[0],
                   &rend_llvm_compiled_ops_block[rend_llvm_compiled_ops_size],
                   back_inserter(lib_bitcode));

        shadingsys->attribute ("lib_bitcode", OSL::TypeDesc::UINT8, &lib_bitcode);

        if (! shaderpath.empty())
            shadingsys->attribute ("searchpath:shader", shaderpath);
        //else shadingsys->attribute ("searchpath:shader", OIIO::Filesystem::parent_path (scenefile));

        int xres = 256, yres = 256;
        {
            OIIO::ArgParse ap;
            std::vector<std::string> outputfiles;
            std::vector<std::string> outputvars;
            ap.options ("Usage:  testshade [options] shader...",
                        "%*", add_shader, "",
                        "-g %d %d", &xres, &yres, "Make an X x Y grid of shading points",
                        "-o %L %L", &outputvars, &outputfiles, "Output (variable, filename)",
                        nullptr);
            if (ap.parse(argc, argv) < 0 || gShaderFiles.empty()) {
                std::cerr << ap.geterror() << std::endl;
                ap.usage ();
                return EXIT_FAILURE;
            }

            for (auto&& file : gShaderFiles) {
                auto group = rend.loadShader(shadingsys.get(), std::move(file));
                if (!group)
                    return EXIT_FAILURE;
                shadingsys->ShaderGroupEnd();
                shaders.push_back (std::move(group));
            }

            if (shaders.empty()) {
                std::cerr << "No shaders loaded" << std::endl;
                return EXIT_FAILURE;
            }

            decltype(gShaderFiles)().swap(gShaderFiles);
        }

        // Set up the OptiX Context
        rend.init(xres, yres);

        // Convert the OSL ShaderGroups accumulated during scene parsing into
        // OptiX Materials
        rend.makeShader(shadingsys.get(), shaders.back(), saveptx);

        // Set up the OptiX scene graph
        rend.finalize();

        double setuptime = timer.lap ();

        // Perform a tiny launch to warm up the OptiX context
        if (warmup)
            rend->launch (0, 1, 1);

        double warmuptime = timer.lap ();

        // Launch the GPU kernel to render the scene
        rend->launch (0, xres, yres);
        double runtime = timer.lap ();

        // Copy the output image from the device buffer
        if (!rend.saveImage("output_buffer", xres, yres, "TEST_OUT.EXR", &errhandler))
            return EXIT_FAILURE;

        // Print some debugging info
        if (debug1 || runstats || profile) {
            double writetime = timer.lap();
            std::cout << "\n";
            std::cout << "Setup : " << OIIO::Strutil::timeintervalformat (setuptime,4) << "\n";
            if (warmup) {
                std::cout << "Warmup: " << OIIO::Strutil::timeintervalformat (warmuptime,4) << "\n";
            }
            std::cout << "Run   : " << OIIO::Strutil::timeintervalformat (runtime,4) << "\n";
            std::cout << "Write : " << OIIO::Strutil::timeintervalformat (writetime,4) << "\n";
            std::cout << "\n";
        }

        shaders.clear ();
        shadingsys.reset();

    } catch (optix::Exception e) {
        printf("orror: %s\n", e.what());
    }
    catch (std::exception e) {
        printf("error: %s\n", e.what());
    }
    return EXIT_SUCCESS;
}
