#include <emucorev/paf/page_turn_shaders.h>
#include <emucorev/paf/page_turn_renderer.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Context {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    Context() {
        EGLint major, minor, count;
        if (!eglInitialize(display, &major, &minor)) throw std::runtime_error("EGL initialization");
        constexpr EGLint config_attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || !count)
            throw std::runtime_error("EGL config");
        constexpr EGLint attributes[]{EGL_WIDTH, 128, EGL_HEIGHT, 64, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, attributes);
        constexpr EGLint version[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
        if (!eglMakeCurrent(display, surface, surface, context)) throw std::runtime_error("EGL activation");
    }
    ~Context() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    }
};
GLuint compile(GLenum type, const std::string& source) {
    auto result = glCreateShader(type);
    const auto* text = source.c_str();
    glShaderSource(result, 1, &text, nullptr); glCompileShader(result);
    GLint ok; glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 8192> log{};
        glGetShaderInfoLog(result, log.size(), nullptr, log.data());
        throw std::runtime_error(log.data());
    }
    return result;
}
GLuint program(const std::string& fragment) {
    const auto vs = compile(GL_VERTEX_SHADER, R"(#version 320 es
precision highp float;
layout(location=4) out vec4 v_TexCoord0;
layout(location=5) out vec4 v_TexCoord1;
layout(location=6) out vec4 v_TexCoord2;
uniform float test_shift;
void main() {
    vec2 uv=vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
    v_TexCoord0=vec4(uv,uv.x*1.7-0.25+test_shift,1.0);
    v_TexCoord1=vec4(uv*4.0-2.0,0.0,1.0);
    v_TexCoord2=vec4(uv,uv.x*.6+.2,uv.y*.7+.1+test_shift);
})");
    const auto fs = compile(GL_FRAGMENT_SHADER, fragment);
    const auto result = glCreateProgram();
    glAttachShader(result, vs); glAttachShader(result, fs); glLinkProgram(result);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) throw std::runtime_error("Page-turn shader link failed");
    return result;
}
void check_mesh_renderer() {
    using namespace emucorev::paf;
    // Compile actual pairs together, not only with diagnostic counterparts:
    // stage-local material arrays must not collide during program linking.
    PageTurnRenderer native_materials(
        {page_turn_bottom_vertex_shader(),page_turn_bottom_fragment_shader()},
        {page_turn_top_vertex_shader(),page_turn_top_fragment_shader()},
        {page_turn_quad_vertex_shader(),page_turn_shadow_fragment_shader()});
    PageTurnRenderer rounded_materials(
        {page_turn_bottom_round_vertex_shader(),page_turn_bottom_round_fragment_shader()},
        {page_turn_top_round_vertex_shader(),page_turn_top_round_fragment_shader()});
    PageTurnRenderer utility_materials(
        {page_turn_empty_back_vertex_shader(),page_turn_empty_back_fragment_shader()},
        {page_turn_quad_vertex_shader(),page_turn_copy_fragment_shader()});
    constexpr auto vertex = R"(#version 320 es
precision highp float;
layout(location=0) in vec2 point;
void main() { gl_Position=vec4(point.x-1.0, point.y*2.0-1.0, 0.0, 1.0); }
)";
    constexpr auto red = R"(#version 320 es
precision highp float;
layout(location=0) out vec4 color;
void main() { color=vec4(1.0,0.0,0.0,1.0); }
)";
    constexpr auto blue = R"(#version 320 es
