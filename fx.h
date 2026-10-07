#pragma once
#include <math.h>
static GLuint pAll, pBr, pBl, pAd, cTex, blTex[2], blFbo[2];
static int fxSt = 0, cW = 0, cH = 0, blW = 0, blH = 0, fxErr = 0; static void* fxCtx = nullptr;
static GLint aScene, aDepth, aNear, aFar, aDens, aStart, aEnd, aSat, aCon, aVig, aSoft, aExp, aFogCol, aTint;
static GLint aDof, aFocus, aFR, aPx;
static GLint bScene, bTexel, bThr, lTex, lDir, dTex, dInt;

static const char* FS_ALL = R"GLSL(
precision highp float;
varying vec2 vUv;
uniform sampler2D uScene, uDepth;
uniform float uNear, uFar, uDens, uStart, uEnd, uSat, uCon, uVig, uSoft, uExp;
uniform vec3 uFogCol, uTint;
uniform float uDof, uFocus, uFRange; uniform vec2 uPx;
void main() {
    float d0 = texture2D(uDepth, vUv).r;
    float z0 = d0 >= 0.99999 ? uFar : 2.0 * uNear * uFar / (uFar + uNear - (d0 * 2.0 - 1.0) * (uFar - uNear));
    float coc = uDof * clamp((abs(z0 - uFocus) - uFRange) / uFRange, 0.0, 1.0);
    vec3 c = texture2D(uScene, vUv).rgb;
    if (uDof > 0.0 && coc > 0.5) {
        vec3 s = c;
        for (int i = 0; i < 8; i++) {
            float a = float(i) * 0.785398;
            float r = mod(float(i), 2.0) > 0.5 ? 1.0 : 0.55;
            s += texture2D(uScene, vUv + vec2(cos(a), sin(a)) * r * coc * 3.0 * uPx).rgb;
        }
        c = s / 9.0;
    }
    if (uDens > 0.0) {
        float d = texture2D(uDepth, vUv).r;
        if (d < 0.99999) {
            float z = 2.0 * uNear * uFar / (uFar + uNear - (d * 2.0 - 1.0) * (uFar - uNear));
            c = mix(c, uFogCol, uDens * smoothstep(uStart, uEnd, z));
        }
    }
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, uSat);
    c = (c - 0.5) * uCon + 0.5;
    float dv = length(vUv - 0.5) * 1.4142;
    c *= uTint * uExp * (1.0 - uVig * smoothstep(uSoft, 1.0, dv));
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";

static const char* FS_BR = R"GLSL(
precision highp float;
varying vec2 vUv;
uniform sampler2D uScene;
uniform vec2 uTexel;
uniform float uThr;
vec3 br(vec2 uv) {
    vec3 c = texture2D(uScene, uv).rgb;
    float l = max(c.r, max(c.g, c.b));
    return c * smoothstep(uThr * 0.85, uThr, l);
}
void main() {
    vec2 o = uTexel * 1.5;
    vec3 c = br(vUv + vec2(-o.x, -o.y)) + br(vUv + vec2(o.x, -o.y))
           + br(vUv + vec2(-o.x, o.y)) + br(vUv + vec2(o.x, o.y));
    gl_FragColor = vec4(c * 0.25, 1.0);
}
)GLSL";

static const char* FS_BL = R"GLSL(
precision highp float;
varying vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uDir;
void main() {
    vec3 c = texture2D(uTex, vUv).rgb * 0.227027;
    c += texture2D(uTex, vUv + uDir * 1.3846).rgb * 0.316216;
    c += texture2D(uTex, vUv - uDir * 1.3846).rgb * 0.316216;
    c += texture2D(uTex, vUv + uDir * 3.2308).rgb * 0.070270;
    c += texture2D(uTex, vUv - uDir * 3.2308).rgb * 0.070270;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

static const char* FS_AD = R"GLSL(
precision mediump float;
varying vec2 vUv;
uniform sampler2D uTex;
uniform float uInt;
void main() { gl_FragColor = vec4(texture2D(uTex, vUv).rgb * uInt, 1.0); }
)GLSL";

