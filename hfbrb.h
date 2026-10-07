static void h_FbRb(GLenum t, GLenum a, GLenum rbt, GLuint rb) {
    if (isDepth(a) && rb != 0) {
        record(2, a, rb);
        if (phase == 1) {
            GLint oRb = 0, fm = 0, w = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &oRb);
            glBindRenderbuffer(GL_RENDERBUFFER, rb);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, 0x8D44, &fm);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, 0x8D42, &w);
            glBindRenderbuffer(GL_RENDERBUFFER, oRb);
            if (fm == 0x81A6 && w >= 64) { phase = 0; frame = START_FRAME - 2; }
        }
    }
    o_FbRb(t, a, rbt, rb);
}
