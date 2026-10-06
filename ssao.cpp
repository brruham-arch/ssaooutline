// libssao.so - SSAO + outline (port ssao_stage14.lua). Hook GL via Dobby.
#include <stdint.h>
#include "gothook.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define LOGFILE "/storage/emulated/0/ssao_log.txt"
#define START_FRAME 240

static void slog(const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    __android_log_print(ANDROID_LOG_INFO, "libssao", "%s", b);
    FILE* f = fopen(LOGFILE, "a"); if (f) { fprintf(f, "%s\n", b); fclose(f); }
}

// ---- parameter (layout harus sama dgn struct ssao_params di Lua) ----
struct Params {
    float NEAR, FAR, RADIUS_WORLD, RANGE_WORLD, BIAS_WORLD, STRENGTH, MAX_PX,
          EDGE_T, EDGE_S, EDGE_W, EDGE_FADE0, EDGE_FADE1;
    int FALLBACK_SWAP;
    float FOG_DENSITY, FOG_START, FOG_END, FOG_R, FOG_G, FOG_B, VIG_STRENGTH, VIG_SOFT, EXPOSURE, TINT_R, TINT_G, TINT_B, SATURATION, CONTRAST, BLOOM_THR, BLOOM_INT, BLOOM_RADIUS;
};
static Params P;

typedef void (*PFN_BindFB)(GLenum, GLuint);
typedef void (*PFN_FbTex)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*PFN_FbRb)(GLenum, GLenum, GLenum, GLuint);
typedef void (*PFN_Clear)(GLbitfield);
typedef EGLBoolean (*PFN_Swap)(EGLDisplay, EGLSurface);
static PFN_BindFB o_BindFB; static PFN_FbTex o_FbTex; static PFN_FbRb o_FbRb;
static PFN_Clear o_Clear;   static PFN_Swap o_Swap;
static int (*pDobbyHook)(void*, void*, void**);

// ---- state ----
static const int MAXN = 512;
static GLuint rec[MAXN * 4]; static int nrec = 0;
static int frame = 0, phase = 0, scrW = 0, scrH = 0, rbFmt = 0, nT = 0, shaderSt = 0;
static GLuint tfbo[32], tatt[32], trb[32], depthTex[32]; static int tw[32], th[32];
static bool depthCleared = false, composited = false, everActive = false, busy = false, hooksDone = false;
static GLuint curFb = 0;
static void* curCtx = nullptr;
static time_t lastT = 0; static int lastF = 0;

static bool isDepth(GLenum a) { return a == GL_DEPTH_ATTACHMENT || a == 0x821A; }
static bool isTarget(GLuint f) { for (int i = 0; i < nT; i++) if (tfbo[i] == f) return true; return false; }

static void checkCtx() {
    void* c = eglGetCurrentContext();
    if (!c) return;
    if (curCtx && c != curCtx) {
        nrec = 0; frame = 0; phase = 0; shaderSt = 0; curFb = 0;
        slog("context berubah, reinit");
    }
    curCtx = c;
}

static void record(int kind, GLenum att, GLuint obj) {
    checkCtx();
    GLint fbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    for (int i = 0; i < nrec; i++)
        if (rec[i*4] == (GLuint)kind && rec[i*4+1] == (GLuint)fbo && rec[i*4+2] == att) { rec[i*4+3] = obj; return; }
    if (nrec < MAXN) {
        int i = nrec;
        rec[i*4] = kind; rec[i*4+1] = fbo; rec[i*4+2] = att; rec[i*4+3] = obj; nrec++;
    }
}

