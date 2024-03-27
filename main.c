#include <OpenGL/gl3.h>
#include "SDL.h"
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#define MATH_3D_IMPLEMENTATION
#include "math_3d.h"
#include "noise.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// @todo improve lighting calcs, which are a bit bodged
// @todo multi-textures.
// @todo how could I create an infinite world.
// @todo changing MESH_WIDTH and MESH_DEPTH to 1024 seg-faults. Ah, stack overflow, I suspect.

#define MESH_WIDTH 128
#define MESH_DEPTH 128
static const int NUM_TRIANGLES = 2 * (MESH_WIDTH - 1) * (MESH_DEPTH - 1);

int noise_gen_seed;

void gl_check_(const char *file, const int line, const char *expr)
{
    GLenum e;
    while((e = glGetError())) {
        printf("%s: %d '%s', error: %i\n", file, line, expr, e);
    }
}
#if !defined NDEBUG
    #define GLCHECK(expr) do {expr; gl_check_(__FILE__, __LINE__, #expr);} while (false);
#else
    #define GLCHECK(expr) (expr)
#endif

SDL_Window* window = NULL;
SDL_GLContext gl_context = 0;

int screen_width, screen_height;
GLuint vao, vbo, ibo, program;
GLuint rock_texture_id, grass_texture_id, snow_texture_id;

enum Keys {up, down, left, right};
bool keys[4] = {0};

GLfloat pitch = 0.0f, yaw = -90.0f;
vec3_t camera_pos = {0.0f, 1.0f, 2.0f};
vec3_t camera_front = {0.0f, 0.0f, -1.0f};
vec3_t camera_up = {0.0f, 1.0f, 0.0f};

Uint64 clock_freq;

void cleanup()
{
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

GLuint compile_shader(const char* source, GLenum type)
{
    GLuint shader;
    GLCHECK(shader = glCreateShader(type));
    GLCHECK(glShaderSource(shader, 1, &source, NULL));
    GLCHECK(glCompileShader(shader));

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLint info_len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        GLchar* err = malloc(info_len);
        glGetShaderInfoLog(shader, info_len, NULL, err);
        puts(err);
        free(err);
        return 0;
    }

    return shader;
}

