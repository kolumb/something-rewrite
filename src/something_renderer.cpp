#include "./something_renderer.hpp"

bool Renderer::gl_compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader)
{
    const auto source = unwrap_or_panic(
                            read_file_as_string_view(file_path, &shader_buffer),
                            "Could not read file `", file_path, "`: ",
                            strerror(errno));
    const GLint source_size = static_cast<GLint>(source.count);

    *shader = glCreateShader(shader_type);
    glShaderSource(*shader, 1, &source.data, &source_size);
    glCompileShader(*shader);

    GLint compiled;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLchar log[1024];
        GLsizei log_size = 0;
        glGetShaderInfoLog(*shader, sizeof(log), &log_size, log);

        const String_View log_sv = {
            (size_t) log_size,
            log
        };

        println(stderr, "Failed to compile ", file_path, ":", log_sv);
        return false;
    }

    return true;
}

bool Renderer::gl_link_program(GLuint *shader, size_t shader_size, GLuint *program)
{
    *program = glCreateProgram();

    for (size_t i = 0; i < shader_size; ++i) {
        glAttachShader(*program, shader[i]);
    }
    glLinkProgram(*program);

    GLint linked = 0;
    glGetProgramiv(*program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLsizei log_size = 0;
        GLchar log[1024];
        glGetProgramInfoLog(*program, sizeof(log), &log_size, log);

        const String_View log_sv = {
            (size_t) log_size,
            log
        };

        panic("Failed to link the shader program: ", log_sv);
    }

    return program;
}

void Renderer::fill_rect(AABB<float> aabb, RGBA shade, int atlas_index, Flip flip)
{
    Triangle<GLfloat> lower, upper;
    aabb.split_into_triangles(&lower, &upper);

    if (atlas_index < 0) {
        Triangle<GLfloat> no_uv;
        fill_triangle(lower, shade, no_uv);
        fill_triangle(upper, shade, no_uv);
    } else {
        Triangle<GLfloat> lower_uv, upper_uv;
        assert((size_t) atlas_index < atlas.uvs.size);
        auto uv = atlas.uvs.data[atlas_index];

        if (flip & Flip::HORIZONTALLY()) {
            uv = uv.flip_horizontally();
        }

        if (flip & Flip::VERTICALLY()) {
            uv = uv.flip_vertically();
        }

        uv.split_into_triangles(&lower_uv, &upper_uv);

        fill_triangle(lower, shade, lower_uv);
        fill_triangle(upper, shade, upper_uv);
    }
}

void Renderer::fill_triangle(Triangle<GLfloat> triangle, RGBA rgba, Triangle<GLfloat> uv)
{
    // NOTE: I'm not sure if we should ignore the call if the buffer is full or crash.
    // Crash can help to troubleshoot disappearing triangles problem in the future.
    assert(batch_buffer_size < BATCH_BUFFER_CAPACITY);
    triangles_buffer[batch_buffer_size] = triangle;
    colors_buffer[batch_buffer_size][0] = rgba;
    colors_buffer[batch_buffer_size][1] = rgba;
    colors_buffer[batch_buffer_size][2] = rgba;
    uv_buffer[batch_buffer_size] = uv;
    batch_buffer_size += 1;
}

bool Renderer::reload_shaders()
{
    rect_program_failed = true;

    println(stderr, "LOG: compiling the shader program");
    glDeleteProgram(rect_program);
    // Compiling The Shader Program
    {
        GLuint shaders[2] = {0};

        if (!gl_compile_shader_file("./assets/shaders/rect.vert", GL_VERTEX_SHADER,
                                    shaders)) {
            return false;
        }

        if (!gl_compile_shader_file("./assets/shaders/rect.frag", GL_FRAGMENT_SHADER,
                                    shaders + 1)) {
            return false;
        }

        if (!gl_link_program(shaders, sizeof(shaders) / sizeof(shaders[0]), &rect_program)) {
            return false;
        }
    }
    glUseProgram(rect_program);

    // Uniforms
    u_atlas           = glGetUniformLocation(rect_program, "atlas");
    u_resolution      = glGetUniformLocation(rect_program, "resolution");
    u_camera_position = glGetUniformLocation(rect_program, "camera_position");
    u_camera_z        = glGetUniformLocation(rect_program, "camera_z");
    u_time            = glGetUniformLocation(rect_program, "time");

    rect_program_failed = false;

    return true;
}

