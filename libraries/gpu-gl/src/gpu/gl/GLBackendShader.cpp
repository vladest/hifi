//
//  Created by Sam Gateau on 2017/04/13
//  Copyright 2013-2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "GLBackend.h"
#include "GLShader.h"
#include <gl/GLShaders.h>

using namespace gpu;
using namespace gpu::gl;

// GLSL version
std::string GLBackend::getBackendShaderHeader() const {
    return std::string("#version 410 core");
}

// Shader domain
static const size_t NUM_SHADER_DOMAINS = 3;

// GL Shader type enums
// Must match the order of type specified in gpu::Shader::Type
static const std::array<GLenum, NUM_SHADER_DOMAINS> SHADER_DOMAINS { {
    GL_VERTEX_SHADER,
    GL_FRAGMENT_SHADER,
    GL_GEOMETRY_SHADER,
} };

// Domain specific defines
// Must match the order of type specified in gpu::Shader::Type
static const std::array<std::string, NUM_SHADER_DOMAINS> DOMAIN_DEFINES { {
    "#define GPU_VERTEX_SHADER",
    "#define GPU_PIXEL_SHADER",
    "#define GPU_GEOMETRY_SHADER",
} };

// Stereo specific defines
static const std::string stereoVersion {
#ifdef GPU_STEREO_DRAWCALL_INSTANCED
    "#define GPU_TRANSFORM_IS_STEREO\n#define GPU_TRANSFORM_STEREO_CAMERA\n#define GPU_TRANSFORM_STEREO_CAMERA_INSTANCED\n#define GPU_TRANSFORM_STEREO_SPLIT_SCREEN"
#endif
#ifdef GPU_STEREO_DRAWCALL_DOUBLED
#ifdef GPU_STEREO_CAMERA_BUFFER
    "#define GPU_TRANSFORM_IS_STEREO\n#define GPU_TRANSFORM_STEREO_CAMERA\n#define GPU_TRANSFORM_STEREO_CAMERA_ATTRIBUTED"
#else
    "#define GPU_TRANSFORM_IS_STEREO"
#endif
#endif
};

// Versions specific of the shader
static const std::array<std::string, GLShader::NumVersions> VERSION_DEFINES { {
    "",
    stereoVersion
} };

GLShader* GLBackend::compileBackendShader(const Shader& shader) {
    // Any GLSLprogram ? normally yes...
    const std::string& shaderSource = shader.getSource().getCode();
    GLenum shaderDomain = SHADER_DOMAINS[shader.getType()];
    GLShader::ShaderObjects shaderObjects;

    for (int version = 0; version < GLShader::NumVersions; version++) {
        auto& shaderObject = shaderObjects[version];

        std::string shaderDefines = getBackendShaderHeader() + "\n" + DOMAIN_DEFINES[shader.getType()] + "\n" + VERSION_DEFINES[version];
        std::string error;

#ifdef SEPARATE_PROGRAM
        bool result = ::gl::compileShader(shaderDomain, shaderSource, shaderDefines, shaderObject.glshader, shaderObject.glprogram, error);
#else
        bool result = ::gl::compileShader(shaderDomain, shaderSource, shaderDefines, shaderObject.glshader, error);
#endif
        if (!result) {
            qCWarning(gpugllogging) << "GLBackend::compileBackendProgram - Shader didn't compile:\n" << error.c_str();
            return nullptr;
        }
    }

    // So far so good, the shader is created successfully
    GLShader* object = new GLShader(this->shared_from_this());
    object->_shaderObjects = shaderObjects;

    return object;
}

GLShader* GLBackend::compileBackendProgram(const Shader& program) {
    if (!program.isProgram()) {
        return nullptr;
    }

    GLShader::ShaderObjects programObjects;

    for (int version = 0; version < GLShader::NumVersions; version++) {
        auto& programObject = programObjects[version];

        // Let's go through every shaders and make sure they are ready to go
        std::vector< GLuint > shaderGLObjects;
        for (auto subShader : program.getShaders()) {
            auto object = GLShader::sync((*this), *subShader);
            if (object) {
                shaderGLObjects.push_back(object->_shaderObjects[version].glshader);
            } else {
                qCWarning(gpugllogging) << "GLBackend::compileBackendProgram - One of the shaders of the program is not compiled?";
                return nullptr;
            }
        }

        std::string error;
        GLuint glprogram = ::gl::compileProgram(shaderGLObjects, error);
        if (glprogram == 0) {
            qCWarning(gpugllogging) << "GLBackend::compileBackendProgram - Program didn't link:\n" << error.c_str();
            return nullptr;
        }

        programObject.glprogram = glprogram;

        makeProgramBindings(programObject);
    }

    // So far so good, the program versions have all been created successfully
    GLShader* object = new GLShader(this->shared_from_this());
    object->_shaderObjects = programObjects;

    return object;
}