static bool doAttach() {
    GLint oldFbo, oldRb, oldTex, u = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &oldRb);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    int n = 0; GLint fmt0 = 0;
    for (int i = 0; i < nrec; i++) {
        if (rec[i*4] != 2 || n >= 32) continue;
        GLuint f = rec[i*4+1], a = rec[i*4+2], r = rec[i*4+3];
        if (f && r && glIsFramebuffer(f) && glIsRenderbuffer(r)) {
            glBindRenderbuffer(GL_RENDERBUFFER, r);
            GLint fm = 0; glGetRenderbufferParameteriv(GL_RENDERBUFFER, 0x8D44, &fm);
            { GLint ww=0,hh2=0; glGetRenderbufferParameteriv(GL_RENDERBUFFER,0x8D42,&ww); glGetRenderbufferParameteriv(GL_RENDERBUFFER,0x8D43,&hh2); slog("cand fbo=%u att=0x%X rb=%u fmt=0x%X %dx%d", f, a, r, fm, ww, hh2); }
            if (fm == 0x81A6 || fm == 0x81A5) {
                fmt0 = 0x81A6;
                if (fm == fmt0) {
                    tfbo[n] = f; tatt[n] = a; trb[n] = r;
                    glGetRenderbufferParameteriv(GL_RENDERBUFFER, 0x8D42, &u); tw[n] = u;
                    glGetRenderbufferParameteriv(GL_RENDERBUFFER, 0x8D43, &u); th[n] = u;
                    if (tw[n] >= 64 && th[n] >= 64) n++;
                }
            }
        }
    }
    glBindRenderbuffer(GL_RENDERBUFFER, oldRb);
    nT = n;
    for (int i = 1, b = 0; i < n; i++) { if (tw[i]*th[i] > tw[b]*th[b]) b = i; if (i == n-1 && b > 0) { GLuint x; int y; x=tfbo[0];tfbo[0]=tfbo[b];tfbo[b]=x; x=tatt[0];tatt[0]=tatt[b];tatt[b]=x; x=trb[0];trb[0]=trb[b];trb[b]=x; y=tw[0];tw[0]=tw[b];tw[b]=y; y=th[0];th[0]=th[b];th[b]=y; } }
    if (n == 0) return false;
    rbFmt = fmt0; scrW = tw[0]; scrH = th[0];
    GLint ifmt = fmt0; GLenum typ = (fmt0 == 0x81A6) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    for (int i = 0; i < n; i++) {
        int same = -1;
        for (int j = 0; j < i; j++) if (trb[j] == trb[i]) { same = j; break; }
        if (same >= 0) { depthTex[i] = depthTex[same]; continue; }
        glGenTextures(1, &depthTex[i]);
        glBindTexture(GL_TEXTURE_2D, depthTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, tw[i], th[i], 0, GL_DEPTH_COMPONENT, typ, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, oldTex);
    for (int i = 0; i < n; i++) {
        o_BindFB(GL_FRAMEBUFFER, tfbo[i]);
        o_FbTex(GL_FRAMEBUFFER, tatt[i], GL_TEXTURE_2D, depthTex[i], 0);
    }
    o_BindFB(GL_FRAMEBUFFER, oldFbo);
    return true;
}

static void doRevert() {
    GLint oldFbo; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    for (int i = 0; i < nT; i++) {
        o_BindFB(GL_FRAMEBUFFER, tfbo[i]);
        o_FbRb(GL_FRAMEBUFFER, tatt[i], GL_RENDERBUFFER, trb[i]);
    }
    o_BindFB(GL_FRAMEBUFFER, oldFbo);
}

// ---- shader ----
static const char* VS = R"GLSL(
attribute vec2 aPos;
varying vec2 vUv;
void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* FS_AO = R"GLSL(
precision highp float;
varying vec2 vUv;
uniform sampler2D uDepth;
uniform vec2 uTexel;
uniform float uNear, uFar, uRadius, uRange, uBias, uStrength, uMaxPx;
uniform float uEdgeT, uEdgeW, uFade0, uFade1;
float lin(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    float d0 = texture2D(uDepth, vUv).r;
    if (d0 >= 0.99999) { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); return; }
    float zc = lin(d0);
    float edge = 0.0;
    if (zc < uFade1) {
        vec2 t = uTexel * uEdgeW;
        float zl = lin(texture2D(uDepth, vUv - vec2(t.x, 0.0)).r);
        float zr = lin(texture2D(uDepth, vUv + vec2(t.x, 0.0)).r);
        float zu = lin(texture2D(uDepth, vUv + vec2(0.0, t.y)).r);
        float zd = lin(texture2D(uDepth, vUv - vec2(0.0, t.y)).r);
        float e = max(abs(zl + zr - 2.0 * zc), abs(zu + zd - 2.0 * zc)) / zc;
        edge = smoothstep(uEdgeT, uEdgeT * 2.0, e) * (1.0 - smoothstep(uFade0, uFade1, zc));
    }
    float rpx = clamp(uRadius * 770.0 / zc, 3.0, uMaxPx);
    vec2 fc = floor(gl_FragCoord.xy);
    float idx = mod(fc.x, 4.0) + 4.0 * mod(fc.y, 4.0);
    float rot = idx * 0.3926991;
    float occ = 0.0;
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        float a = rot + fi * 2.399963;
        float r = sqrt((fi + 0.5) / 6.0) * rpx;
        vec2 uv = vUv + vec2(cos(a), sin(a)) * r * uTexel;
        float zs = lin(texture2D(uDepth, uv).r);
        float diff = zc - zs;
        if (diff > uBias && diff < uRange) occ += 1.0 - diff / uRange;
    }
    float ao = clamp(1.0 - uStrength * occ / 6.0, 0.0, 1.0);
    gl_FragColor = vec4(ao, edge, 0.0, 1.0);
}
)GLSL";