static void tp() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void fxDraw() {
    bool fOn = P.FOG_DENSITY > 0.001f;
    bool cOn = fOn || fabsf(P.SATURATION - 1.0f) > 0.001f || fabsf(P.CONTRAST - 1.0f) > 0.001f
            || P.VIG_STRENGTH > 0.001f || P.EXPOSURE < 0.999f
            || P.TINT_R < 0.999f || P.TINT_G < 0.999f || P.TINT_B < 0.999f;
    bool bOn = P.EN_BLOOM && P.BLOOM_INT > 0.001f;
    if (!P.EN_FX) { cOn = false; fOn = false; }
    bool dOn = P.EN_FX && P.EN_DOF && P.DOF_AMOUNT > 0.01f; if (dOn) cOn = true;
    if ((!cOn && !bOn) || fxErr >= 3) return;
    void* c = eglGetCurrentContext();
    if (c != fxCtx) { fxCtx = c; fxSt = 0; cTex = 0; blTex[0] = blTex[1] = 0; blFbo[0] = blFbo[1] = 0; cW = cH = 0; fxErr = 0; }
    if (fxSt == 0) {
        pAll = makeProgram(VS, FS_ALL); pBr = makeProgram(VS, FS_BR);
        pBl = makeProgram(VS, FS_BL); pAd = makeProgram(VS, FS_AD);
        if (!pAll || !pBr || !pBl || !pAd) { fxSt = 2; slog("fx shader gagal"); return; }
        #define UA(v, n) v = glGetUniformLocation(pAll, n)
        UA(aScene,"uScene"); UA(aDepth,"uDepth"); UA(aNear,"uNear"); UA(aFar,"uFar"); UA(aDens,"uDens");
        UA(aStart,"uStart"); UA(aEnd,"uEnd"); UA(aSat,"uSat"); UA(aCon,"uCon"); UA(aVig,"uVig");
        UA(aSoft,"uSoft");
        aDof = glGetUniformLocation(pAll, "uDof"); aFocus = glGetUniformLocation(pAll, "uFocus"); aFR = glGetUniformLocation(pAll, "uFRange"); aPx = glGetUniformLocation(pAll, "uPx"); UA(aExp,"uExp"); UA(aFogCol,"uFogCol"); UA(aTint,"uTint");
        bScene = glGetUniformLocation(pBr, "uScene"); bTexel = glGetUniformLocation(pBr, "uTexel");
        bThr = glGetUniformLocation(pBr, "uThr");
        lTex = glGetUniformLocation(pBl, "uTex"); lDir = glGetUniformLocation(pBl, "uDir");
        dTex = glGetUniformLocation(pAd, "uTex"); dInt = glGetUniformLocation(pAd, "uInt");
        fxSt = 1; slog("fx shader OK (gabungan 1 pass)");
    }
    if (fxSt != 1) return;
    GLint dst = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &dst);
    if (!cTex) glGenTextures(1, &cTex);
    if (!blTex[0]) { glGenTextures(2, blTex); glGenFramebuffers(2, blFbo); }
    if (cW != scrW || cH != scrH) {
        glBindTexture(GL_TEXTURE_2D, cTex); tp();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, scrW, scrH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        blW = scrW / 6; blH = scrH / 6;
        bool ok = true;
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, blTex[i]); tp();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blW, blH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, blFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blTex[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, dst);
        if (!ok) { fxErr = 3; slog("fx FBO tidak complete"); return; }
        cW = scrW; cH = scrH;
    }
    // satu salinan layar untuk semua efek
    glBindTexture(GL_TEXTURE_2D, cTex);
    glGetError();
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, scrW, scrH);
    GLenum e = glGetError();
    if (e) { fxErr++; slog("fx copy error 0x%X (%d/3)", e, fxErr); return; }

    glDisable(GL_BLEND);
    if (bOn) {
        glBindFramebuffer(GL_FRAMEBUFFER, blFbo[0]);
        glViewport(0, 0, blW, blH);
        glUseProgram(pBr);
        glUniform1i(bScene, 0); glUniform2f(bTexel, 1.0f / scrW, 1.0f / scrH); glUniform1f(bThr, P.BLOOM_THR);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glUseProgram(pBl);
        glUniform1i(lTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, blFbo[1]);
        glBindTexture(GL_TEXTURE_2D, blTex[0]);
        glUniform2f(lDir, P.BLOOM_RADIUS / blW, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindFramebuffer(GL_FRAMEBUFFER, blFbo[0]);
        glBindTexture(GL_TEXTURE_2D, blTex[1]);
        glUniform2f(lDir, 0.0f, P.BLOOM_RADIUS / blH);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, dst);
    glViewport(0, 0, scrW, scrH);
    if (cOn) {
        glUseProgram(pAll);
        glBindTexture(GL_TEXTURE_2D, cTex);
        GLint o1 = 0;
        if (fOn || dOn) {
            glActiveTexture(GL_TEXTURE1);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &o1);
            glBindTexture(GL_TEXTURE_2D, depthTex[0]);
            glActiveTexture(GL_TEXTURE0);
        }
        glUniform1i(aScene, 0); glUniform1i(aDepth, 1);
        glUniform1f(aNear, P.NEAR); glUniform1f(aFar, P.FAR);
        glUniform1f(aDens, P.FOG_DENSITY); glUniform1f(aStart, P.FOG_START); glUniform1f(aEnd, P.FOG_END);
        glUniform1f(aSat, P.SATURATION); glUniform1f(aCon, P.CONTRAST);
        glUniform1f(aVig, P.VIG_STRENGTH); glUniform1f(aSoft, P.VIG_SOFT); glUniform1f(aExp, P.EXPOSURE);
        glUniform3f(aFogCol, P.FOG_R, P.FOG_G, P.FOG_B); glUniform3f(aTint, P.TINT_R, P.TINT_G, P.TINT_B);
        glUniform1f(aDof, dOn ? P.DOF_AMOUNT : 0.0f); glUniform1f(aFocus, P.DOF_FOCUS); glUniform1f(aFR, P.DOF_RANGE); glUniform2f(aPx, 1.0f / scrW, 1.0f / scrH);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (fOn || dOn) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, o1);
            glActiveTexture(GL_TEXTURE0);
        }
    }
    if (bOn) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
        glUseProgram(pAd);
        glBindTexture(GL_TEXTURE_2D, blTex[0]);
        glUniform1i(dTex, 0); glUniform1f(dInt, P.BLOOM_INT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}