void init()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf ("SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    clock_freq = SDL_GetPerformanceFrequency();
    printf("clock frequency: %" PRIu64 "\n", clock_freq);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    window = SDL_CreateWindow("sdl-gl",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              1024,
                              768,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window)
    {
        printf ("SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
    {
        printf ("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        exit(1);
    }

    printf("OpenGL loaded\n");
    printf("Vendor:   %s\n", glGetString(GL_VENDOR));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    printf("Version:  %s\n", glGetString(GL_VERSION));

    SDL_GL_SetSwapInterval(1);

    SDL_GetWindowSize(window, &screen_width, &screen_height);
    GLCHECK(glViewport(0, 0, screen_width, screen_height));

    SDL_ShowCursor(SDL_DISABLE);
    SDL_CaptureMouse(SDL_TRUE);
    SDL_SetWindowGrab(window, SDL_TRUE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
}

GLuint make_texture(const char* filename)
{
    int image_width, image_height;
    stbi_uc* image = stbi_load(filename, &image_width, &image_height, NULL, 3);
    if (!image)
    {
        puts("Failed to load image");
        exit(1);
    }

    GLuint texture;
    GLCHECK(glGenTextures(1, &texture));
    GLCHECK(glBindTexture(GL_TEXTURE_2D, texture));
    GLCHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image_width, image_height, 0, GL_RGB, GL_UNSIGNED_BYTE, image));
    GLCHECK(glGenerateMipmap(GL_TEXTURE_2D));
    GLCHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLCHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GLCHECK(glBindTexture(GL_TEXTURE_2D, 0));
    stbi_image_free(image);
    return texture;
}

float make_height(const GLfloat x, const GLfloat y)
{
    const int num_octaves = 8;
    const float verticality = 0.35f;

    float h = 0.0f;
    for (int i = 0; i < num_octaves; ++i) {
        const float scale = verticality / (pow(2.0f, i));
        const float freq = pow(2.0f, i);
        h += scale * noise2d(x, y, noise_gen_seed, freq);
    }
    return h;
}

void setup()
{
    srand(SDL_GetTicks());
    noise_gen_seed = rand();

    GLCHECK(glEnable(GL_DEPTH_TEST));

    typedef struct {
        vec3_t position;
        vec3_t normal;
        GLfloat texture_coord_x;
        GLfloat texture_coord_y;
    } Vertex;

    Vertex vertices[MESH_WIDTH * MESH_DEPTH] = {0};
    for (int z = 0; z < MESH_DEPTH; ++z) {
        for (int x = 0; x < MESH_WIDTH; ++x) {
            const GLfloat xc = -1.0f + 2.0f * (GLfloat)x / (MESH_WIDTH - 1);
            const GLfloat zc = -1.0f + 2.0f * (GLfloat)z / (MESH_DEPTH - 1);
            const GLfloat yc = make_height(xc, zc);
            vertices[z * MESH_WIDTH + x] = (Vertex){vec3(xc, yc, zc),
                                                    vec3(0, 0, 0),
                                                    8.0f * x / (MESH_WIDTH - 1),
                                                    8.0f * z / (MESH_DEPTH - 1)};


        }
    }
    GLuint indices[NUM_TRIANGLES * 3];
    int triangle = 0;
    for (int z = 0; z < (MESH_DEPTH - 1); ++z) {
        for (int x = 0; x <  (MESH_WIDTH - 1); ++x) {
            indices[3 * triangle] = x + z * MESH_WIDTH;
            indices[3 * triangle + 1] = x + (z + 1) * MESH_WIDTH;
            indices[3 * triangle + 2] = x + z * MESH_WIDTH + 1;
            ++triangle;

            indices[3 * triangle] = x + z * MESH_WIDTH + 1 ;
            indices[3 * triangle + 1] = x + (z + 1) * MESH_WIDTH;
            indices[3 * triangle + 2] = x + (z + 1) * MESH_WIDTH + 1;
            ++triangle;
        }
    }

    // Calculate the normal per surface triangle
    vec3_t surface_normals[NUM_TRIANGLES];
    int vertex_share_count[MESH_WIDTH * MESH_DEPTH] = {0};

    for (int i = 0; i < NUM_TRIANGLES; ++i) {
        const int i1 = indices[i * 3];
        const int i2 = indices[i * 3 + 1];
        const int i3 = indices[i * 3 + 2];

        const vec3_t p1 = vertices[i1].position;
        const vec3_t p2 = vertices[i2].position;
        const vec3_t p3 = vertices[i3].position;

        surface_normals[i] = v3_norm(v3_cross(v3_sub(p2, p1),
                                              v3_sub(p3, p1)));

        ++vertex_share_count[i1];
        ++vertex_share_count[i2];
        ++vertex_share_count[i3];
    }

    for (int i = 0; i < NUM_TRIANGLES; ++i) {
        const int i1 = indices[i * 3];
        const int i2 = indices[i * 3 + 1];
        const int i3 = indices[i * 3 + 2];

        vertices[i1].normal = v3_add(vertices[i1].normal, surface_normals[i]);
        vertices[i2].normal = v3_add(vertices[i2].normal, surface_normals[i]);
        vertices[i3].normal = v3_add(vertices[i3].normal, surface_normals[i]);
    }

    for (int i = 0; i < MESH_WIDTH * MESH_DEPTH; ++i) {
        const int share_count = vertex_share_count[i];
        if (share_count > 0)
            vertices[i].normal = v3_divs(vertices[i].normal,
                                         (float)share_count);
    }

    GLCHECK(glGenVertexArrays(1, &vao));
    GLCHECK(glBindVertexArray(vao));
    GLCHECK(glGenBuffers(1, &vbo));
    GLCHECK(glBindBuffer(GL_ARRAY_BUFFER, vbo));
    GLCHECK(glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW));
    GLCHECK(glGenBuffers(1, &ibo));
    GLCHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo));
    GLCHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW));
    GLCHECK(glEnableVertexAttribArray(0));
    GLCHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)0));
    GLCHECK(glEnableVertexAttribArray(1));
    GLCHECK(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat))));
    GLCHECK(glEnableVertexAttribArray(2));
    GLCHECK(glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat))));
    GLCHECK(glBindVertexArray(0));
    GLCHECK(glBindBuffer(GL_ARRAY_BUFFER, 0));

    static const char* vertex_shader_source =
        "#version 330 core\n"
        "uniform mat4 mvp;\n"
        "layout(location = 0) in vec3 position;\n"
        "layout(location = 1) in vec3 normal;\n"
        "layout(location = 2) in vec2 in_texture_coord;\n"
        "out vec3 vertex_normal;\n"
        "out vec2 texture_coord;\n"
        "out float height;\n"
        "void main()\n"
        "{\n"
        "   gl_Position = mvp * vec4(position, 1.0);\n"
        "   vertex_normal = normal;\n"
        "   texture_coord = in_texture_coord;\n"
        "   height = position.y;\n"
        "}\n";

    GLuint vertex_shader = compile_shader(vertex_shader_source,
                                          GL_VERTEX_SHADER);

    static const char* fragment_shader_source =
        "#version 330 core\n"
        "uniform sampler2D rock;\n"
        "uniform sampler2D snow;\n"
        "uniform sampler2D grass;\n"
        "in vec2 texture_coord;\n"
        "in vec3 vertex_normal;\n"
        "in float height;\n"
        "out vec4 colour;\n"
        "const float ambient_depth = 0.3f;\n"
        "const vec3 up = vec3(0.0f, 1.0f, 0.0f);"
        "void main()\n"
        "{\n"
        "   vec3 lightpos = vec3(0.8f, 1.0f, 0.9f);"
        "   float diffuse = max(0, dot(vertex_normal, normalize(lightpos)));\n"
        "   float angle = max(0, dot(vertex_normal, up));\n"
        "   float snow_amount = 0.0; // smoothstep(0.7, 0.8, angle);\n"
        "   float grass_amount = smoothstep(0.7, 0.8, angle);\n"
        "   grass_amount *= grass_amount;\n"
        "   float rock_amount = max(0, 1 - snow_amount - grass_amount);\n"
        "   vec4 tex = rock_amount * texture(rock, texture_coord) + snow_amount * texture(snow, texture_coord) + grass_amount * texture(grass, texture_coord);\n"
        "   colour = tex * (ambient_depth + diffuse);\n"
        "}\n";

    GLuint fragment_shader = compile_shader(fragment_shader_source,
                                            GL_FRAGMENT_SHADER);

    GLCHECK(program = glCreateProgram());
    GLCHECK(glAttachShader(program, vertex_shader));
    GLCHECK(glAttachShader(program, fragment_shader));
    GLCHECK(glLinkProgram(program));
    GLCHECK(glDeleteShader(fragment_shader));
    GLCHECK(glDeleteShader(vertex_shader));

    rock_texture_id = make_texture("rock.jpg");
    grass_texture_id = make_texture("grass.jpg");
    snow_texture_id = make_texture("snow.jpg");
}