static const char* FS_C = R"GLSL(
precision mediump float;
varying vec2 vUv;
uniform sampler2D uAO;
uniform vec2 uAoTexel;
uniform float uEdgeS;
void main() {
    vec2 o = uAoTexel;
    float ao = (texture2D(uAO, vUv + vec2( o.x,  o.y)).r
              + texture2D(uAO, vUv + vec2(-o.x,  o.y)).r
              + texture2D(uAO, vUv + vec2( o.x, -o.y)).r
              + texture2D(uAO, vUv + vec2(-o.x, -o.y)).r) * 0.25;
    float edge = texture2D(uAO, vUv).g * uEdgeS;
    gl_FragColor = vec4(vec3(ao * (1.0 - edge)), 1.0);
}
)GLSL";

static GLuint progAO, progC, aoTex, aoFbo;
static GLint uDepth, uTexel, uNear, uFar, uRadius, uRange, uBias, uStrength, uMaxPx,
             uEdgeT, uEdgeW, uFade0, uFade1, uAO, uAoTexel, uEdgeS;
static int hw, hh;
static const float verts[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };

static GLuint compile(GLenum kind, const char* src) {
    GLuint sh = glCreateShader(kind);
    glShaderSource(sh, 1, &src, nullptr); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[512]; glGetShaderInfoLog(sh, 512, nullptr, b); slog("shader gagal: %s", b); return 0; }
    return sh;
}
static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "aPos"); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char b[512]; glGetProgramInfoLog(p, 512, nullptr, b); slog("link gagal: %s", b); return 0; }
    return p;
}

static void build() {
    progAO = makeProgram(VS, FS_AO); progC = makeProgram(VS, FS_C);
    if (!progAO || !progC) { shaderSt = 2; return; }
    #define UL(v, n) v = glGetUniformLocation(progAO, n)
    UL(uDepth,"uDepth"); UL(uTexel,"uTexel"); UL(uNear,"uNear"); UL(uFar,"uFar");
    UL(uRadius,"uRadius"); UL(uRange,"uRange"); UL(uBias,"uBias"); UL(uStrength,"uStrength");
    UL(uMaxPx,"uMaxPx"); UL(uEdgeT,"uEdgeT"); UL(uEdgeW,"uEdgeW"); UL(uFade0,"uFade0"); UL(uFade1,"uFade1");
    uAO = glGetUniformLocation(progC, "uAO");
    uAoTexel = glGetUniformLocation(progC, "uAoTexel");
    uEdgeS = glGetUniformLocation(progC, "uEdgeS");
    hw = scrW / 2; hh = scrH / 2;
    GLint oFbo, oTex;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oFbo); glGetIntegerv(GL_TEXTURE_BINDING_2D, &oTex);
    glGenTextures(1, &aoTex); glBindTexture(GL_TEXTURE_2D, aoTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hw, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &aoFbo);
    o_BindFB(GL_FRAMEBUFFER, aoFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoTex, 0);
    GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    o_BindFB(GL_FRAMEBUFFER, oFbo); glBindTexture(GL_TEXTURE_2D, oTex);
    if (s != GL_FRAMEBUFFER_COMPLETE) { slog("FBO AO tidak complete: 0x%04X", s); shaderSt = 2; return; }
    shaderSt = 1;
}

#include "fx.h"
static const GLenum CAPS[5] = { GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST };

