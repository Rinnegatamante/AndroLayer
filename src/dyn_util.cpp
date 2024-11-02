// Here we define variants for dynamically loaded functions that need to be used as import resolves
#include "glad/glad.h"

void _glActiveTexture(GLenum texture) {
	glActiveTexture(texture);
}

void _glAttachShader(GLuint prog, GLuint shad) {
	glAttachShader(prog, shad);
}

void _glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
	glBindAttribLocation(program, index, name);
}

void _glBindBuffer(GLenum target, GLuint buffer) {
	glBindBuffer(target, buffer);
}

void _glBindFramebuffer(GLenum target, GLuint framebuffer) {
	glBindFramebuffer(target, framebuffer);
}

void _glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
	glBindRenderbuffer(target, renderbuffer);
}

void _glBindTexture(GLenum target, GLuint texture) {
	glBindTexture(target, texture);
}

void _glBlendFunc(GLenum sfactor, GLenum dfactor) {
	glBlendFunc(sfactor, dfactor);
}

void _glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
	glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
}

void _glBufferData(GLenum target, GLsizei size, const GLvoid *data, GLenum usage) {
	glBufferData(target, size, data, usage);
}

GLenum _glCheckFramebufferStatus(GLenum target) {
	return glCheckFramebufferStatus(target);
}

void _glClear(GLbitfield mask) {
	glClear(mask);
}

void _glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
	glClearColor(red, green, blue, alpha);
}

void _glClearDepthf(GLclampf depth) {
	glClearDepthf(depth);
}

void _glClearStencil(GLint s) {
	glClearStencil(s);
}

void _glCompileShader(GLuint shader) {
	glCompileShader(shader);
}

void _glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
	glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

GLuint _glCreateProgram() {
	return glCreateProgram();
}

GLuint _glCreateShader(GLenum shaderType) {
	return glCreateShader(shaderType);
}

void _glCullFace(GLenum mode) {
	glCullFace(mode);
}

void _glDeleteBuffers(GLsizei n, const GLuint *gl_buffers) {
	glDeleteBuffers(n, gl_buffers);
}

void _glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
	glDeleteFramebuffers(n, framebuffers);
}

void _glDeleteProgram(GLuint prog) {
	glDeleteProgram(prog);
}

void _glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
	glDeleteRenderbuffers(n, renderbuffers);
}

void _glDeleteShader(GLuint shad) {
	glDeleteShader(shad);
}

void _glDeleteTextures(GLsizei n, const GLuint *textures) {
	glDeleteTextures(n, textures);
}

void _glDepthFunc(GLenum func) {
	glDepthFunc(func);
}

void _glDepthMask(GLboolean flag) {
	glDepthMask(flag);
}

void _glDepthRangef(GLfloat nearVal, GLfloat farVal) {
	glDepthRange(nearVal, farVal);
}

void _glDisable(GLenum cap) {
	glDisable(cap);
}

void _glDisableVertexAttribArray(GLuint index) {
	glDisableVertexAttribArray(index);
}

void _glDrawArrays(GLenum mode, GLint first, GLsizei count) {
	glDrawArrays(mode, first, count);
}

void _glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
	glDrawElements(mode, count, type, indices);
}

void _glEnable(GLenum cap) {
	glEnable(cap);
}

void _glEnableVertexAttribArray(GLuint index) {
	glEnableVertexAttribArray(index);
}

void _glFinish() {
	glFinish();
}

void _glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
	glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

void _glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
	glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

void _glFrontFace(GLenum mode) {
	glFrontFace(mode);
}

void _glGenBuffers(GLsizei n, GLuint *buffers) {
	glGenBuffers(n, buffers);
}

void _glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
	glGenFramebuffers(n, framebuffers);
}

void _glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
	glGenRenderbuffers(n, renderbuffers);
}

void _glGenTextures(GLsizei n, GLuint *textures) {
	glGenTextures(n, textures);
}

GLint _glGetAttribLocation(GLuint prog, const GLchar *name) {
	return glGetAttribLocation(prog, name);
}

GLenum _glGetError() {
	return glGetError();
}

void _glGetBooleanv(GLenum pname, GLboolean *params) {
	glGetBooleanv(pname, params);
}

void _glGetIntegerv(GLenum pname, GLint *data) {
	glGetIntegerv(pname, data);
}

void _glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
	glGetProgramInfoLog(program, maxLength, length, infoLog);
}

void _glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
	glGetProgramiv(program, pname, params);
}

void _glGetShaderInfoLog(GLuint handle, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
	glGetShaderInfoLog(handle, maxLength, length, infoLog);
}

void _glGetShaderiv(GLuint handle, GLenum pname, GLint *params) {
	glGetShaderiv(handle, pname, params);
}

const GLubyte *_glGetString(GLenum name) {
	return glGetString(name);
}

GLint _glGetUniformLocation(GLuint prog, const GLchar *name) {
	return glGetUniformLocation(prog, name);
}

void _glHint(GLenum target, GLenum mode) {
	glHint(target, mode);
}

void _glLinkProgram(GLuint progr) {
	glLinkProgram(progr);
}

void _glPolygonOffset(GLfloat factor, GLfloat units) {
	glPolygonOffset(factor, units);
}

void _glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {
	glReadPixels(x, y, width, height, format, type, data);
}

void _glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
	glRenderbufferStorage(target, internalformat, width, height);
}

void _glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
	glScissor(x, y, width, height);
}

void _glShaderSource(GLuint handle, GLsizei count, const GLchar *const *string, const GLint *length) {
	glShaderSource(handle, count, string, length);
}

void _glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) {
	glTexImage2D(target, level, internalFormat, width, height, border, format, type, data);
}

void _glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
	glTexParameterf(target, pname, param);
}

void _glTexParameteri(GLenum target, GLenum pname, GLint param) {
	glTexParameteri(target, pname, param);
}

void _glUniform1f(GLint location, GLfloat v0) {
	glUniform1f(location, v0);
}

void _glUniform1fv(GLint location, GLsizei count, const GLfloat *value) {
	glUniform1fv(location, count, value);
}

void _glUniform1i(GLint location, GLint v0) {
	glUniform1i(location, v0);
}

void _glUniform2fv(GLint location, GLsizei count, const GLfloat *value) {
	glUniform2fv(location, count, value);
}

void _glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
	glUniform3f(location, v0, v2, v2);
}

void _glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
	glUniform3fv(location, count, value);
}

void _glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
	glUniform4fv(location, count, value);
}

void _glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	glUniformMatrix3fv(location, count, transpose, value);
}

void _glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	glUniformMatrix4fv(location, count, transpose, value);
}

void _glUseProgram(GLuint program) {
	glUseProgram(program);
}

void _glVertexAttrib4fv(GLuint index, const GLfloat *v) {
	glVertexAttrib4fv(index, v);
}

void _glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
	glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

void _glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
	glViewport(x, y, width, height);
}