precision highp float;
layout(location=0) out vec4 color;
void main() { color=vec4(0.0,0.0,1.0,1.0); }
)";
    PageTurnRenderer renderer({vertex,red},{vertex,blue},
        {page_turn_quad_vertex_shader(),page_turn_shadow_fragment_shader()});
    PageSurfaceDraw material{};
    PageTurnMesh mesh;
    std::array<unsigned char,128*64*4> pixels;
    unsigned checked = 0;
    for (auto deepest : std::array<PagePoint,4>{{{1.9f,.95f},{1.1f,.4f},{-.8f,-.3f},{-3.f,-2.f}}}) {
        const auto shape = page_turn_fold_shape(deepest,{2,1});
        page_turn_mesh(shape,mesh); renderer.upload_mesh(mesh);
        glClearColor(0,1,0,0); glClear(GL_COLOR_BUFFER_BIT);
        renderer.draw(PageSurface::front,material); renderer.draw(PageSurface::folded,material);
        glReadPixels(0,0,128,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Mesh renderer GPU error");
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x) {
            const float distance = ((x+.5f)/64.f-shape.center.x)*shape.normal.x
                + ((y+.5f)/64.f-shape.center.y)*shape.normal.y;
            if(std::abs(distance)<.00001f) continue;
            const auto i=(y*128+x)*4;
            if(pixels[i]!=(distance>=0?255:0) || pixels[i+1]!=0
                || pixels[i+2]!=(distance<0?255:0) || pixels[i+3]!=255)
                throw std::runtime_error("Mesh renderer leaves a hole or draws the wrong page side");
            ++checked;
        }
    }
    std::cout << "PASS page surface fan submission: " << checked << " pixel centers / four folds\n";
    const std::array<float,20> quad_parameters{1,0,0,0,0,2,0,0,0,0,1,0,-1,-1,0,1,0,0,1,1};
    const std::array<float,8> shadow_parameters{0,0,std::bit_cast<float>(0x3c00u),0,.25f,.5f,.75f,1};
    GLuint black;
    glGenTextures(1,&black); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,black);
    const std::array<unsigned char,4> texel{0,0,0,255};
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texel.data());
    material.vertex_parameters=quad_parameters; material.fragment_parameters=shadow_parameters;
    material.viewport={1,1,1,1,1,128,64,.5f,.5f,0,0,0};
    material.fragment_state={0,0,0,0,1,0,0,1.f/128,1.f/64,0,16,0};
    material.textures[0]=black;
    const std::array<PageQuadVertex,4> quad{{{{0,0},{0,0}},{{2,0},{1,0}},{{0,1},{0,1}},{{2,1},{1,1}}}};
    renderer.draw_quad(quad,material);
    glReadPixels(0,0,128,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Shadow rectangle GPU error");
    for(unsigned i=0;i<pixels.size();++i)
        if(pixels[i]!=std::array<unsigned char,4>{64,128,191,255}[i%4])
            throw std::runtime_error("Shadow rectangle does not cover the target with the expected material");
    glDeleteTextures(1,&black);
    std::cout << "PASS native shadow rectangle: 8192 expected RGBA pixels\n";
}
enum class VertexKind { top, bottom, quad, bottom_round, top_round, empty_back };
void check_vertex(const char* reference_path, VertexKind kind = VertexKind::top,
    const char* lookup_path = nullptr) {
    using namespace emucorev::paf;
    const bool bottom=kind==VertexKind::bottom || kind==VertexKind::bottom_round;
    const bool quad=kind==VertexKind::quad;
    const bool empty=kind==VertexKind::empty_back;
    const bool rounded=kind==VertexKind::bottom_round || kind==VertexKind::top_round || empty;
    const unsigned stride=(rounded && !bottom && !empty)?16:rounded?12:(bottom || quad)?8:12;
    auto build = [&](const std::string& source) {
        const auto vs = compile(GL_VERTEX_SHADER,source);
        const auto fs = compile(GL_FRAGMENT_SHADER,"#version 320 es\nprecision highp float; layout(location=0) out vec4 c; void main(){c=vec4(1.0);}");
        const auto shader=glCreateProgram();
        glAttachShader(shader,vs); glAttachShader(shader,fs);
        const char* outputs[]{"gl_Position","v_TexCoord0",rounded?"v_TexCoord1":"v_TexCoord2","v_TexCoord2"};
        glTransformFeedbackVaryings(shader,(rounded && !bottom && !empty)?4:rounded?3:(bottom || quad)?2:3,outputs,GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(shader); glDeleteShader(vs); glDeleteShader(fs);
        GLint ok=0; glGetProgramiv(shader,GL_LINK_STATUS,&ok);
        if(!ok) throw std::runtime_error("Top vertex transform-feedback link failed");
        const auto sampler=glGetUniformLocation(shader,"vertTex_ExpMinusTexture");
        if(sampler>=0) { glUseProgram(shader); glUniform1i(sampler,4); }
        return shader;
    };
    const std::array sources{page_turn_top_vertex_shader,page_turn_bottom_vertex_shader,page_turn_quad_vertex_shader,
        page_turn_bottom_round_vertex_shader,page_turn_top_round_vertex_shader,page_turn_empty_back_vertex_shader};
    const auto native=build(std::string(sources[static_cast<unsigned>(kind)]()));
    GLuint reference=0;
    if(reference_path) {
        std::ifstream file(reference_path,std::ios::binary);
        if(!file) throw std::runtime_error("Missing top vertex reference");
        std::string source{std::istreambuf_iterator<char>(file),{}};
        const std::string old="layout(binding = 0, std430) restrict readonly buffer vertexDataType";
        const auto at=source.find(old);
        if(at==std::string::npos) throw std::runtime_error("Unexpected top vertex reference block");
        source.replace(at,old.size(),"layout(binding = 0, std140) uniform vertexDataType");
        reference=build(source);
    }
    GLuint vao, buffers[4], feedback;
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(4,buffers); glGenTransformFeedbacks(1,&feedback);
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,feedback);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
    glBufferData(GL_ARRAY_BUFFER,1024*4*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
    glBindBuffer(GL_UNIFORM_BUFFER,buffers[1]);
    glBufferData(GL_UNIFORM_BUFFER,112*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,buffers[1]);
    glBindBuffer(GL_UNIFORM_BUFFER,buffers[2]);
    glBufferData(GL_UNIFORM_BUFFER,48,nullptr,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,2,buffers[2]);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[3]);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,1024*64,nullptr,GL_DYNAMIC_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffers[3]);
    GLuint lookup;
    glGenTextures(1,&lookup); glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D,lookup);
    std::vector<unsigned char> lookup_pixels;
    if(lookup_path) {
        std::ifstream file(lookup_path,std::ios::binary);
        if(!file) throw std::runtime_error("Missing external lookup");
        lookup_pixels.assign(std::istreambuf_iterator<char>(file),{});
        if(lookup_pixels.size()!=128*8) throw std::runtime_error("Expected 128x8 scalar lookup bytes");
    } else {
        lookup_pixels.resize(64);
        for(unsigned i=0;i<64;++i) lookup_pixels[i]=static_cast<unsigned char>((i*71+17)%256);
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,lookup_path?128:64,lookup_path?8:1,0,GL_RED,GL_UNSIGNED_BYTE,lookup_pixels.data());
    glEnable(GL_RASTERIZER_DISCARD);
    PageTurnMesh mesh;
    unsigned compared=0,differences=0,nonfinite=0;
    std::array<unsigned,16> component_differences{};
    std::array<float,16> component_maximum{};
    float maximum=0;
    for(auto deepest:std::array<PagePoint,4>{{{1.9f,.95f},{1.1f,.4f},{-.8f,-.3f},{-3.f,-2.f}}}) {
        const auto shape=page_turn_fold_shape(deepest,{2,1});
        page_turn_mesh(shape,mesh);
        std::vector<std::array<float,4>> points(mesh.vertex_count);
        for(unsigned i=0;i<mesh.vertex_count;++i)
            points[i]={mesh.vertices[i].x,mesh.vertices[i].y,float(i%7)*.1f,float(i%11)*.13f};
        glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
        glBufferSubData(GL_ARRAY_BUFFER,0,points.size()*sizeof(points[0]),points.data());
        for(unsigned configuration=0;configuration<3;++configuration) {
            std::array<float,112> p{};
            p[0]=p[5]=p[10]=p[15]=1;
            p[16]=p[21]=p[26]=p[31]=1;
            p[36]=.9f; p[41]=.2f; p[42]=.8f;
            p[48]=p[49]=1;
            p[52]=p[53]=1; p[54]=1.f+configuration*.2f; p[55]=1;
            p[56]=shape.minimum.distance; p[58]=deepest.x; p[59]=deepest.y;
            p[60]=shape.minimum.on_fold.x; p[61]=shape.minimum.on_fold.y; p[62]=3.3f;
            p[64]=.23f; p[65]=.6f; p[66]=.45f;
            p[68]=shape.center.x; p[69]=shape.center.y; p[70]=shape.normal.x; p[71]=shape.normal.y;
            p[72]=2; p[73]=1; p[74]=.5f; p[75]=1;
            p[84]=.8f; p[85]=.6f; p[86]=.3f;
            p[92]=2; p[93]=3; p[94]=4;
            p[108]=1; p[109]=2; p[110]=5;
            if(bottom) {
                std::fill(p.begin()+16,p.end(),0.f);
                p[16]=250; p[17]=.3f;
                p[20]=p[21]=1;
                p[24]=p[25]=1; p[26]=1.f+configuration*.2f; p[27]=1;
                p[28]=lookup_path?5.f:0.f; p[29]=lookup_path?.2f:1.f;
                p[32]=deepest.x; p[33]=deepest.y;
                p[34]=shape.minimum.on_fold.x; p[35]=shape.minimum.on_fold.y;
                p[36]=.009f;
                p[40]=shape.center.x; p[41]=shape.center.y; p[42]=shape.normal.x; p[43]=shape.normal.y;
                p[44]=2; p[45]=1; p[46]=.5f; p[47]=1;
            }
            if(quad) {
                p[12]=-.1f*configuration; p[13]=.2f*configuration;
                p[16]=.2f; p[17]=.3f; p[18]=1.1f; p[19]=.7f;
            }
            if(empty) {
                std::fill(p.begin()+16,p.end(),0.f);
                p[18]=16.2f; p[19]=.48f;
                p[20]=p[21]=1;
                p[24]=p[25]=p[26]=p[27]=1;
                p[28]=shape.center.x; p[29]=shape.center.y; p[30]=shape.normal.x; p[31]=shape.normal.y;
                p[32]=2; p[33]=1; p[34]=.5f; p[35]=1;
            }
            const std::array<float,12> viewport{1,configuration==2?-1.f:1.f,1,1,
                configuration==1?0.f:1.f,128,64,.5f,.5f,0,0,0};
            glBindBuffer(GL_UNIFORM_BUFFER,buffers[1]); glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(p),p.data());
            glBindBuffer(GL_UNIFORM_BUFFER,buffers[2]); glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(viewport),viewport.data());
            auto capture=[&](GLuint shader) {
                glUseProgram(shader); glBeginTransformFeedback(GL_POINTS);
                glDrawArrays(GL_POINTS,0,mesh.vertex_count); glEndTransformFeedback();
                glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[3]);
                const auto size=mesh.vertex_count*stride*sizeof(float);
                const auto* data=static_cast<const float*>(glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,size,GL_MAP_READ_BIT));
                if(!data) throw std::runtime_error("Cannot read top vertex feedback");
                std::vector<float> result(data,data+mesh.vertex_count*stride);
                glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
                if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Top vertex feedback error");
                return result;
            };
            const auto actual=capture(native);
            for(float value:actual) nonfinite+=!std::isfinite(value);
            if(reference) {
                const auto original=capture(reference);
                for(unsigned i=0;i<actual.size();++i) {
                    differences+=std::bit_cast<unsigned>(actual[i])!=std::bit_cast<unsigned>(original[i]);
                    component_differences[i%stride]+=std::bit_cast<unsigned>(actual[i])!=std::bit_cast<unsigned>(original[i]);
                    component_maximum[i%stride]=std::max(component_maximum[i%stride],std::abs(actual[i]-original[i]));
                    maximum=std::max(maximum,std::abs(actual[i]-original[i])); ++compared;
                }
            }
        }
    }
    glDisable(GL_RASTERIZER_DISCARD); glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,0);
    glDeleteTransformFeedbacks(1,&feedback); glDeleteBuffers(4,buffers); glDeleteVertexArrays(1,&vao);
    glDeleteTextures(1,&lookup);
    glDeleteProgram(native); if(reference) glDeleteProgram(reference);
    const std::array labels{"Top","Bottom","Quad","Bottom rounded","Top rounded","Empty back"};
    std::cout << labels[static_cast<unsigned>(kind)] << " vertex: " << nonfinite << " nonfinite outputs, " << compared << " compared words, "
        << differences << " differences, max " << maximum << '\n';
    if(lookup_path) std::cout << "Used external 128x8 scalar lookup, range 5\n";
    if(differences) for(unsigned i=0;i<stride;++i)
        std::cout << "component " << i << ": " << component_differences[i] << " max " << component_maximum[i] << '\n';
    if(nonfinite || differences) throw std::runtime_error("Page vertex comparison failed");
}
}
int main(int argc, char** argv) {
    try {
        const char* lookup_path=nullptr;
        if(argc==5 && std::string(argv[3])=="--lookup-r8") { lookup_path=argv[4]; argc=3; }
        const bool top_only=argc==3 && std::string(argv[1])=="--top-vertex";
        const bool bottom_only=argc==3 && std::string(argv[1])=="--bottom-vertex";
        const bool quad_only=argc==3 && std::string(argv[1])=="--quad-vertex";
        const bool round_vertex=argc==3 && std::string(argv[1])=="--bottom-round-vertex";
        const bool top_round_vertex=argc==3 && std::string(argv[1])=="--top-round-vertex";
        const bool empty_vertex=argc==3 && std::string(argv[1])=="--empty-back-vertex";
        const bool bottom_fragment=argc==3 && std::string(argv[1])=="--bottom-fragment";
        const bool top_fragment=argc==3 && std::string(argv[1])=="--top-fragment";
        const bool shadow_fragment=argc==3 && std::string(argv[1])=="--shadow-fragment";
        const bool round_fragment=argc==3 && std::string(argv[1])=="--bottom-round-fragment";
        const bool top_round_fragment=argc==3 && std::string(argv[1])=="--top-round-fragment";
        const bool copy_fragment=argc==3 && std::string(argv[1])=="--copy-fragment";
        if(lookup_path && !bottom_only && !round_vertex) throw std::runtime_error("External lookup applies only to bottom vertex modes");
        if (argc > 2 && !top_only && !bottom_only && !quad_only && !round_vertex && !top_round_vertex && !empty_vertex && !bottom_fragment && !top_fragment && !shadow_fragment && !round_fragment && !top_round_fragment && !copy_fragment) throw std::runtime_error("Unknown page shader comparison mode");
        Context context;
        std::cout << std::unitbuf << "GPU " << glGetString(GL_RENDERER) << '\n';
        if(top_only || bottom_only || quad_only || round_vertex || top_round_vertex || empty_vertex) {
            check_vertex(argv[2],empty_vertex?VertexKind::empty_back:top_round_vertex?VertexKind::top_round:
                round_vertex?VertexKind::bottom_round:quad_only?VertexKind::quad:bottom_only?VertexKind::bottom:VertexKind::top,lookup_path);
            return 0;
        }
        check_vertex(nullptr);
        check_vertex(nullptr,VertexKind::bottom);
        check_vertex(nullptr,VertexKind::quad);
        check_vertex(nullptr,VertexKind::bottom_round);
        check_vertex(nullptr,VertexKind::top_round);
        check_vertex(nullptr,VertexKind::empty_back);
        const auto native = program(std::string(copy_fragment?emucorev::paf::page_turn_copy_fragment_shader():top_round_fragment?emucorev::paf::page_turn_top_round_fragment_shader():round_fragment?emucorev::paf::page_turn_bottom_round_fragment_shader():shadow_fragment?emucorev::paf::page_turn_shadow_fragment_shader():top_fragment?emucorev::paf::page_turn_top_fragment_shader():bottom_fragment?emucorev::paf::page_turn_bottom_fragment_shader():emucorev::paf::page_turn_empty_back_fragment_shader()));
        GLuint reference = 0;
        if (argc == 2 || bottom_fragment || top_fragment || shadow_fragment || round_fragment || top_round_fragment || copy_fragment) {
            std::ifstream file(argv[argc==3?2:1], std::ios::binary);
            if (!file) throw std::runtime_error("Missing reference shader");
            std::string source{std::istreambuf_iterator<char>(file), {}};
            const std::string old = "layout(binding = 1, std430) restrict readonly buffer fragmentDataType";
            const auto at = source.find(old);
            if (at == std::string::npos && !bottom_fragment && !copy_fragment) throw std::runtime_error("Unexpected reference parameter block");
            if (at != std::string::npos) source.replace(at, old.size(), "layout(binding = 1, std140) uniform fragmentDataType");
            reference = program(source);
        }
        GLuint vao, buffers[2], textures[2], framebuffer, output;
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(2, buffers);
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[0]);
        glBufferData(GL_UNIFORM_BUFFER, 160, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, buffers[0]);
        const std::array<float, 12> state{0,0,0,0,1,0,0,1.f/128,1.f/64,0,112,0};
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[1]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(state), state.data(), GL_STATIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, buffers[1]);
        glGenTextures(2, textures);
        std::array<unsigned char, 32*32*4> pixels;
        for (unsigned t = 0; t < 2; ++t) {
            for (unsigned i = 0; i < pixels.size(); ++i)
                pixels[i] = static_cast<unsigned char>((i*37 + (i/4)*13 + t*79) % 256);
            glActiveTexture(GL_TEXTURE0 + t); glBindTexture(GL_TEXTURE_2D, textures[t]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        }
        glGenTextures(1, &output); glBindTexture(GL_TEXTURE_2D, output);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 128,64);
        glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output,0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete RGBA target");
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, textures[1]);
        glViewport(0,0,128,64); glDisable(GL_BLEND); glDisable(GL_DITHER);
        std::array<unsigned char, 128*64*4> original{}, actual{};
        unsigned differences = 0, nonzero = 0, saturated = 0, reported = 0;
        std::array<unsigned,4> channel_differences{}, maximum{};
        const unsigned cases=copy_fragment?1:16;
        for (unsigned sample = 0; sample < cases; ++sample) {
            const float rate = static_cast<float>(sample)/15;
            std::array<float,40> material{.2f,.3f,.8f,1.2f,rate,0,.7f,.9f,
                1.1f,.25f+rate,.1f,.2f,.3f,0,1.f+rate,.7f};
            if(top_fragment) {
                material[0]=rate; material[2]=.6f; material[3]=.7f;
                material[4]=.8f; material[5]=.9f;
                material[18]=.4f; material[19]=.3f; material[20]=.2f; material[21]=.1f;
            }
            if(round_fragment) {
                material[4]=(rate-.5f)*.15f;
                material[5]=.03f+rate*.07f;
            }
            if(top_round_fragment) {
                material[4]=rate; material[6]=.6f; material[7]=.7f;
                material[8]=.8f; material[9]=.9f;
                material[22]=.4f; material[23]=.3f; material[24]=.2f; material[25]=.1f;
            }
            if(shadow_fragment) {
                auto packed=[](float a,float b) {
                    return std::bit_cast<float>(unsigned(std::bit_cast<std::uint16_t>(static_cast<_Float16>(a)))
                        | (unsigned(std::bit_cast<std::uint16_t>(static_cast<_Float16>(b)))<<16));
                };
                material[0]=packed(.1f,.2f); material[1]=packed(2.f+rate,1.5f);
                material[2]=packed(rate,0); material[3]=0;
                material[4]=.3f; material[5]=.5f; material[6]=.7f; material[7]=.8f;
            }
            glBindBuffer(GL_UNIFORM_BUFFER,buffers[0]);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(material),material.data());
            auto render = [&](GLuint shader, auto& result) {
                glUseProgram(shader);
                if(bottom_fragment || top_fragment || round_fragment || top_round_fragment) glUniform1f(glGetUniformLocation(shader,"test_shift"),rate*.7f);
                glDrawArrays(GL_TRIANGLE_STRIP,0,4);
                glReadPixels(0,0,128,64,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
                if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GPU draw error");
            };
            render(native, actual);
            nonzero += std::count_if(actual.begin(),actual.end(),[](auto value){return value != 0;});
            saturated += std::count(actual.begin(),actual.end(),255);
            if (reference) {
                render(reference, original);
                for (unsigned i=0;i<actual.size();++i) {
                    if(i%4==3 && actual[i]!=original[i] && reported++<8)
                        std::cout << "alpha sample " << sample << " pixel " << i/4 << " native " << unsigned(actual[i]) << " original " << unsigned(original[i]) << '\n';
                    differences += actual[i] != original[i];
                    channel_differences[i%4] += actual[i] != original[i];
                    maximum[i%4] = std::max(maximum[i%4], static_cast<unsigned>(std::abs(int(actual[i])-int(original[i]))));
                }
            }
        }
        glDeleteProgram(native); if(reference) glDeleteProgram(reference);
        glDeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&output);
        glDeleteTextures(2,textures); glDeleteBuffers(2,buffers); glDeleteVertexArrays(1,&vao);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        check_mesh_renderer();
        std::cout << cases << " material draws, " << nonzero << " nonzero channels, " << saturated << " saturated channels\n";
        if(reference) {
            std::cout << differences << " differing channels\n";
            for(unsigned i=0;i<4;++i) std::cout << "channel " << i << ": " << channel_differences[i] << " max " << maximum[i] << '\n';
        } else std::cout << "Native draw only; no reference comparison\n";
        if (!nonzero || saturated==cases*actual.size() || differences) return 1;
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