static void drawAO(GLuint dst) {
    if (shaderSt == 0) build();
    if (shaderSt != 1) return;
    GLint oFbo, oProg, oAct, oBuf, vp[4], bsRgb, bdRgb, bsA, bdA, oAttr, oTex;
    GLboolean saved[5];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oFbo); glGetIntegerv(GL_CURRENT_PROGRAM, &oProg);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oAct); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oBuf);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_BLEND_SRC_RGB, &bsRgb); glGetIntegerv(GL_BLEND_DST_RGB, &bdRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsA); glGetIntegerv(GL_BLEND_DST_ALPHA, &bdA);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &oAttr);
    for (int i = 0; i < 5; i++) saved[i] = glIsEnabled(CAPS[i]);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oTex);
    for (int i = 0; i < 5; i++) glDisable(CAPS[i]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);

    // pass 1: AO + outline (half-res)
    o_BindFB(GL_FRAMEBUFFER, aoFbo);
    glViewport(0, 0, hw, hh);
    glUseProgram(progAO);
    glBindTexture(GL_TEXTURE_2D, depthTex[0]);
    glUniform1i(uDepth, 0);
    glUniform2f(uTexel, 1.0f / scrW, 1.0f / scrH);
    glUniform1f(uNear, P.NEAR); glUniform1f(uFar, P.FAR);
    glUniform1f(uRadius, P.RADIUS_WORLD); glUniform1f(uRange, P.RANGE_WORLD);
    glUniform1f(uBias, P.BIAS_WORLD); glUniform1f(uStrength, P.STRENGTH);
    glUniform1f(uMaxPx, P.MAX_PX);
    glUniform1f(uEdgeT, P.EDGE_T); glUniform1f(uEdgeW, P.EDGE_W);
    glUniform1f(uFade0, P.EDGE_FADE0); glUniform1f(uFade1, P.EDGE_FADE1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // pass 2: composite (dst = dst * src)
    o_BindFB(GL_FRAMEBUFFER, dst);
    glViewport(0, 0, scrW, scrH);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_ONE);
    glUseProgram(progC);
    glBindTexture(GL_TEXTURE_2D, aoTex);
    glUniform1i(uAO, 0);
    glUniform2f(uAoTexel, 1.0f / hw, 1.0f / hh);
    glUniform1f(uEdgeS, P.EDGE_S);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    fxDraw();
    if (oAttr == 0) glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, oBuf);
    glBindTexture(GL_TEXTURE_2D, oTex);
    glActiveTexture(oAct);
    glUseProgram(oProg);
    glBlendFuncSeparate(bsRgb, bdRgb, bsA, bdA);
    for (int i = 0; i < 5; i++) { if (saved[i]) glEnable(CAPS[i]); else glDisable(CAPS[i]); }
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    o_BindFB(GL_FRAMEBUFFER, oFbo);
}

static bool ranOnce = false;
static void runAO(GLuint dst) {
    if (!ranOnce) { ranOnce = true; slog("runAO pertama"); }
    busy = true; drawAO(dst); busy = false;
}

// ---- hook callbacks ----
static void h_FbTex(GLenum t, GLenum a, GLenum ta, GLuint tex, GLint lv) {
    if (isDepth(a)) record(1, a, tex);
    o_FbTex(t, a, ta, tex, lv);
}
static void h_FbRb(GLenum t, GLenum a, GLenum rbt, GLuint rb) {
    if (isDepth(a) && rb != 0) {
        record(2, a, rb);
        if (phase == 1) { phase = 0; frame = START_FRAME - 20; slog("depth di-reattach game, attach ulang"); }
    }
    o_FbRb(t, a, rbt, rb);
}
static void h_Clear(GLbitfield m) {
    GLint f = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &f); if ((m & GL_DEPTH_BUFFER_BIT) && nT > 0 && (GLuint)f == tfbo[0]) depthCleared = true;
    { static int cn = 0; if ((m & GL_DEPTH_BUFFER_BIT) && cn < 30 && phase == 1) { cn++; slog("clear depth fbo=%d tgt=%d", f, (int)isTarget((GLuint)f)); } }
    o_Clear(m);
}
static void h_BindFB(GLenum t, GLuint f) {
    if (!busy && t == GL_FRAMEBUFFER && f != curFb) {
        GLuint prev = curFb; curFb = f;
        if (phase == 1 && depthCleared && !composited && nT > 0 && prev == tfbo[0] && f != tfbo[0]) {
            composited = true; runAO(prev);
        }
    }
    o_BindFB(t, f);
}

