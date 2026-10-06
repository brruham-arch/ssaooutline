#pragma once
#include <math.h>
static GLuint progV, progF, progG, gTex; static int fxSt = 0, gW = 0, gH = 0, gErr = 0; static void* fxCtx = nullptr;
static GLint vVig, vSoft, vExp, vTint, fDepth, fNear, fFar, fDens, fStart, fEnd, fCol, gScene, gSat, gCon;

static const char* FS_V = R"GLSL(
precision mediump float;
varying vec2 vUv;
uniform float uVig, uSoft, uExp;
uniform vec3 uTint;
void main() {
    float d = length(vUv - 0.5) * 1.4142;
    float v = 1.0 - uVig * smoothstep(uSoft, 1.0, d);
    gl_FragColor = vec4(uTint * uExp * v, 1.0);
}
)GLSL";

static const char* FS_F = R"GLSL(
precision highp float;
varying vec2 vUv;
uniform sampler2D uDepth;
uniform float uNear, uFar, uDens, uStart, uEnd;
uniform vec3 uCol;
void main() {
    float d = texture2D(uDepth, vUv).r;
    if (d >= 0.99999) { gl_FragColor = vec4(0.0); return; }
    float z = 2.0 * uNear * uFar / (uFar + uNear - (d * 2.0 - 1.0) * (uFar - uNear));
    gl_FragColor = vec4(uCol, uDens * smoothstep(uStart, uEnd, z));
}
)GLSL";

static const char* FS_G = R"GLSL(
precision mediump float;
varying vec2 vUv;
uniform sampler2D uScene;
uniform float uSat, uCon;
void main() {
    vec3 c = texture2D(uScene, vUv).rgb;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, uSat);
    c = (c - 0.5) * uCon + 0.5;
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";

static void fxDraw() {
    bool vOn = P.VIG_STRENGTH > 0.001f || P.EXPOSURE < 0.999f || P.TINT_R < 0.999f || P.TINT_G < 0.999f || P.TINT_B < 0.999f;
    bool fOn = P.FOG_DENSITY > 0.001f;
    bool gOn = (fabsf(P.SATURATION - 1.0f) > 0.001f || fabsf(P.CONTRAST - 1.0f) > 0.001f) && gErr < 3;
    if (!vOn && !fOn && !gOn) return;
    void* c = eglGetCurrentContext();
    if (c != fxCtx) { fxCtx = c; fxSt = 0; gTex = 0; gErr = 0; }
    if (fxSt == 0) {
        progV = makeProgram(VS, FS_V); progF = makeProgram(VS, FS_F); progG = makeProgram(VS, FS_G);
        if (!progV || !progF || !progG) { fxSt = 2; slog("fx shader gagal"); return; }
        vVig = glGetUniformLocation(progV, "uVig"); vSoft = glGetUniformLocation(progV, "uSoft");
        vExp = glGetUniformLocation(progV, "uExp"); vTint = glGetUniformLocation(progV, "uTint");
        fDepth = glGetUniformLocation(progF, "uDepth"); fNear = glGetUniformLocation(progF, "uNear");
        fFar = glGetUniformLocation(progF, "uFar"); fDens = glGetUniformLocation(progF, "uDens");
        fStart = glGetUniformLocation(progF, "uStart"); fEnd = glGetUniformLocation(progF, "uEnd");
        fCol = glGetUniformLocation(progF, "uCol");
        gScene = glGetUniformLocation(progG, "uScene"); gSat = glGetUniformLocation(progG, "uSat");
        gCon = glGetUniformLocation(progG, "uCon");
        fxSt = 1; slog("fx shader OK (termasuk grade)");
    }
    if (fxSt != 1) return;
    if (vOn) {
        glBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_ONE);
        glUseProgram(progV);
        glUniform1f(vVig, P.VIG_STRENGTH); glUniform1f(vSoft, P.VIG_SOFT); glUniform1f(vExp, P.EXPOSURE);
        glUniform3f(vTint, P.TINT_R, P.TINT_G, P.TINT_B);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    if (fOn) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glUseProgram(progF);
        glBindTexture(GL_TEXTURE_2D, depthTex[0]);
        glUniform1i(fDepth, 0);
        glUniform1f(fNear, P.NEAR); glUniform1f(fFar, P.FAR);
        glUniform1f(fDens, P.FOG_DENSITY); glUniform1f(fStart, P.FOG_START); glUniform1f(fEnd, P.FOG_END);
        glUniform3f(fCol, P.FOG_R, P.FOG_G, P.FOG_B);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    if (gOn) {
        if (gTex == 0 || gW != scrW || gH != scrH) {
            if (gTex == 0) glGenTextures(1, &gTex);
            glBindTexture(GL_TEXTURE_2D, gTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, scrW, scrH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            gW = scrW; gH = scrH;
        }
        glBindTexture(GL_TEXTURE_2D, gTex);
        glGetError();
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, scrW, scrH);
        GLenum e = glGetError();
        if (e) { gErr++; slog("grade copy error 0x%X (%d/3)", e, gErr); return; }
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
        glUseProgram(progG);
        glUniform1i(gScene, 0);
        glUniform1f(gSat, P.SATURATION); glUniform1f(gCon, P.CONTRAST);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}