GLfloat deg2rad(const GLfloat d)
{
    return d * M_PI / 180.0f;
}

void handle_mouse_event(const SDL_MouseMotionEvent* e)
{
    static const double sensitivity = 0.2;
    double xrel = e->xrel;
    double yrel = e->yrel;

    xrel *= sensitivity;
    yrel *= sensitivity;

    yaw += (GLfloat)xrel;
    pitch -= (GLfloat)yrel;

    if (pitch > 89.0f)
        pitch = 89.0f;
    else if (pitch < -89.0f)
        pitch = -89.0f;

    const double pr = deg2rad(pitch);
    const double yr = deg2rad(yaw);

    vec3_t front = vec3(cos(yr) * cos(pr),
                        sin(pr),
                        sin(yr) * cos(pr));
    camera_front = v3_norm(front);

}

void do_movement(const GLfloat delta)
{
    const GLfloat speed = delta * 0.001f;

    if (keys[up])
        camera_pos = v3_add(camera_pos, v3_muls(camera_front, speed));

    if (keys[down])
        camera_pos = v3_sub(camera_pos, v3_muls(camera_front, speed));

    if (keys[left])
        camera_pos = v3_sub(camera_pos, v3_muls(v3_norm(v3_cross(camera_front, camera_up)), speed));

    if (keys[right])
        camera_pos = v3_add(camera_pos, v3_muls(v3_norm(v3_cross(camera_front, camera_up)), speed));
}