GLBackend::ElementResource GLBackend::getFormatFromGLUniform(GLenum gltype) {
    switch (gltype) {
    case GL_FLOAT: return ElementResource(Element(SCALAR, gpu::FLOAT, UNIFORM), Resource::BUFFER);
    case GL_FLOAT_VEC2: return ElementResource(Element(VEC2, gpu::FLOAT, UNIFORM), Resource::BUFFER);
    case GL_FLOAT_VEC3: return ElementResource(Element(VEC3, gpu::FLOAT, UNIFORM), Resource::BUFFER);
    case GL_FLOAT_VEC4: return ElementResource(Element(VEC4, gpu::FLOAT, UNIFORM), Resource::BUFFER);
        /*
        case GL_DOUBLE: return ElementResource(Element(SCALAR, gpu::FLOAT, UNIFORM), Resource::BUFFER);
        case GL_DOUBLE_VEC2: return ElementResource(Element(VEC2, gpu::FLOAT, UNIFORM), Resource::BUFFER);
        case GL_DOUBLE_VEC3: return ElementResource(Element(VEC3, gpu::FLOAT, UNIFORM), Resource::BUFFER);
        case GL_DOUBLE_VEC4: return ElementResource(Element(VEC4, gpu::FLOAT, UNIFORM), Resource::BUFFER);
        */
    case GL_INT: return ElementResource(Element(SCALAR, gpu::INT32, UNIFORM), Resource::BUFFER);
    case GL_INT_VEC2: return ElementResource(Element(VEC2, gpu::INT32, UNIFORM), Resource::BUFFER);
    case GL_INT_VEC3: return ElementResource(Element(VEC3, gpu::INT32, UNIFORM), Resource::BUFFER);
    case GL_INT_VEC4: return ElementResource(Element(VEC4, gpu::INT32, UNIFORM), Resource::BUFFER);

    case GL_UNSIGNED_INT: return ElementResource(Element(SCALAR, gpu::UINT32, UNIFORM), Resource::BUFFER);
#if defined(Q_OS_WIN)
    case GL_UNSIGNED_INT_VEC2: return ElementResource(Element(VEC2, gpu::UINT32, UNIFORM), Resource::BUFFER);
    case GL_UNSIGNED_INT_VEC3: return ElementResource(Element(VEC3, gpu::UINT32, UNIFORM), Resource::BUFFER);
    case GL_UNSIGNED_INT_VEC4: return ElementResource(Element(VEC4, gpu::UINT32, UNIFORM), Resource::BUFFER);
#endif

    case GL_BOOL: return ElementResource(Element(SCALAR, gpu::BOOL, UNIFORM), Resource::BUFFER);
    case GL_BOOL_VEC2: return ElementResource(Element(VEC2, gpu::BOOL, UNIFORM), Resource::BUFFER);
    case GL_BOOL_VEC3: return ElementResource(Element(VEC3, gpu::BOOL, UNIFORM), Resource::BUFFER);
    case GL_BOOL_VEC4: return ElementResource(Element(VEC4, gpu::BOOL, UNIFORM), Resource::BUFFER);


    case GL_FLOAT_MAT2: return ElementResource(Element(gpu::MAT2, gpu::FLOAT, UNIFORM), Resource::BUFFER);
    case GL_FLOAT_MAT3: return ElementResource(Element(MAT3, gpu::FLOAT, UNIFORM), Resource::BUFFER);
    case GL_FLOAT_MAT4: return ElementResource(Element(MAT4, gpu::FLOAT, UNIFORM), Resource::BUFFER);

        /*    {GL_FLOAT_MAT2x3    mat2x3},
        {GL_FLOAT_MAT2x4    mat2x4},
        {GL_FLOAT_MAT3x2    mat3x2},
        {GL_FLOAT_MAT3x4    mat3x4},
        {GL_FLOAT_MAT4x2    mat4x2},
        {GL_FLOAT_MAT4x3    mat4x3},
        {GL_DOUBLE_MAT2    dmat2},
        {GL_DOUBLE_MAT3    dmat3},
        {GL_DOUBLE_MAT4    dmat4},
        {GL_DOUBLE_MAT2x3    dmat2x3},
        {GL_DOUBLE_MAT2x4    dmat2x4},
        {GL_DOUBLE_MAT3x2    dmat3x2},
        {GL_DOUBLE_MAT3x4    dmat3x4},
        {GL_DOUBLE_MAT4x2    dmat4x2},
        {GL_DOUBLE_MAT4x3    dmat4x3},
        */

    case GL_SAMPLER_1D: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER), Resource::TEXTURE_1D);
    case GL_SAMPLER_2D: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER), Resource::TEXTURE_2D);

    case GL_SAMPLER_3D: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER), Resource::TEXTURE_3D);
    case GL_SAMPLER_CUBE: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER), Resource::TEXTURE_CUBE);

