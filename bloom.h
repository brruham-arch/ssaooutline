#pragma once
static GLuint blPBr, blPBl, blPAd, blSrc, blTex[2], blFbo[2];
static int blSt = 0, blW = 0, blH = 0, blSW = 0, blSH = 0, blErr = 0; static void* blCtx = nullptr;
static GLint blUSrc, blUTexel, blUThr, blUBlTex, blUDir, blUAdTex, blUInt;

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

static void blParams() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void bloomDraw(GLuint dst) {
    if (P.BLOOM_INT < 0.001f || blErr >= 3) return;
    void* c = eglGetCurrentContext();
    if (c != blCtx) { blCtx = c; blSt = 0; blSrc = 0; blTex[0] = blTex[1] = 0; blFbo[0] = blFbo[1] = 0; blSW = blSH = 0; blErr = 0; }
    if (blSt == 0) {
        blPBr = makeProgram(VS, FS_BR); blPBl = makeProgram(VS, FS_BL); blPAd = makeProgram(VS, FS_AD);
        if (!blPBr || !blPBl || !blPAd) { blSt = 2; slog("bloom shader gagal"); return; }
        blUSrc = glGetUniformLocation(blPBr, "uScene"); blUTexel = glGetUniformLocation(blPBr, "uTexel");
        blUThr = glGetUniformLocation(blPBr, "uThr");
        blUBlTex = glGetUniformLocation(blPBl, "uTex"); blUDir = glGetUniformLocation(blPBl, "uDir");
        blUAdTex = glGetUniformLocation(blPAd, "uTex"); blUInt = glGetUniformLocation(blPAd, "uInt");
        blSt = 1; slog("bloom shader OK");
    }
    if (blSt != 1) return;
    if (!blSrc) glGenTextures(1, &blSrc);
    if (!blTex[0]) { glGenTextures(2, blTex); glGenFramebuffers(2, blFbo); }
    if (blSW != scrW || blSH != scrH) {
        glBindTexture(GL_TEXTURE_2D, blSrc); blParams();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, scrW, scrH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        blW = scrW / 4; blH = scrH / 4;
        bool ok = true;
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, blTex[i]); blParams();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blW, blH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, blFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blTex[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, dst);
        if (!ok) { blErr = 3; slog("bloom FBO tidak complete"); return; }
        blSW = scrW; blSH = scrH;
    }
    glBindTexture(GL_TEXTURE_2D, blSrc);
    glGetError();
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, scrW, scrH);
    GLenum e = glGetError();
    if (e) { blErr++; slog("bloom copy error 0x%X (%d/3)", e, blErr); return; }

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, blFbo[0]);
    glViewport(0, 0, blW, blH);
    glUseProgram(blPBr);
    glUniform1i(blUSrc, 0); glUniform2f(blUTexel, 1.0f / scrW, 1.0f / scrH); glUniform1f(blUThr, P.BLOOM_THR);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glUseProgram(blPBl);
    glUniform1i(blUBlTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, blFbo[1]);
    glBindTexture(GL_TEXTURE_2D, blTex[0]);
    glUniform2f(blUDir, P.BLOOM_RADIUS / blW, 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, blFbo[0]);
    glBindTexture(GL_TEXTURE_2D, blTex[1]);
    glUniform2f(blUDir, 0.0f, P.BLOOM_RADIUS / blH);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindFramebuffer(GL_FRAMEBUFFER, dst);
    glViewport(0, 0, scrW, scrH);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glUseProgram(blPAd);
    glBindTexture(GL_TEXTURE_2D, blTex[0]);
    glUniform1i(blUAdTex, 0); glUniform1f(blUInt, P.BLOOM_INT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