void Renderer::init(const char *atlas_conf_path)
{
    // TODO(#19): it's impossible to build an atlas with 0 margin
    // It triggers some asserts.
    atlas = Atlas::from_config(atlas_conf_path, 10);

    // Compiling The Shader Program
    reload_shaders();

    println(stderr, "LOG: initializing vertex position attribute");
    // Initializing Vertex Position Attribute
    {
        const size_t V2_COMPONENTS = 2;
        glGenBuffers(1, &triangles_buffer_id);
        glBindBuffer(GL_ARRAY_BUFFER, triangles_buffer_id);
        {
            const size_t TRIANGLE_VERTICES = 3;
            static_assert(
                sizeof(triangles_buffer) == sizeof(GLfloat) * V2_COMPONENTS * TRIANGLE_VERTICES * BATCH_BUFFER_CAPACITY,
                "Looks like compiler did an oopsie-doopsie and padded something incorrectly in the Triangle or V2 structures");
        }
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(triangles_buffer),
                     triangles_buffer,
                     GL_DYNAMIC_DRAW);
        const GLint vertex_position = 1;
        println(stderr, "vertex_position: ", vertex_position);
        glEnableVertexAttribArray(vertex_position);

        glVertexAttribPointer(
            vertex_position,    // index
            V2_COMPONENTS,      // numComponents
            GL_FLOAT,           // type
            0,                  // normalized
            0,                  // stride
            0                   // offset
        );
    }

    println(stderr, "LOG: initializing vertex color attribute");
    // Initializing Vertex Color Attribute
    {
        const size_t RGBA_COMPONENTS = 4;
        glGenBuffers(1, &colors_buffer_id);
        glBindBuffer(GL_ARRAY_BUFFER, colors_buffer_id);
        {
            const size_t TRIANGLE_VERTICES = 3;
            static_assert(
                sizeof(colors_buffer) == sizeof(GLfloat) * RGBA_COMPONENTS * TRIANGLE_VERTICES * BATCH_BUFFER_CAPACITY,
                "Looks like compiler did an oopsie-doopsie and padded something incorrectly in the RGBA structure");
        }
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(colors_buffer),
                     colors_buffer,
                     GL_DYNAMIC_DRAW);
        const GLint vertex_color = 2;
        println(stderr, "vertex_color: ", vertex_color);

        glEnableVertexAttribArray(vertex_color);
        glVertexAttribPointer(
            vertex_color,       // index
            RGBA_COMPONENTS,    // numComponents
            GL_FLOAT,           // type
            0,                  // normalized
            0,                  // stride
            0                   // offset
        );
    }

    println(stderr, "LOG: initializing UV position attribute");
    // Initializing Vertex UV Attribute
    {
        const size_t V2_COMPONENTS = 2;
        glGenBuffers(1, &uv_buffer_id);
        glBindBuffer(GL_ARRAY_BUFFER, uv_buffer_id);
        {
            const size_t TRIANGLE_VERTICES = 3;
            static_assert(
                sizeof(uv_buffer) == sizeof(GLfloat) * V2_COMPONENTS * TRIANGLE_VERTICES * BATCH_BUFFER_CAPACITY,
                "Looks like compiler did an oopsie-doopsie and padded something incorrectly in the Triangle or V2 structures");
        }
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(uv_buffer),
                     uv_buffer,
                     GL_DYNAMIC_DRAW);
        const GLint vertex_uv = 3;
        println(stderr, "vertex_uv: ", vertex_uv);
        glEnableVertexAttribArray(vertex_uv);

        glVertexAttribPointer(
            vertex_uv,    // index
            V2_COMPONENTS,      // numComponents
            GL_FLOAT,           // type
            0,                  // normalized
            0,                  // stride
            0                   // offset
        );
    }
}

void Renderer::present()
{
    if (!rect_program_failed) {
        glBindBuffer(GL_ARRAY_BUFFER, triangles_buffer_id);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        sizeof(triangles_buffer[0]) * batch_buffer_size,
                        triangles_buffer);

        glBindBuffer(GL_ARRAY_BUFFER, colors_buffer_id);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        sizeof(colors_buffer[0]) * 3 * batch_buffer_size,
                        colors_buffer);

        glBindBuffer(GL_ARRAY_BUFFER, uv_buffer_id);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        sizeof(uv_buffer[0]) * 3 * batch_buffer_size,
                        uv_buffer);

        glUniform2f(u_resolution, SCREEN_WIDTH, SCREEN_HEIGHT);
        glDrawArrays(GL_TRIANGLES,
                     0,
                     static_cast<GLsizei>(batch_buffer_size) * 3 * 2);
    }

    batch_buffer_size = 0;
}