#if defined(Q_OS_WIN)
    case GL_SAMPLER_2D_MULTISAMPLE: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER_MULTISAMPLE), Resource::TEXTURE_2D);
    case GL_SAMPLER_1D_ARRAY: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER), Resource::TEXTURE_1D_ARRAY);
    case GL_SAMPLER_2D_ARRAY: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER), Resource::TEXTURE_2D_ARRAY);
    case GL_SAMPLER_2D_MULTISAMPLE_ARRAY: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER_MULTISAMPLE), Resource::TEXTURE_2D_ARRAY);
#endif

    case GL_SAMPLER_2D_SHADOW: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER_SHADOW), Resource::TEXTURE_2D);
#if defined(Q_OS_WIN)
    case GL_SAMPLER_CUBE_SHADOW: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER_SHADOW), Resource::TEXTURE_CUBE);

    case GL_SAMPLER_2D_ARRAY_SHADOW: return ElementResource(Element(SCALAR, gpu::FLOAT, SAMPLER_SHADOW), Resource::TEXTURE_2D_ARRAY);
#endif

        //    {GL_SAMPLER_1D_SHADOW    sampler1DShadow},
        //   {GL_SAMPLER_1D_ARRAY_SHADOW    sampler1DArrayShadow},

    case GL_SAMPLER_BUFFER: return ElementResource(Element(SCALAR, gpu::FLOAT, RESOURCE_BUFFER), Resource::BUFFER);

        //    {GL_SAMPLER_2D_RECT    sampler2DRect},
        //   {GL_SAMPLER_2D_RECT_SHADOW    sampler2DRectShadow},

#if defined(Q_OS_WIN)
    case GL_INT_SAMPLER_1D: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER), Resource::TEXTURE_1D);
    case GL_INT_SAMPLER_2D: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER), Resource::TEXTURE_2D);
    case GL_INT_SAMPLER_2D_MULTISAMPLE: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER_MULTISAMPLE), Resource::TEXTURE_2D);
    case GL_INT_SAMPLER_3D: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER), Resource::TEXTURE_3D);
    case GL_INT_SAMPLER_CUBE: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER), Resource::TEXTURE_CUBE);

    case GL_INT_SAMPLER_1D_ARRAY: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER), Resource::TEXTURE_1D_ARRAY);
    case GL_INT_SAMPLER_2D_ARRAY: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER), Resource::TEXTURE_2D_ARRAY);
    case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY: return ElementResource(Element(SCALAR, gpu::INT32, SAMPLER_MULTISAMPLE), Resource::TEXTURE_2D_ARRAY);

        //   {GL_INT_SAMPLER_BUFFER    isamplerBuffer},
        //   {GL_INT_SAMPLER_2D_RECT    isampler2DRect},

    case GL_UNSIGNED_INT_SAMPLER_1D: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER), Resource::TEXTURE_1D);
    case GL_UNSIGNED_INT_SAMPLER_2D: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER), Resource::TEXTURE_2D);
    case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER_MULTISAMPLE), Resource::TEXTURE_2D);
    case GL_UNSIGNED_INT_SAMPLER_3D: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER), Resource::TEXTURE_3D);
    case GL_UNSIGNED_INT_SAMPLER_CUBE: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER), Resource::TEXTURE_CUBE);

    case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER), Resource::TEXTURE_1D_ARRAY);
    case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER), Resource::TEXTURE_2D_ARRAY);
    case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY: return ElementResource(Element(SCALAR, gpu::UINT32, SAMPLER_MULTISAMPLE), Resource::TEXTURE_2D_ARRAY);
