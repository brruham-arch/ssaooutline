#pragma once
static GLuint progV, progF; static int fxSt = 0; static void* fxCtx = nullptr;
static GLint vVig, vSoft, vExp, vTint, fDepth, fNear, fFar, fDens, fStart, fEnd, fCol;

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

static void fxDraw() {
    bool vOn = P.VIG_STRENGTH > 0.001f || P.EXPOSURE < 0.999f || P.TINT_R < 0.999f || P.TINT_G < 0.999f || P.TINT_B < 0.999f;
    bool fOn = P.FOG_DENSITY > 0.001f;
    if (!vOn && !fOn) return;
    void* c = eglGetCurrentContext();
    if (c != fxCtx) { fxCtx = c; fxSt = 0; }
    if (fxSt == 0) {
        progV = makeProgram(VS, FS_V); progF = makeProgram(VS, FS_F);
        if (!progV || !progF) { fxSt = 2; slog("fx shader gagal"); return; }
        vVig = glGetUniformLocation(progV, "uVig"); vSoft = glGetUniformLocation(progV, "uSoft");
        vExp = glGetUniformLocation(progV, "uExp"); vTint = glGetUniformLocation(progV, "uTint");
        fDepth = glGetUniformLocation(progF, "uDepth"); fNear = glGetUniformLocation(progF, "uNear");
        fFar = glGetUniformLocation(progF, "uFar"); fDens = glGetUniformLocation(progF, "uDens");
        fStart = glGetUniformLocation(progF, "uStart"); fEnd = glGetUniformLocation(progF, "uEnd");
        fCol = glGetUniformLocation(progF, "uCol");
        fxSt = 1; slog("fx shader OK");
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
}
