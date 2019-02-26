commit ded32ca5d454f344b82ed39a44d4b2e019fca50d
Author: rzulak <rzulak@imageworks.com>
Date:   Fri Feb 22 14:19:13 2019 -0800

    Baseline GPU spline code

diff --git a/src/testoptix/testoptix.cpp b/src/testoptix/testoptix.cpp
index 68410e66..ab9aff35 100644
--- a/src/testoptix/testoptix.cpp
+++ b/src/testoptix/testoptix.cpp
@@ -49,11 +49,12 @@ namespace pugi = OIIO::pugi;
 #endif
 
 #include <OSL/oslexec.h>
-
-#include "optixrend.h"
-
 #include <optix_world.h>
 
+#include "../liboslexec/splineimpl.h"
+
+using namespace optix;
+#include "stringtable.h"
 
 // The pre-compiled renderer support library LLVM bitcode is embedded into the
 // testoptix executable and made available through these variables.
@@ -62,35 +63,179 @@ extern char rend_llvm_compiled_ops_block[];
 
 using namespace OSL;
 
-optix::Context optix_ctx = NULL;
+namespace { // anonymous namespace
+
+class OptixRenderer : public RendererServices
+{
+public:
+    // Just use 4x4 matrix for transformations
+    typedef Matrix44 Transformation;
+
+    OptixRenderer () { }
+    ~OptixRenderer () { }
+
+    // Function stubs
+    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
+                    TransformationPtr xform, float time) override
+    {
+        return 0;
+    }
+
+    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
+                    ustring from, float time) override
+    {
+        return 0;
+    }
+
+    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
+                    TransformationPtr xform) override
+    {
+        return 0;
+    }
+
+    bool get_matrix(ShaderGlobals *sg, Matrix44 &result,
+                    ustring from) override
+    {
+        return 0;
+    }
+
+    bool get_inverse_matrix(ShaderGlobals *sg, Matrix44 &result,
+                            ustring to, float time) override
+    {
+        return 0;
+    }
+
+    bool get_array_attribute(ShaderGlobals *sg, bool derivatives,
+                             ustring object, TypeDesc type, ustring name,
+                             int index, void *val) override
+    {
+        return 0;
+    }
+
+    bool get_attribute(ShaderGlobals *sg, bool derivatives, ustring object,
+                       TypeDesc type, ustring name, void *val) override
+    {
+        return 0;
+    }
+
+    bool get_userdata(bool derivatives, ustring name, TypeDesc type,
+                      ShaderGlobals *sg, void *val) override
+    {
+        return 0;
+    }
+
+    uint64_t register_string(const std::string& str, const std::string& var_name) override
+    {
+        return mStrTable.addString (ustring(str), ustring(var_name));
+    }
+
+    int supports(string_view feature) const override
+    {
+        if (feature == "OptiX")
+            return true;
+
+        return false;
+    }
+
+    /// Return true if the texture handle (previously returned by
+    /// get_texture_handle()) is a valid texture that can be subsequently
+    /// read or sampled.
+    bool good(TextureHandle *handle) override {
+        return int64_t(handle) != RT_TEXTURE_ID_NULL;
+    }
+
+    /// Given the name of a texture, return an opaque handle that can be
+    /// used with texture calls to avoid the name lookups.
+    TextureHandle * get_texture_handle(ustring filename) override;
+
+    optix::Program init(int xres, int yres);
+
+    void finalize();
+
+    void makeImager(ShadingSystem* shadingsys, ShaderGroupRef& groupref, bool saveptx);
+
+    // Copies the specified device buffer into an output vector, assuming that
+    // the buffer is in FLOAT3 format (and that Vec3 and float3 have the same
+    // underlying representation).
+    std::vector<OSL::Color3>
+    getPixelBuffer(const std::string& buffer_name, int width, int height);
+
+    bool
+    saveImage(const std::string& buffer_name, int width, int height,
+              const std::string& imagefile, OIIO::ErrorHandler* errHandler);
+
+    optix::Context& operator -> () { return mContext; }
+
+private:
+    optix::Context        mContext;
+    optix::Program        mProgram;
+    std::unordered_map<OIIO::ustring, optix::TextureSampler, OIIO::ustringHash> mSamplers;
+    StringTable           mStrTable;
+    unsigned              mWidth, mHeight;
+};
+
+// "/shots/cae/zzz040/pix/rnd/zzz040_lgts_cobretenov_strokes_bty_strokes_keystill_noz_v1/2kdcf_cglnh_exr/zzz040_lgts_cobretenov_strokes_bty_strokes_keystill_noz_v1_2kdcf_cglnh.1001.exr"
+std::string kDirectory = 
+"/shots/cae/zzz040/pix/rnd/zzz040_lgts_cobretenov_strokes_bty_strokes_keyrotate_anim_v1";
+std::string kUV =
+"/aov_uv_2kdcf_nch_exr/zzz040_lgts_cobretenov_strokes_bty_strokes_keyrotate_anim_v1_aov_uv_2kdcf_nch";
+std::string kZ =
+"/aov_util_z_2kdcf_ncf_exr/zzz040_lgts_cobretenov_strokes_bty_strokes_keyrotate_anim_v1_aov_util_z_2kdcf_ncf";
+std::string kSurface =
+"/aov_util_surface_color_2kdcf_cglnh_exr/zzz040_lgts_cobretenov_strokes_bty_strokes_keyrotate_anim_v1_aov_util_surface_color_2kdcf_cglnh";
+std::string kDiffuse =
+"/aov_color_diffuse_2kdcf_cglnh_exr/zzz040_lgts_cobretenov_strokes_bty_strokes_keyrotate_anim_v1_aov_color_diffuse_2kdcf_cglnh";
 
 /// Given the name of a texture, return an opaque handle that can be
 /// used with texture calls to avoid the name lookups.
 RendererServices::TextureHandle* OptixRenderer::get_texture_handle (ustring filename) {
-    if (!m_sampler) {
-std::cout << "Want " << filename << "\n";
-        m_sampler = optix_ctx->createTextureSampler();
-        m_sampler->setWrapMode(0, RT_WRAP_REPEAT);
-        m_sampler->setWrapMode(1, RT_WRAP_REPEAT);
-        m_sampler->setWrapMode(2, RT_WRAP_REPEAT);
-
-        m_sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
-        m_sampler->setIndexingMode(false ? RT_TEXTURE_INDEX_ARRAY_INDEX : RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
-        m_sampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);
-        m_sampler->setMaxAnisotropy(1.0f);
-        
-        OIIO::ImageBuf image(filename);
-        if (!image.init_spec(filename, 0, 0))
+    auto itr = mSamplers.find(filename);
+    if (itr == mSamplers.end()) {
+        optix::TextureSampler sampler = mContext->createTextureSampler();
+        sampler->setWrapMode(0, RT_WRAP_REPEAT);
+        sampler->setWrapMode(1, RT_WRAP_REPEAT);
+        sampler->setWrapMode(2, RT_WRAP_REPEAT);
+
+        sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
+        sampler->setIndexingMode(false ? RT_TEXTURE_INDEX_ARRAY_INDEX : RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
+        sampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);
+        sampler->setMaxAnisotropy(1.0f);
+
+
+        OIIO::ImageBuf image;
+        int nchan = 0;
+        const bool isAOV = OIIO::Strutil::starts_with(filename, "output:");
+        if (isAOV) {
+            std::string path = kDirectory;
+                 if (filename == "output:UV")
+                path += kUV;
+            else if (filename == "output:Diffuse")
+                path += kDiffuse;
+            else if (filename == "output:Surface_Color")
+                path += kSurface;
+            else if (filename == "output:Z")
+                path += kZ;
+            path += ".1001.exr";
+
+std::cout << "Loading: '" << path << "'\n";
+
+            if (!image.init_spec(path, 0, 0))
+                return 0;
+            nchan = image.spec().nchannels;
+        }
+        else if (image.init_spec(filename, 0, 0))
+            nchan = image.spec().nchannels;
+        else
             return 0;
 
         OIIO::ROI roi = OIIO::get_roi_full(image.spec());
-        int width = roi.width(), height = roi.height(), nchan = image.spec().nchannels;
+        int width = roi.width(), height = roi.height();
         std::vector<float> pixels(width * height * nchan);
         image.get_pixels(roi, OIIO::TypeDesc::FLOAT, pixels.data());
 
-        optix::Buffer m_buffer = optix_ctx->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT4, width, height);
+        optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT4, width, height);
 
-        float* device_ptr = (float*)m_buffer->map();
+        float* device_ptr = static_cast<float*>(buffer->map());
         unsigned int pixel_idx = 0;
         for (unsigned y = 0; y < height; ++y) {
             for (unsigned x = 0; x < width; ++x) {
@@ -99,25 +244,185 @@ std::cout << "Want " << filename << "\n";
                 pixel_idx += nchan;
             }
         }
-        m_buffer->unmap();
-        m_sampler->setBuffer(m_buffer);
-std::cout << "Texture for " << filename << "\n";
+        buffer->unmap();
+        sampler->setBuffer(buffer);
+        itr = mSamplers.emplace(std::move(filename), std::move(sampler)).first;
+
+        if (isAOV) {
+            mWidth  = std::max(mWidth,  unsigned(width));
+            mHeight = std::max(mHeight, unsigned(height));
+        }
     }
