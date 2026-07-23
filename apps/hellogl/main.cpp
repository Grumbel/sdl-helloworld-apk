#include <SDL.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------
// Minimal column-major 4x4 matrix helpers (OpenGL convention: v' = M*v).
// No external math library needed for a single spinning cube.
// ---------------------------------------------------------------------
using Mat4 = float[16];

static void mat4Identity(Mat4 m) {
    memset(m, 0, sizeof(Mat4));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b
static void mat4Multiply(const Mat4 a, const Mat4 b, Mat4 out) {
    Mat4 result;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            result[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, result, sizeof(Mat4));
}

static void mat4Translate(float x, float y, float z, Mat4 out) {
    mat4Identity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

static void mat4RotateX(float radians, Mat4 out) {
    mat4Identity(out);
    float c = cosf(radians), s = sinf(radians);
    out[5] = c;  out[6] = s;
    out[9] = -s; out[10] = c;
}

static void mat4RotateY(float radians, Mat4 out) {
    mat4Identity(out);
    float c = cosf(radians), s = sinf(radians);
    out[0] = c;  out[2] = -s;
    out[8] = s;  out[10] = c;
}

static void mat4Perspective(float fovyRadians, float aspect, float nearZ, float farZ, Mat4 out) {
    memset(out, 0, sizeof(Mat4));
    float f = 1.0f / tanf(fovyRadians / 2.0f);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (farZ + nearZ) / (nearZ - farZ);
    out[11] = -1.0f;
    out[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
}

// ---------------------------------------------------------------------
// Shader sources: an ES 3.00 variant and an ES 2.0 (GLSL ES 100) fallback
// variant, selected at runtime based on which GL context we actually
// managed to get (see the context-creation retry loop in main()).
// Attribute locations are pinned via glBindAttribLocation in buildProgram
// rather than "layout(location=...)" so both variants share that logic;
// an explicit layout qualifier (if present) always wins over
// glBindAttribLocation, so it's harmless to call it unconditionally.
// ---------------------------------------------------------------------
static const char *kVertexShaderSrcES3 =
    "#version 300 es\n"
    "in vec3 aPos;\n"
    "in vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *kFragmentShaderSrcES3 =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vec4(vColor, 1.0);\n"
    "}\n";

static const char *kVertexShaderSrcES2 =
    "attribute vec3 aPos;\n"
    "attribute vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *kFragmentShaderSrcES2 =
    "precision mediump float;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(vColor, 1.0);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_Log("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint buildProgram(const char *vsSrc, const char *fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aColor");
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        SDL_Log("Program link error: %s", log);
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// ---------------------------------------------------------------------
// Cube geometry: 8 vertices (position + color), 12 triangles (36 indices)
// ---------------------------------------------------------------------
static const float kCubeVertices[] = {
    // x,     y,     z,      r,    g,    b
    -0.5f, -0.5f, -0.5f,   0.0f, 0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f, 1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 1.0f,
};

static const GLushort kCubeIndices[] = {
    0, 1, 2,  2, 3, 0,  // back
    4, 5, 6,  6, 7, 4,  // front
    0, 3, 7,  7, 4, 0,  // left
    1, 5, 6,  6, 2, 1,  // right
    0, 4, 5,  5, 1, 0,  // bottom
    3, 2, 6,  6, 7, 3,  // top
};

// Context versions to try, in order. Some Android GL drivers (seen in the
// wild on this exact device) reject a plain "ES 3.0" request via SDL's
// simple EGL_CONTEXT_CLIENT_VERSION path, AND don't support the
// EGL_KHR_create_context extension SDL would otherwise use for 3.1+. So we
// just try everything reasonable and adapt (GLSL version, VAO usage) to
// whichever one actually succeeds, rather than hard-requiring ES3.
struct ContextAttempt { int major; int minor; const char *label; };
static const ContextAttempt kAttempts[] = {
    { 3, 1, "OpenGL ES 3.1" },
    { 3, 0, "OpenGL ES 3.0" },
    { 2, 0, "OpenGL ES 2.0" },
};

int main(int argc, char *argv[]) {
    SDL_Log("Hello, World! from SDL2 + OpenGL ES + C++ on Android");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow(
        "HelloGL",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        0, 0,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = nullptr;
    int gotMajor = 0;
    for (const auto &attempt : kAttempts) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attempt.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempt.minor);
        glContext = SDL_GL_CreateContext(window);
        if (glContext) {
            SDL_Log("Created %s context", attempt.label);
            gotMajor = attempt.major;
            break;
        }
        SDL_Log("Requesting %s failed: %s", attempt.label, SDL_GetError());
    }

    if (!glContext) {
        SDL_Log("Could not create any GL context");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    bool haveES3 = (gotMajor >= 3);
    SDL_GL_SetSwapInterval(1); // vsync paces the loop

    SDL_Log("GL_VERSION: %s", glGetString(GL_VERSION));
    SDL_Log("GL_RENDERER: %s", glGetString(GL_RENDERER));
    SDL_Log("GL_VENDOR: %s", glGetString(GL_VENDOR));
    SDL_Log("GL_SHADING_LANGUAGE_VERSION: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

    int drawableW = 0, drawableH = 0;
    SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
    glViewport(0, 0, drawableW, drawableH);
    glEnable(GL_DEPTH_TEST);

    GLuint program = buildProgram(haveES3 ? kVertexShaderSrcES3 : kVertexShaderSrcES2,
                                   haveES3 ? kFragmentShaderSrcES3 : kFragmentShaderSrcES2);
    if (!program) {
        SDL_Log("Failed to build shader program");
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    GLint mvpLoc = glGetUniformLocation(program, "uMVP");

    // No VAO here on purpose: VAOs are ES3-only, and this app may have
    // fallen back to an ES2 context above. We only ever draw one object
    // with one buffer layout, so binding the buffers + attrib pointers
    // once, up front, is sufficient — no need to rebind per frame.
    GLuint vbo = 0, ebo = 0;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices, GL_STATIC_DRAW);

    const GLsizei stride = 6 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Cube bounces within this box (world units, camera looking down -Z).
    const float boundX = 1.6f, boundY = 1.0f;
    float posX = 0.0f, posY = 0.0f, posZ = -4.0f;
    float velX = 0.9f, velY = 0.7f; // units/sec
    float angleX = 0.0f, angleY = 0.0f;
    const float spinSpeedX = 1.3f, spinSpeedY = 0.9f; // rad/sec

    Uint64 lastTicks = SDL_GetTicks64();
    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.scancode == SDL_SCANCODE_AC_BACK ||
                        event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        running = false;
                    }
                    break;
                case SDL_FINGERDOWN:
                    SDL_Log("Touch at (%.2f, %.2f)", event.tfinger.x, event.tfinger.y);
                    break;
                default:
                    break;
            }
        }

        Uint64 nowTicks = SDL_GetTicks64();
        float dt = (nowTicks - lastTicks) / 1000.0f;
        lastTicks = nowTicks;
        if (dt > 0.05f) dt = 0.05f; // clamp in case of a stall/hitch

        posX += velX * dt;
        posY += velY * dt;
        if (posX < -boundX || posX > boundX) { velX = -velX; posX = SDL_clamp(posX, -boundX, boundX); }
        if (posY < -boundY || posY > boundY) { velY = -velY; posY = SDL_clamp(posY, -boundY, boundY); }

        angleX += spinSpeedX * dt;
        angleY += spinSpeedY * dt;

        Mat4 rotX, rotY, model, translate, view, proj, vp, mvp;
        mat4RotateX(angleX, rotX);
        mat4RotateY(angleY, rotY);
        mat4Multiply(rotY, rotX, model);          // model = rotY * rotX
        mat4Translate(posX, posY, posZ, translate);
        mat4Multiply(translate, model, model);    // model = translate * (rotY * rotX)

        mat4Identity(view); // camera at origin looking down -Z; cube already placed at posZ
        float aspect = (float)drawableW / (float)drawableH;
        mat4Perspective(60.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f, proj);

        mat4Multiply(proj, view, vp);
        mat4Multiply(vp, model, mvp);

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        glDrawElements(GL_TRIANGLES, (GLsizei)(sizeof(kCubeIndices) / sizeof(kCubeIndices[0])),
                        GL_UNSIGNED_SHORT, 0);

        SDL_GL_SwapWindow(window);
    }

    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
