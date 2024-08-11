#include <iostream>
#include <cassert>

#include "glad/glad.h"
#include "GLFW/glfw3.h"

constexpr int W = 800, H = 500;
constexpr float vert[] = { -.5,-.5, .5,.5, .5,-.5, -.5,.5, .5,.5, -.5,-.5 };

constexpr float zero[]{0,0,0,0}, one[]{1,1,1,1};

float proj[4*4]{}, _init_matrix = ([]{ // NOLINT(*-reserved-identifier)
    float aspect = (float)W / (float)H;
    float tanHalfFov = std::tan(.5f); // tan(pi/3/2)
    float near = 0.1f, far = 100.0f;
    proj[0] = 1.0f / (aspect * tanHalfFov);
    proj[5] = 1.0f / tanHalfFov;
    proj[10] = (far + near) / (near - far);
    proj[11] =-1.0f;
    proj[14] = (2.0f * far * near) / (near - far);
}(), 0.f);

GLFWwindow* window;

constexpr auto circle_vsh = R"(
#version 410 core
layout (location = 0) in vec2 pos;
out vec2 fragUV;
out vec4 fragCol;
uniform float t;
uniform mat4 proj;
const vec4 colors[] = vec4[](
    vec4(.06f, .93f, .8f, .5f),
    vec4(1.f, .68f, .2f, .5f),
    vec4(.37f, .83f, .09f, .5f),
    vec4(.87f, .93f, .2f, .5f),
    vec4(1.f, .67, .47f, .5f),
    vec4(1.f, .37f, .4f, .5f)
);
void main() {
    fragUV = pos * 2;
    fragCol = colors[gl_InstanceID];
    vec4 p = vec4(pos.x - sin(t + gl_InstanceID), pos.y, -3 - cos(t + gl_InstanceID), 1);
    gl_Position = proj * p;
})", circle_fsh = R"(
#version 410 core
in vec2 fragUV;
in vec4 fragCol;
layout (location = 0) out vec4 accum;
layout (location = 1) out float reveal;
void main() {
    if (length(fragUV) > 0.8) discard;
    float weight = clamp(pow(min(1.0, fragCol.a * 10.0) + 0.01, 3.0) * 1e8 *
                         pow(1.0 - gl_FragCoord.z * 0.9, 3.0), 1e-2, 3e3);
    accum = vec4(fragCol.rgb * fragCol.a, fragCol.a) * weight;
    reveal = fragCol.a;
})";

constexpr auto homo_vsh = R"(
#version 410 core
layout (location = 0) in vec2 pos;
vec2 scale[] = vec2[](
    vec2(2./25., 1),
    vec2(2./25., 1),
    vec2(2./25., 3./5.), vec2(2./25., 1./5.), vec2(2./25., 1),
    vec2(6./25., 1./5.), vec2(2./25., 1./5.), vec2(6./25., 1./5.), vec2(2./25., 1./5.), vec2(6./25., 1./5.),
    vec2(2./25., 1),
    vec2(2./25., 3./5.), vec2(2./25., 1./5.), vec2(2./25., 1)
);
vec2 offset[] = vec2[](
    vec2(-16./25., 0),
    vec2(-12./25., 0),
    vec2(-8./25., 1./5.), vec2(-6./25., 0), vec2(-4./25., 0),
    vec2(2./25., 2./5.), vec2(0, 1./5.), vec2(2./25., 0), vec2(4./25., -1./5.), vec2(2./25., -2./5.),
    vec2(8./25., 0),
    vec2(12./25., 1./5.), vec2(14./25., 0), vec2(16./25., 0)
);
void main() {
    gl_Position = vec4(pos * scale[gl_InstanceID] + offset[gl_InstanceID], 0, 1);
})", homo_fsh = R"(
#version 410 core
out vec4 color;
void main() { color = vec4(1, 0.333, 0, 1); }
)";

constexpr auto composite_vsh = R"(
#version 410 core
layout (location = 0) in vec2 pos;
void main() { gl_Position = vec4(pos * 2, 0, 1); }
)", composite_fsh = R"(
#version 410 core
out vec4 frag;
uniform sampler2D accum;
uniform sampler2D reveal;
const float EPSILON = 0.00001f;
bool isApproximatelyEqual(float a, float b) {
    return abs(a - b) <= (abs(a) < abs(b) ? abs(b) : abs(a)) * EPSILON;
}
float max3(vec3 v) { return max(max(v.x, v.y), v.z); }
void main() {
    ivec2 coords = ivec2(gl_FragCoord.xy);
    float revealage = texelFetch(reveal, coords, 0).r;
    if (isApproximatelyEqual(revealage, 1.0f)) discard;
    vec4 accumulation = texelFetch(accum, coords, 0);
    if (isinf(max3(abs(accumulation.rgb)))) accumulation.rgb = vec3(accumulation.a);
    vec3 average_color = accumulation.rgb / max(accumulation.a, EPSILON);
    frag = vec4(average_color, 1.0f - revealage);
})";

GLuint createShader(const char* vsh, const char* fsh) {
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsh, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fsh, nullptr);
    glCompileShader(fragmentShader);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if(!success) {
        GLint length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog("\0", length);
        glGetProgramInfoLog(program, 512, nullptr, infoLog.data());
        throw std::runtime_error("Failed to link shader:\n" + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

int main() {
    if (glfwInit() == GLFW_FALSE)
        assert(false && "failed to initialize GLFW");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    window = glfwCreateWindow(W/2, H/2, "weighted blended", nullptr, nullptr);
#else
    window = glfwCreateWindow(W, H, "weighted blended", nullptr, nullptr);
#endif

    assert(window && "failed to create window");

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        assert(false && "failed to initialize GLAD");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, W, H);

    GLuint circle = createShader(circle_vsh, circle_fsh),
           homo = createShader(homo_vsh, homo_fsh),
           composite = createShader(composite_vsh, composite_fsh);
    glUseProgram(circle);
    glUniformMatrix4fv(glGetUniformLocation(circle, "proj"), 1, GL_FALSE, proj);

    glUseProgram(composite);
    glUniform1i(glGetUniformLocation(composite, "accum"), 0);
    glUniform1i(glGetUniformLocation(composite, "reveal"), 1);

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vert), vert, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint accum, reveal;
    glGenTextures(1, &accum);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accum);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenTextures(1, &reveal);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, reveal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accum, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, reveal, 0);

    constexpr GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);

    while (!glfwWindowShouldClose(window)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_BLEND);
        glUseProgram(homo);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 14);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearBufferfv(GL_COLOR, 0, zero); // {0,0,0,0}
        glClearBufferfv(GL_COLOR, 1, one); // {1,1,1,1}

        glEnable(GL_BLEND);
        glBlendFunci(0, GL_ONE, GL_ONE); // accumulation
        glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR); // reveal
        glBlendEquation(GL_FUNC_ADD);

        glUseProgram(circle);
        glUniform1f(glGetUniformLocation(circle, "t"), (float)glfwGetTime() * .5f);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(composite);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    return 0;
}