-    return (RendererServices::TextureHandle*) intptr_t(m_sampler->getId());
+    return (RendererServices::TextureHandle*) intptr_t(itr->second->getId());
 }
 
-/// Return true if the texture handle (previously returned by
-/// get_texture_handle()) is a valid texture that can be subsequently
-/// read or sampled.
-bool OptixRenderer::good (TextureHandle *handle) {
-    if (!m_sampler)
-        return false;
-    return (intptr_t(m_sampler->getId())) == intptr_t(handle);
+optix::Program OptixRenderer::init(int xres, int yres)
+{
+    // Set up the OptiX context
+    mContext = optix::Context::create();
+    mWidth  = xres;
+    mHeight = yres;
+
+    ASSERT ((mContext->getEnabledDeviceCount() == 1) &&
+            "Only one CUDA device is currently supported");
+
+    mContext->setRayTypeCount (2);
+    mContext->setEntryPointCount (1);
+    mContext->setStackSize (2048);
+    mContext->setPrintEnabled (true);
+
+    // Load the renderer CUDA source and generate PTX for it
+    std::string rendererPTX;
+    std::string filepath = std::string(PTX_PATH) + "/renderer.ptx";
+    if (! OIIO::Filesystem::read_text_file(filepath, rendererPTX)) {
+        std::cerr << "Unable to load " << filepath << std::endl;
+        exit (EXIT_FAILURE);
+    }
+
+    // Create the OptiX programs and set them on the optix::Context
+    mProgram = mContext->createProgramFromPTXString(rendererPTX, "raygen");
+    mContext->setRayGenerationProgram (0, mProgram);
+
+    // Set up the string table. This allocates a block of CUDA device memory to
+    // hold all of the static strings used by the OSL shaders. The strings can
+    // be accessed via OptiX variables that hold pointers to the table entries.
+    mStrTable.init(mContext);
+
+    {
+        optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_USER);
+        buffer->setElementSize(sizeof(pvt::Spline::SplineBasis));
+        buffer->setSize(sizeof(pvt::Spline::gBasisSet)/sizeof(pvt::Spline::SplineBasis));
+
+        std::cout << "A: " << sizeof(pvt::Spline::SplineBasis) << "\n";
+        std::cout << "B: " << sizeof(pvt::Spline::gBasisSet) << "\n";
+        std::cout << "C: " << sizeof(pvt::Spline::gBasisSet)/sizeof(pvt::Spline::SplineBasis) << "\n";
+
+        pvt::Spline::SplineBasis* basis = (pvt::Spline::SplineBasis*) buffer->map();
+        ::memcpy(basis, &pvt::Spline::gBasisSet[0], sizeof(pvt::Spline::gBasisSet));
+        buffer->unmap();
+        mContext["gBasisSet"]->setBuffer(buffer);
+    }
+
+    return mProgram;
+}
+
+void OptixRenderer::finalize()
+{
+printf("Rendering at %u x %u\n", mWidth, mHeight);
+
+    // Create the output buffer
+    optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_OUTPUT, RT_FORMAT_FLOAT3, mWidth, mHeight);
+    mContext["output_buffer"]->set(buffer);
+
+    mContext["invw"]->setFloat (1.f / float(mWidth));
+    mContext["invh"]->setFloat (1.f / float(mHeight));
+    mContext->validate();
+}
+
+void OptixRenderer::makeImager(ShadingSystem* shadingsys, ShaderGroupRef& groupref, bool saveptx)
+{
+    // Optimize each ShaderGroup in the scene, and use the resulting PTX to create
+    // OptiX Programs which can be called by the closest hit program in the wrapper
+    // to execute the compiled OSL shader.
+
+    const char* outputs[] = { "Output_Color", "Output_Alpha" };
+
+    shadingsys->attribute (groupref.get(), "renderer_outputs", TypeDesc(TypeDesc::STRING, 2), outputs);
+//groupref.get()->param_storage (0);
+
+    shadingsys->optimize_group (groupref.get(), nullptr);
+
+std::cout << "Symbol: " << shadingsys->find_symbol (*groupref.get(), ustring("Output_Color")) << "\n";
+std::cout << "Symbol: " << shadingsys->find_symbol (*groupref.get(), ustring("Output_Alpha")) << "\n";
+
+    std::string group_name, init_name, entry_name;
+    shadingsys->getattribute (groupref.get(), "groupname",        group_name);
+    shadingsys->getattribute (groupref.get(), "group_init_name",  init_name);
+    shadingsys->getattribute (groupref.get(), "group_entry_name", entry_name);
+
+std::cout << "group_name: " << group_name << "\n";
+std::cout << "group_init_name: " << init_name << "\n";
+
+    // Retrieve the compiled ShaderGroup PTX
+    std::string osl_ptx;
+    shadingsys->getattribute (groupref.get(), "ptx_compiled_version",
+                              OSL::TypeDesc::PTR, &osl_ptx);
+
+    if (osl_ptx.empty()) {
+        std::cerr << "Failed to generate PTX for ShaderGroup "
+                  << group_name << std::endl;
+        exit (EXIT_FAILURE);
+    }
+
+    if (saveptx) {
+        static int sFileID = 0;
+        std::ofstream out (group_name + "_" + std::to_string(sFileID++) + ".ptx");
+        out << osl_ptx;
+        out.close();
+    }
+
+    // Create Programs from the init and group_entry functions
+    optix::Program osl_init = mContext->createProgramFromPTXString (
+        osl_ptx, init_name);
+
+    optix::Program osl_group = mContext->createProgramFromPTXString (
+        osl_ptx, entry_name);
+
+    // Set the OSL functions as Callable Programs so that they can be
+    // executed by the closest hit program in the wrapper
+    mProgram["osl_init_func" ]->setProgramId (osl_init );
+    mProgram["osl_group_func"]->setProgramId (osl_group);
+}
+
+std::vector<OSL::Color3>
+OptixRenderer::getPixelBuffer(const std::string& buffer_name, int width, int height)
+{
+    const OSL::Color3* buffer_ptr =
+        static_cast<OSL::Color3*>(mContext[buffer_name]->getBuffer()->map());
+
+    if (! buffer_ptr) {
+        std::cerr << "Unable to map buffer " << buffer_name << std::endl;
+        exit (EXIT_FAILURE);
+    }
+
+    std::vector<OSL::Color3> pixels;
+    std::copy (&buffer_ptr[0], &buffer_ptr[width * height], back_inserter(pixels));
+
+    mContext[buffer_name]->getBuffer()->unmap();
+
+    return pixels;
+}
+
+bool
+OptixRenderer::saveImage(const std::string& buffer_name, int width, int height,
+                         const std::string& imagefile, OIIO::ErrorHandler* errHandler)
+{
+    std::vector<OSL::Color3> pixels = getPixelBuffer("output_buffer", width, height);
+
+    // Make an ImageBuf that wraps it ('pixels' still owns the memory)
+    OIIO::ImageBuf pixelbuf(OIIO::ImageSpec(width, height, 3, TypeDesc::FLOAT), pixels.data());
+    pixelbuf.set_write_format(TypeDesc::HALF);
+
+    // Write image to disk
+    if (OIIO::Strutil::iends_with(imagefile, ".jpg") ||
+        OIIO::Strutil::iends_with(imagefile, ".jpeg") ||
+        OIIO::Strutil::iends_with(imagefile, ".gif") ||
+        OIIO::Strutil::iends_with(imagefile, ".png")) {
+        // JPEG, GIF, and PNG images should be automatically saved as sRGB
+        // because they are almost certainly supposed to be displayed on web
+        // pages.
+        OIIO::ImageBufAlgo::colorconvert(pixelbuf, pixelbuf, "linear", "sRGB", false, "", "");
+    }
+
+    pixelbuf.set_write_format (TypeDesc::HALF);
+    if (pixelbuf.write(imagefile))
+        return true;
+
+    if (errHandler)
+        errHandler->error("Unable to write output image: %s", pixelbuf.geterror().c_str());
+    return false;
 }