#endif
        //    {GL_UNSIGNED_INT_SAMPLER_BUFFER    usamplerBuffer},
        //    {GL_UNSIGNED_INT_SAMPLER_2D_RECT    usampler2DRect},
        /*
        {GL_IMAGE_1D    image1D},
        {GL_IMAGE_2D    image2D},
        {GL_IMAGE_3D    image3D},
        {GL_IMAGE_2D_RECT    image2DRect},
        {GL_IMAGE_CUBE    imageCube},
        {GL_IMAGE_BUFFER    imageBuffer},
        {GL_IMAGE_1D_ARRAY    image1DArray},
        {GL_IMAGE_2D_ARRAY    image2DArray},
        {GL_IMAGE_2D_MULTISAMPLE    image2DMS},
        {GL_IMAGE_2D_MULTISAMPLE_ARRAY    image2DMSArray},
        {GL_INT_IMAGE_1D    iimage1D},
        {GL_INT_IMAGE_2D    iimage2D},
        {GL_INT_IMAGE_3D    iimage3D},
        {GL_INT_IMAGE_2D_RECT    iimage2DRect},
        {GL_INT_IMAGE_CUBE    iimageCube},
        {GL_INT_IMAGE_BUFFER    iimageBuffer},
        {GL_INT_IMAGE_1D_ARRAY    iimage1DArray},
        {GL_INT_IMAGE_2D_ARRAY    iimage2DArray},
        {GL_INT_IMAGE_2D_MULTISAMPLE    iimage2DMS},
        {GL_INT_IMAGE_2D_MULTISAMPLE_ARRAY    iimage2DMSArray},
        {GL_UNSIGNED_INT_IMAGE_1D    uimage1D},
        {GL_UNSIGNED_INT_IMAGE_2D    uimage2D},
        {GL_UNSIGNED_INT_IMAGE_3D    uimage3D},
        {GL_UNSIGNED_INT_IMAGE_2D_RECT    uimage2DRect},
        {GL_UNSIGNED_INT_IMAGE_CUBE    uimageCube},+        [0]    {_name="fInnerRadius" _location=0 _element={_semantic=15 '\xf' _dimension=0 '\0' _type=0 '\0' } }    gpu::Shader::Slot

        {GL_UNSIGNED_INT_IMAGE_BUFFER    uimageBuffer},
        {GL_UNSIGNED_INT_IMAGE_1D_ARRAY    uimage1DArray},
        {GL_UNSIGNED_INT_IMAGE_2D_ARRAY    uimage2DArray},
        {GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE    uimage2DMS},
        {GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY    uimage2DMSArray},
        {GL_UNSIGNED_INT_ATOMIC_COUNTER    atomic_uint}
        */
    default:
        return ElementResource(Element(), Resource::BUFFER);
    }

};

int GLBackend::makeUniformSlots(GLuint glprogram, const Shader::BindingSet& slotBindings,
    Shader::SlotSet& uniforms, Shader::SlotSet& textures, Shader::SlotSet& samplers) {
    GLint uniformsCount = 0;

    glGetProgramiv(glprogram, GL_ACTIVE_UNIFORMS, &uniformsCount);

    for (int i = 0; i < uniformsCount; i++) {
        const GLint NAME_LENGTH = 256;
        GLchar name[NAME_LENGTH];
        GLint length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(glprogram, i, NAME_LENGTH, &length, &size, &type, name);
        GLint location = glGetUniformLocation(glprogram, name);
        const GLint INVALID_UNIFORM_LOCATION = -1;

        // Try to make sense of the gltype
        auto elementResource = getFormatFromGLUniform(type);

        // The uniform as a standard var type
        if (location != INVALID_UNIFORM_LOCATION) {
            // Let's make sure the name doesn't contains an array element
            std::string sname(name);
            auto foundBracket = sname.find_first_of('[');
            if (foundBracket != std::string::npos) {
                //  std::string arrayname = sname.substr(0, foundBracket);

                if (sname[foundBracket + 1] == '0') {
                    sname = sname.substr(0, foundBracket);
                } else {
                    // skip this uniform since it's not the first element of an array
                    continue;
                }
            }

            if (elementResource._resource == Resource::BUFFER) {
                uniforms.insert(Shader::Slot(sname, location, elementResource._element, elementResource._resource));
            } else {
                // For texture/Sampler, the location is the actual binding value
                GLint binding = -1;
                glGetUniformiv(glprogram, location, &binding);

                auto requestedBinding = slotBindings.find(std::string(sname));
                if (requestedBinding != slotBindings.end()) {
                    if (binding != (*requestedBinding)._location) {
                        binding = (*requestedBinding)._location;
                        glProgramUniform1i(glprogram, location, binding);
                    }
                }

                textures.insert(Shader::Slot(name, binding, elementResource._element, elementResource._resource));
                samplers.insert(Shader::Slot(name, binding, elementResource._element, elementResource._resource));
            }
        }
    }

    return uniformsCount;
}