static void* hGLES = nullptr; static void* hEGL = nullptr;
static bool hookFn(const char* name, void* repl, void** orig, void* lib) {
    void* a = dlsym(lib, name);
    if (!a) { slog("sym tidak ada: %s", name); return false; }
    if (lib == hGLES) { bool r = gotHook(a, repl, orig); slog("GOT %s: %s", name, r ? "ok" : "gagal"); return r; }
    if (pDobbyHook(a, repl, orig) != 0) { slog("DobbyHook gagal: %s", name); return false; }
    return true;
}
static void installHooks() {
    slog("pasang hook GL");
    hookFn("glFramebufferTexture2D", (void*)h_FbTex, (void**)&o_FbTex, hGLES);
    hookFn("glFramebufferRenderbuffer", (void*)h_FbRb, (void**)&o_FbRb, hGLES);
    hookFn("glClear", (void*)h_Clear, (void**)&o_Clear, hGLES);
    GLint u = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &u); curFb = u;
    // o_BindFB dipakai sebelum hook terpasang: isi dulu dari dlsym
    o_BindFB = (PFN_BindFB)dlsym(hGLES, "glBindFramebuffer");
    hookFn("glBindFramebuffer", (void*)h_BindFB, (void**)&o_BindFB, hGLES);
    slog("installHooks selesai");
}

static EGLBoolean h_Swap(EGLDisplay d, EGLSurface s) {
    if (!o_Swap) return EGL_TRUE;
    if (!hooksDone) { hooksDone = true; slog("frame pertama"); installHooks(); }
    checkCtx();
    frame++;
    if (phase == 0) {
        if (frame >= START_FRAME && nrec > 0) {
            busy = true; bool ok = doAttach(); busy = false;
            if (ok) {
                phase = 1; everActive = true; lastT = time(nullptr); lastF = frame;
                slog("SSAO+outline aktif | %dx%d | target FBO = %d", scrW, scrH, nT);
            }
        } else if (frame >= 2400 && !everActive) {
            phase = 2; slog("depth renderbuffer tidak terdeteksi (hook terlambat?)");
        }
    } else if (phase == 1) {
        if (shaderSt == 2) {
            busy = true; doRevert(); busy = false; phase = 2;
            slog("shader/FBO gagal, depth dikembalikan");
        } else {
            if (depthCleared && !composited && P.FALLBACK_SWAP) runAO(0);
            depthCleared = false; composited = false;
            if (frame - lastF >= 600) {
                time_t now = time(nullptr); int dt = (int)(now - lastT); if (dt < 1) dt = 1;
                slog("~%d fps", (frame - lastF) / dt);
                lastT = now; lastF = frame;
            }
        }
    }
    return o_Swap(d, s);
}

// ---- export untuk Lua (FFI) ----
static bool inited = false;
EXPORT int ssao_init(void) {
    if (inited) return 1;
    remove(LOGFILE);
    P = { 0.1f, 1000.0f, 1.2f, 2.0f, 0.05f, 1.5f, 16.0f, 0.04f, 0.9f, 1.5f, 150.0f, 400.0f, 1, 0.5f, 60.0f, 300.0f, 0.65f, 0.75f, 0.9f, 0.35f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f, 1.1f, 0.65f, 0.6f, 1.5f };
    hGLES = dlopen("libGLESv2.so", RTLD_NOW);
    hEGL = dlopen("libEGL.so", RTLD_NOW);
    void* hD = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hGLES || !hEGL || !hD) { slog("ERROR: dlopen GLES/EGL/dobby"); return 0; }
    pDobbyHook = (int (*)(void*, void*, void**))dlsym(hD, "DobbyHook");
    if (!pDobbyHook) { slog("ERROR: DobbyHook"); return 0; }
    if (!hookFn("eglSwapBuffers", (void*)h_Swap, (void**)&o_Swap, hEGL)) return 0;
    inited = true;
    slog("ssao_init OK, aktif ~4 detik setelah frame pertama");
    return 1;
}
EXPORT void* ssao_get_params(void) { return &P; }
EXPORT int ssao_is_active(void) { return phase == 1; }