-    
-namespace { // anonymous namespace
 
-ShadingSystem *shadingsys = NULL;
 bool debug1 = false;
 bool debug2 = false;
 bool verbose = false;
@@ -128,12 +433,9 @@ int profile = 0;
 bool O0 = false, O1 = false, O2 = false;
 bool debugnan = false;
 static std::string extraoptions;
-int xres = 640, yres = 480, aa = 1, max_bounces = 1000000, rr_depth = 5;
+int xres = 1998, yres = 1080, aa = 1;
 int num_threads = 0;
-ErrorHandler errhandler;
-OptixRenderer rend;  // RendererServices
-int backgroundShaderID = -1;
-int backgroundResolution = 0;
+
 std::vector<ShaderGroupRef> shaders;
 std::string scenefile, imagefile;
 static std::string shaderpath;
@@ -142,9 +444,6 @@ static std::string shaderpath;
 //     testrender can be used as-is (and they will eventually be used, when
 //     path tracing is added to testoptix)
 
-static std::string renderer_ptx;  // ray generation, etc.
-
-
 
 int get_filenames(int argc, const char *argv[])
 {
@@ -157,7 +456,7 @@ int get_filenames(int argc, const char *argv[])
     return 0;
 }
 
-void getargs(int argc, const char *argv[])
+void getargs(int argc, const char *argv[], ErrorHandler& errhandler)
 {
     bool help = false;
     OIIO::ArgParse ap;
@@ -206,22 +505,6 @@ void getargs(int argc, const char *argv[])
         errhandler.verbosity (ErrorHandler::VERBOSE);
 }
 
-Vec3 strtovec(string_view str) {
-    Vec3 v(0, 0, 0);
-    OIIO::Strutil::parse_float (str, v[0]);
-    OIIO::Strutil::parse_char (str, ',');
-    OIIO::Strutil::parse_float (str, v[1]);
-    OIIO::Strutil::parse_char (str, ',');
-    OIIO::Strutil::parse_float (str, v[2]);
-    return v;
-}
-
-bool strtobool(const char* str) {
-    return strcmp(str, "1") == 0 ||
-           strcmp(str, "on") == 0 ||
-           strcmp(str, "yes") == 0;
-}
-
 template <int N>
 struct ParamStorage {
     ParamStorage() : fparamindex(0), iparamindex(0), sparamindex(0) {}
@@ -265,7 +548,7 @@ private:
 };
 
 
-void parse_scene() {
+void parse_scene(ShadingSystem *shadingsys) {
     // setup default camera (now that resolution is finalized)
 
     // load entire text file into a buffer
@@ -294,23 +577,7 @@ void parse_scene() {
     }
     // loop over all children of world
     for (pugi::xml_node node = root.first_child(); node; node = node.next_sibling()) {
-        if (strcmp(node.name(), "Option") == 0) {
-            for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
-                int i = 0;
-                if (sscanf(attr.value(), " int %d ", &i) == 1) {
-                    if (strcmp(attr.name(), "max_bounces") == 0)
-                        max_bounces = i;
-                    else if (strcmp(attr.name(), "rr_depth") == 0)
-                        rr_depth = i;
-                }
-                // TODO: pass any extra options to shading system (or texture system?)
-            }
-        } else if (strcmp(node.name(), "Background") == 0) {
-            pugi::xml_attribute res_attr = node.attribute("resolution");
-            if (res_attr)
-                backgroundResolution = OIIO::Strutil::from_string<int>(res_attr.value());
-            backgroundShaderID = int(shaders.size()) - 1;
-        } else if (strcmp(node.name(), "ShaderGroup") == 0) {
+        if (strcmp(node.name(), "ShaderGroup") == 0) {
             ShaderGroupRef group;
             pugi::xml_attribute name_attr = node.attribute("name");
             std::string name = name_attr? name_attr.value() : "group";
@@ -363,8 +630,6 @@ void parse_scene() {
             }
             shadingsys->ShaderGroupEnd();
             shaders.push_back (group);
-        } else {
-            // unknown element?
         }
     }
     if (root.next_sibling()) {
@@ -374,250 +639,98 @@ void parse_scene() {
     }
 }
 
-
-// Copies the specified device buffer into an output vector, assuming that
-// the buffer is in FLOAT3 format (and that Vec3 and float3 have the same
-// underlying representation).
-std::vector<OSL::Color3>
-get_pixel_buffer (const std::string& buffer_name, int width, int height)
-{
-    const OSL::Color3* buffer_ptr =
-        static_cast<OSL::Color3*>(optix_ctx[buffer_name]->getBuffer()->map());
-
-    if (! buffer_ptr) {
-        std::cerr << "Unable to map buffer " << buffer_name << std::endl;
-        exit (EXIT_FAILURE);
-    }
-
-    std::vector<OSL::Color3> pixels;
-    std::copy (&buffer_ptr[0], &buffer_ptr[width * height], back_inserter(pixels));
-
-    optix_ctx[buffer_name]->getBuffer()->unmap();
-
-    return pixels;
-}
-
-
-void load_ptx_from_file (std::string& ptx_string, const char* filename)
-{
-    if (! OIIO::Filesystem::read_text_file (filename, ptx_string)) {
-        std::cerr << "Unable to load " << filename << std::endl;
-        exit (EXIT_FAILURE);
-    }
-}
-
-
-optix::Program init_optix_context ()
-{
-    // Set up the OptiX context
-    optix_ctx = optix::Context::create();
-
-    ASSERT ((optix_ctx->getEnabledDeviceCount() == 1) &&
-            "Only one CUDA device is currently supported");
-
-    optix_ctx->setRayTypeCount (2);
-    optix_ctx->setEntryPointCount (1);
-    optix_ctx->setStackSize (2048);
-    optix_ctx->setPrintEnabled (true);
-
-    optix_ctx["radiance_ray_type"]->setUint  (0u);
-    optix_ctx["shadow_ray_type"  ]->setUint  (1u);
-    optix_ctx["bg_color"         ]->setFloat (0.0f, 0.0f, 0.0f);
-    optix_ctx["bad_color"        ]->setFloat (1.0f, 0.0f, 1.0f);
-
-    // Create the output buffer
-    optix::Buffer buffer = optix_ctx->createBuffer (RT_BUFFER_OUTPUT,
-                                                    RT_FORMAT_FLOAT3,
-                                                    xres, yres);
-    optix_ctx["output_buffer"]->set (buffer);
-
-    // Load the renderer CUDA source and generate PTX for it
-    std::string filename = std::string(PTX_PATH) + "/renderer.ptx";
-    load_ptx_from_file (renderer_ptx, filename.c_str());
-
-    // Create the OptiX programs and set them on the optix::Context
-    optix::Program raygen = optix_ctx->createProgramFromPTXString (renderer_ptx, "raygen");
-    optix_ctx->setRayGenerationProgram (0, raygen);
-
-    return raygen;
-}
-
-
-void make_optix_materials (optix::Program& program)
-{
-    int mtl_id = 0;
-    // Optimize each ShaderGroup in the scene, and use the resulting PTX to create
-    // OptiX Programs which can be called by the closest hit program in the wrapper
-    // to execute the compiled OSL shader.
-    for (const auto& groupref : shaders) {
-
-const char* outputs[] = { "Color_OutFirst", "Color_Out" };
-shadingsys->attribute (groupref.get(), "renderer_outputs", TypeDesc(TypeDesc::STRING, 2), outputs);
-    //groupref.get()->param_storage (0);
-
-        shadingsys->optimize_group (groupref.get(), nullptr);
-
-    std::cout << "Symbol: " << shadingsys->find_symbol (*groupref.get(), ustring("Color_Out")) << "\n";
-
-        std::string group_name, init_name, entry_name;
-        shadingsys->getattribute (groupref.get(), "groupname",        group_name);
-        shadingsys->getattribute (groupref.get(), "group_init_name",  init_name);
-        shadingsys->getattribute (groupref.get(), "group_entry_name", entry_name);
-
-std::cout << "group_name: " << group_name << "\n";
-std::cout << "group_init_name: " << init_name << "\n";
-
-        // Retrieve the compiled ShaderGroup PTX
-        std::string osl_ptx;
-        shadingsys->getattribute (groupref.get(), "ptx_compiled_version",
-                                  OSL::TypeDesc::PTR, &osl_ptx);
-
-        if (osl_ptx.empty()) {
-            std::cerr << "Failed to generate PTX for ShaderGroup "
-                      << group_name << std::endl;
-            exit (EXIT_FAILURE);
-        }
-
-        if (saveptx) {
-            std::ofstream out (group_name + "_" + std::to_string( mtl_id++ ) + ".ptx");
-            out << osl_ptx;
-            out.close();
-        }
-
-        // Create Programs from the init and group_entry functions
-        optix::Program osl_init = optix_ctx->createProgramFromPTXString (
-            osl_ptx, init_name);
-
-        optix::Program osl_group = optix_ctx->createProgramFromPTXString (
-            osl_ptx, entry_name);
-
-        // Set the OSL functions as Callable Programs so that they can be
-        // executed by the closest hit program in the wrapper
-        program["osl_init_func" ]->setProgramId (osl_init );
-        program["osl_group_func"]->setProgramId (osl_group);
-
-        return;
-    }
-}
-
-
-void finalize_scene ()
-{
-    optix_ctx["invw"]->setFloat (1.f / xres);
-    optix_ctx["invh"]->setFloat (1.f / yres);
-    optix_ctx->validate();
-}
-
 } // anonymous namespace
 
 
 int main (int argc, const char *argv[])
 {
-try {
-    using namespace OIIO;
-    Timer timer;
-
-    // Read command line arguments
-    getargs (argc, argv);
-
-    shadingsys = new ShadingSystem (&rend, NULL, &errhandler);
-
-    shadingsys->attribute ("lockgeom",           1);
-    shadingsys->attribute ("debug",              0);
-    shadingsys->attribute ("optimize",           2);
-    shadingsys->attribute ("opt_simplify_param", 1);
-    shadingsys->attribute ("range_checking",     0);
-
-    // Setup common attributes
-    shadingsys->attribute ("debug", debug2 ? 2 : (debug1 ? 1 : 0));
-    shadingsys->attribute ("compile_report", debug1|debug2);
-
-    std::vector<char> lib_bitcode;
-    std::copy (&rend_llvm_compiled_ops_block[0],
-               &rend_llvm_compiled_ops_block[rend_llvm_compiled_ops_size],
-               back_inserter(lib_bitcode));
-
-    shadingsys->attribute ("lib_bitcode", OSL::TypeDesc::UINT8, &lib_bitcode);
-
-    if (! shaderpath.empty())
-        shadingsys->attribute ("searchpath:shader", shaderpath);
-    else
-        shadingsys->attribute ("searchpath:shader", OIIO::Filesystem::parent_path (scenefile));
-
-    // Loads a scene, creating camera, geometry and assigning shaders
-    parse_scene();
+    try {
+        using namespace OIIO;
+        Timer timer;
+        ErrorHandler  errhandler;
+
+        // Read command line arguments
+        getargs (argc, argv, errhandler);
+
+        OptixRenderer rend;
+        std::unique_ptr<ShadingSystem> shadingsys(new ShadingSystem (&rend, NULL, &errhandler));
+
+        shadingsys->attribute ("lockgeom",           1);
+        shadingsys->attribute ("debug",              0);
+        shadingsys->attribute ("optimize",           2);
+        shadingsys->attribute ("opt_simplify_param", 1);
+        shadingsys->attribute ("range_checking",     0);
+
+        // Setup common attributes
+        shadingsys->attribute ("debug", debug2 ? 2 : (debug1 ? 1 : 0));
+        shadingsys->attribute ("compile_report", debug1|debug2);
+
+        std::vector<char> lib_bitcode;
+        std::copy (&rend_llvm_compiled_ops_block[0],
+                   &rend_llvm_compiled_ops_block[rend_llvm_compiled_ops_size],
+                   back_inserter(lib_bitcode));
+
+        shadingsys->attribute ("lib_bitcode", OSL::TypeDesc::UINT8, &lib_bitcode);
+
+        if (! shaderpath.empty())
+            shadingsys->attribute ("searchpath:shader", shaderpath);
+        else
+            shadingsys->attribute ("searchpath:shader", OIIO::Filesystem::parent_path (scenefile));
+
+        // Loads a scene, creating camera, geometry and assigning shaders
+        parse_scene(shadingsys.get());
+        if (shaders.empty()) {
+            std::cout << "No shaders in scene\n";
+            return EXIT_FAILURE;
+        }
 
-    // Set up the OptiX Context
-    optix::Program raygen = init_optix_context();
+        // Set up the OptiX Context
+        rend.init(xres, yres);
 
-    // Set up the string table. This allocates a block of CUDA device memory to
-    // hold all of the static strings used by the OSL shaders. The strings can
-    // be accessed via OptiX variables that hold pointers to the table entries.
-    rend.init_string_table(optix_ctx);
+        // Convert the OSL ShaderGroups accumulated during scene parsing into
+        // OptiX Materials
+        rend.makeImager(shadingsys.get(), shaders.back(), false);
 
-    // Convert the OSL ShaderGroups accumulated during scene parsing into
-    // OptiX Materials
-    make_optix_materials(raygen);
+        // Set up the OptiX scene graph
+        rend.finalize();
 
-    // Set up the OptiX scene graph
-    finalize_scene();
+        double setuptime = timer.lap ();
 
-    double setuptime = timer.lap ();
+        // Perform a tiny launch to warm up the OptiX context
+        if (warmup)
+            rend->launch (0, 1, 1);
 
-    // Perform a tiny launch to warm up the OptiX context
-    if (warmup)
-        optix_ctx->launch (0, 1, 1);
+        double warmuptime = timer.lap ();
 
-    double warmuptime = timer.lap ();
+        // Launch the GPU kernel to render the scene
+        rend->launch (0, xres, yres);
+        double runtime = timer.lap ();
 
-    // Launch the GPU kernel to render the scene
-    optix_ctx->launch (0, xres, yres);
-    double runtime = timer.lap ();
+        // Copy the output image from the device buffer
+        if (!rend.saveImage("output_buffer", xres, yres, imagefile, &errhandler))
+            return EXIT_FAILURE;
 
-    // Copy the output image from the device buffer
-    std::vector<OSL::Color3> pixels = get_pixel_buffer ("output_buffer", xres, yres);
+        // Print some debugging info
+        if (debug1 || runstats || profile) {
+            double writetime = timer.lap();
+            std::cout << "\n";
+            std::cout << "Setup : " << OIIO::Strutil::timeintervalformat (setuptime,4) << "\n";
+            if (warmup) {
+                std::cout << "Warmup: " << OIIO::Strutil::timeintervalformat (warmuptime,4) << "\n";
+            }
+            std::cout << "Run   : " << OIIO::Strutil::timeintervalformat (runtime,4) << "\n";
+            std::cout << "Write : " << OIIO::Strutil::timeintervalformat (writetime,4) << "\n";
+            std::cout << "\n";
+        }
 
-    // Make an ImageBuf that wraps it ('pixels' still owns the memory)
-    ImageBuf pixelbuf (ImageSpec(xres, yres, 3, TypeDesc::FLOAT), pixels.data());
-    pixelbuf.set_write_format (TypeDesc::HALF);
+        shaders.clear ();
+        shadingsys.reset();
 
-    // Write image to disk
-    if (Strutil::iends_with (imagefile, ".jpg") ||
-        Strutil::iends_with (imagefile, ".jpeg") ||
-        Strutil::iends_with (imagefile, ".gif") ||
-        Strutil::iends_with (imagefile, ".png")) {
-        // JPEG, GIF, and PNG images should be automatically saved as sRGB
-        // because they are almost certainly supposed to be displayed on web
-        // pages.
-        ImageBufAlgo::colorconvert (pixelbuf, pixelbuf,
-                                    "linear", "sRGB", false, "", "");
+    } catch (optix::Exception e) {
+        printf("orror: %s\n", e.what());
     }
-    pixelbuf.set_write_format (TypeDesc::HALF);
-    if (! pixelbuf.write (imagefile))
-        errhandler.error ("Unable to write output image: %s",
-                          pixelbuf.geterror().c_str());
-
-    printf("WROTE IT\n");
-    // Print some debugging info
-    if (debug1 || runstats || profile) {
-        double writetime = timer.lap();
-        std::cout << "\n";
-        std::cout << "Setup : " << OIIO::Strutil::timeintervalformat (setuptime,4) << "\n";
-        if (warmup) {
-            std::cout << "Warmup: " << OIIO::Strutil::timeintervalformat (warmuptime,4) << "\n";
-        }
-        std::cout << "Run   : " << OIIO::Strutil::timeintervalformat (runtime,4) << "\n";
-        std::cout << "Write : " << OIIO::Strutil::timeintervalformat (writetime,4) << "\n";
-        std::cout << "\n";
+    catch (std::exception e) {
+        printf("error: %s\n", e.what());
     }
-
-    shaders.clear ();
-    delete shadingsys;
-} catch (optix::Exception e) {
-    printf("orror: %s\n", e.what());
-}
-catch (std::exception e) {
-    printf("error: %s\n", e.what());
-}
     return EXIT_SUCCESS;
 }