int GLBackend::makeUniformBlockSlots(GLuint glprogram, const Shader::BindingSet& slotBindings, Shader::SlotSet& buffers) {
    GLint buffersCount = 0;

    glGetProgramiv(glprogram, GL_ACTIVE_UNIFORM_BLOCKS, &buffersCount);

    // fast exit
    if (buffersCount == 0) {
        return 0;
    }

    GLint maxNumUniformBufferSlots = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxNumUniformBufferSlots);
    std::vector<GLint> uniformBufferSlotMap(maxNumUniformBufferSlots, -1);

    struct UniformBlockInfo {
        using Vector = std::vector<UniformBlockInfo>;
        const GLuint index{ 0 };
        const std::string name;
        GLint binding{ -1 };
        GLint size{ 0 };

        static std::string getName(GLuint glprogram, GLuint i) {
            static const GLint NAME_LENGTH = 256;
            GLint length = 0;
            GLchar nameBuffer[NAME_LENGTH];
            glGetActiveUniformBlockiv(glprogram, i, GL_UNIFORM_BLOCK_NAME_LENGTH, &length);
            glGetActiveUniformBlockName(glprogram, i, NAME_LENGTH, &length, nameBuffer);
            return std::string(nameBuffer);
        }

        UniformBlockInfo(GLuint glprogram, GLuint i) : index(i), name(getName(glprogram, i)) {
            glGetActiveUniformBlockiv(glprogram, index, GL_UNIFORM_BLOCK_BINDING, &binding);
            glGetActiveUniformBlockiv(glprogram, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
        }
    };

    UniformBlockInfo::Vector uniformBlocks;
    uniformBlocks.reserve(buffersCount);
    for (int i = 0; i < buffersCount; i++) {
        uniformBlocks.push_back(UniformBlockInfo(glprogram, i));
    }

    for (auto& info : uniformBlocks) {
        auto requestedBinding = slotBindings.find(info.name);
        if (requestedBinding != slotBindings.end()) {
            info.binding = (*requestedBinding)._location;
            glUniformBlockBinding(glprogram, info.index, info.binding);
            uniformBufferSlotMap[info.binding] = info.index;
        }
    }

    for (auto& info : uniformBlocks) {
        if (slotBindings.count(info.name)) {
            continue;
        }

        // If the binding is 0, or the binding maps to an already used binding
        if (info.binding == 0 || !isUnusedSlot(uniformBufferSlotMap[info.binding])) {
            // If no binding was assigned then just do it finding a free slot
            auto slotIt = std::find_if(uniformBufferSlotMap.begin(), uniformBufferSlotMap.end(), GLBackend::isUnusedSlot);
            if (slotIt != uniformBufferSlotMap.end()) {
                info.binding = slotIt - uniformBufferSlotMap.begin();
                glUniformBlockBinding(glprogram, info.index, info.binding);
            } else {
                // This should neve happen, an active ubo cannot find an available slot among the max available?!
                info.binding = -1;
            }
        }

        uniformBufferSlotMap[info.binding] = info.index;
    }

    for (auto& info : uniformBlocks) {
        static const Element element(SCALAR, gpu::UINT32, gpu::UNIFORM_BUFFER);
        buffers.insert(Shader::Slot(info.name, info.binding, element, Resource::BUFFER, info.size));
    }
    return buffersCount;
}

