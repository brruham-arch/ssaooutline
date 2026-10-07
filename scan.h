#pragma once
static bool scanned = false;
static void scanExisting() {
    GLint old = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old);
    int found = 0;
    const GLenum atts[2] = { GL_DEPTH_ATTACHMENT, 0x821A };
    for (GLuint f = 1; f < 400; f++) {
        if (!glIsFramebuffer(f)) continue;
        o_BindFB(GL_FRAMEBUFFER, f);
        for (int k = 0; k < 2; k++) {
            GLint t = 0, n = 0;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, atts[k], 0x8CD0, &t);
            if (t == (GLint)GL_RENDERBUFFER) {
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, atts[k], 0x8CD1, &n);
                if (n) { record(2, atts[k], (GLuint)n); found++; }
            }
        }
    }
    o_BindFB(GL_FRAMEBUFFER, old);
    glGetError();
    slog("scan FBO lama: %d depth ditemukan", found);
}