void run_loop()
{
    SDL_Event event;
    bool quit = false;

    GLCHECK(glClearColor(0.18f, 0.37f, 0.54f, 1.0f));

    Uint64 counter = SDL_GetPerformanceCounter();

    while (!quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        const double delta = 1000.0f * (double)(now - counter) / (double)clock_freq;
        counter = now;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                quit = true;

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                quit = true;

            if (event.type == SDL_MOUSEMOTION)
                handle_mouse_event(&event.motion);

            if (event.type == SDL_KEYDOWN ||  event.type == SDL_KEYUP) {
                const bool value = event.type == SDL_KEYDOWN;
                switch (event.key.keysym.sym) {
                    case SDLK_w: keys[up] = value; break;
                    case SDLK_a: keys[left] = value; break;
                    case SDLK_s: keys[down] = value; break;
                    case SDLK_d: keys[right] = value; break;
                    default: break;
                }
            }
        }

        do_movement(delta);

        mat4_t view = m4_look_at(camera_pos,
                                v3_add(camera_pos, camera_front),
                                camera_up);
        mat4_t projection = m4_perspective(45.0f, (float)screen_width / screen_height, 0.1f, 100.0f);

        GLCHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
        GLCHECK(glUseProgram(program));
        GLCHECK(glBindVertexArray(vao));

        GLCHECK(glActiveTexture(GL_TEXTURE0));
        GLCHECK(glBindTexture(GL_TEXTURE_2D, rock_texture_id));
        GLCHECK(glActiveTexture(GL_TEXTURE1));
        GLCHECK(glBindTexture(GL_TEXTURE_2D, snow_texture_id));
        GLCHECK(glActiveTexture(GL_TEXTURE2));
        GLCHECK(glBindTexture(GL_TEXTURE_2D, grass_texture_id));
        GLCHECK(glUniform1i(glGetUniformLocation(program, "rock"), 0));
        GLCHECK(glUniform1i(glGetUniformLocation(program, "snow"), 1));
        GLCHECK(glUniform1i(glGetUniformLocation(program, "grass"), 2));

        GLCHECK(glEnable(GL_CULL_FACE));
        /*GLCHECK(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));*/

        for (int i = 0; i < 10; ++i) {
            mat4_t model = m4_identity();
            const mat4_t mvp = m4_mul(projection, m4_mul(view, model));
            GLCHECK(glUniformMatrix4fv(glGetUniformLocation(program, "mvp"), 1, GL_FALSE, (GLfloat*)&mvp.m));
            GLCHECK(glDrawElements(GL_TRIANGLES, 3 * NUM_TRIANGLES, GL_UNSIGNED_INT, 0));
        }

        GLCHECK(glBindVertexArray(0));

        SDL_GL_SwapWindow(window);
    }
}

int main(int argc, char **argv)
{
    atexit(cleanup);

    init();
    setup();
    run_loop();

    return 0;
}