int GLBackend::makeInputSlots(GLuint glprogram, const Shader::BindingSet& slotBindings, Shader::SlotSet& inputs) {
    GLint inputsCount = 0;

    glGetProgramiv(glprogram, GL_ACTIVE_ATTRIBUTES, &inputsCount);

    for (int i = 0; i < inputsCount; i++) {
        const GLint NAME_LENGTH = 256;
        GLchar name[NAME_LENGTH];
        GLint length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveAttrib(glprogram, i, NAME_LENGTH, &length, &size, &type, name);

        GLint binding = glGetAttribLocation(glprogram, name);

        auto elementResource = getFormatFromGLUniform(type);
        inputs.insert(Shader::Slot(name, binding, elementResource._element, -1));
    }

    return inputsCount;
}

int GLBackend::makeOutputSlots(GLuint glprogram, const Shader::BindingSet& slotBindings, Shader::SlotSet& outputs) {
    /*   GLint outputsCount = 0;

    glGetProgramiv(glprogram, GL_ACTIVE_, &outputsCount);

    for (int i = 0; i < inputsCount; i++) {
    const GLint NAME_LENGTH = 256;
    GLchar name[NAME_LENGTH];
    GLint length = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveAttrib(glprogram, i, NAME_LENGTH, &length, &size, &type, name);

    auto element = getFormatFromGLUniform(type);
    outputs.insert(Shader::Slot(name, i, element));
    }
    */
    return 0; //inputsCount;
}

void GLBackend::makeProgramBindings(ShaderObject& shaderObject) {
    if (!shaderObject.glprogram) {
        return;
    }
    GLuint glprogram = shaderObject.glprogram;
    GLint loc = -1;

    //Check for gpu specific attribute slotBindings
    loc = glGetAttribLocation(glprogram, "inPosition");
    if (loc >= 0 && loc != gpu::Stream::POSITION) {
        glBindAttribLocation(glprogram, gpu::Stream::POSITION, "inPosition");
    }

    loc = glGetAttribLocation(glprogram, "inNormal");
    if (loc >= 0 && loc != gpu::Stream::NORMAL) {
        glBindAttribLocation(glprogram, gpu::Stream::NORMAL, "inNormal");
    }

    loc = glGetAttribLocation(glprogram, "inColor");
    if (loc >= 0 && loc != gpu::Stream::COLOR) {
        glBindAttribLocation(glprogram, gpu::Stream::COLOR, "inColor");
    }

    loc = glGetAttribLocation(glprogram, "inTexCoord0");
    if (loc >= 0 && loc != gpu::Stream::TEXCOORD) {
        glBindAttribLocation(glprogram, gpu::Stream::TEXCOORD, "inTexCoord0");
    }

    loc = glGetAttribLocation(glprogram, "inTangent");
    if (loc >= 0 && loc != gpu::Stream::TANGENT) {
        glBindAttribLocation(glprogram, gpu::Stream::TANGENT, "inTangent");
    }

    loc = glGetAttribLocation(glprogram, "inTexCoord1");
    if (loc >= 0 && loc != gpu::Stream::TEXCOORD1) {
        glBindAttribLocation(glprogram, gpu::Stream::TEXCOORD1, "inTexCoord1");
    }

    loc = glGetAttribLocation(glprogram, "inSkinClusterIndex");
    if (loc >= 0 && loc != gpu::Stream::SKIN_CLUSTER_INDEX) {
        glBindAttribLocation(glprogram, gpu::Stream::SKIN_CLUSTER_INDEX, "inSkinClusterIndex");
    }

    loc = glGetAttribLocation(glprogram, "inSkinClusterWeight");
    if (loc >= 0 && loc != gpu::Stream::SKIN_CLUSTER_WEIGHT) {
        glBindAttribLocation(glprogram, gpu::Stream::SKIN_CLUSTER_WEIGHT, "inSkinClusterWeight");
    }

    loc = glGetAttribLocation(glprogram, "_drawCallInfo");
    if (loc >= 0 && loc != gpu::Stream::DRAW_CALL_INFO) {
        glBindAttribLocation(glprogram, gpu::Stream::DRAW_CALL_INFO, "_drawCallInfo");
    }

    // Link again to take into account the assigned attrib location
    glLinkProgram(glprogram);

    GLint linked = 0;
    glGetProgramiv(glprogram, GL_LINK_STATUS, &linked);
    if (!linked) {
        qCWarning(gpugllogging) << "GLShader::makeBindings - failed to link after assigning slotBindings?";
    }
}

