#include <cstdio>
#include <cstdint>
#include <limits>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <irrKlang.h>

#define ASSERT(x) if (!(x)) __debugbreak();
#define GAME_NAME "Space Invaders"
#define VERSION "v0.1"

bool game_running = false;
int move_dir = 0;
bool fire_pressed = 0;
bool reset = 0;
bool game_over = false;
int screen_width = 0;
int screen_height = 0;
bool window_resize = true;
bool render = true;

irrklang::ISoundEngine* SoundEngine = irrklang::createIrrKlangDevice();

#define GL_ERROR_CASE(glerror)\
    case glerror: snprintf(error, sizeof(error), "%s", #glerror)

inline void gl_debug(const char* file, int line) {
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		char error[128];

		switch (err) {
			GL_ERROR_CASE(GL_INVALID_ENUM); break;
			GL_ERROR_CASE(GL_INVALID_VALUE); break;
			GL_ERROR_CASE(GL_INVALID_OPERATION); break;
			GL_ERROR_CASE(GL_INVALID_FRAMEBUFFER_OPERATION); break;
			GL_ERROR_CASE(GL_OUT_OF_MEMORY); break;
		default: snprintf(error, sizeof(error), "%s", "UNKNOWN_ERROR"); break;
		}

		fprintf(stderr, "%s - %s: %d\n", error, file, line);
	}
}

#undef GL_ERROR_CASE

void validate_shader(GLuint shader, const char* file = 0) {
	static const unsigned int BUFFER_SIZE = 512;
	char buffer[BUFFER_SIZE];
	GLsizei length = 0;

	glGetShaderInfoLog(shader, BUFFER_SIZE, &length, buffer);

	if (length > 0) {
		printf("Shader %d(%s) compile error: %s\n", shader, (file ? file : ""), buffer);
	}
}

bool validate_program(GLuint program) {
	static const GLsizei BUFFER_SIZE = 512;
	GLchar buffer[BUFFER_SIZE];
	GLsizei length = 0;

	glGetProgramInfoLog(program, BUFFER_SIZE, &length, buffer);

	if (length > 0) {
		printf("Program %d link error: %s\n", program, buffer);
		return false;
	}

	return true;
}
void updateWindowTitle(GLFWwindow* pWindow, size_t frames, size_t alien_speed)
{ 
		std::stringstream ss;
		ss << GAME_NAME << "  " << VERSION << "  Alien Speed:  " << alien_speed <<  "  [" << frames << " FPS]";

		glfwSetWindowTitle(pWindow, ss.str().c_str());
}

void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error: %s\n", description);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	switch (key) {
	case GLFW_KEY_ESCAPE:
		if (action == GLFW_PRESS) game_running = false;
		break;
	case GLFW_KEY_RIGHT:
		if (action == GLFW_PRESS) move_dir += 1;
		else if (action == GLFW_RELEASE) move_dir -= 1;
		break;
	case GLFW_KEY_LEFT:
		if (action == GLFW_PRESS) move_dir -= 1;
		else if (action == GLFW_RELEASE) move_dir += 1;
		break;
	case GLFW_KEY_SPACE:
		if (action == GLFW_RELEASE) fire_pressed = true;
		break;
	case GLFW_KEY_R:
		if (action == GLFW_RELEASE) reset = true;
		break;
	case GLFW_KEY_G:
		if (action == GLFW_RELEASE) game_over = true;
		break;
	default:
		break;
	}
}
void window_size_callback(GLFWwindow* window, int width, int height)
{
	screen_width = width;
	screen_height = height;
	window_resize = true;
}

struct High_Score
{
	uint32_t hs;
};
void read_high_score(High_Score& high_score)
{
	std::ifstream in("score.dat");
	if (in.good())
		in.read((char*)&high_score, sizeof(high_score));
	else
		high_score.hs = 0;
	in.close();
}
void write_high_score(High_Score& high_score)
{
	std::ofstream out("score.dat");
	if (out.good())
		out.write((char*)&high_score, sizeof(high_score));
	out.close();
}

/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
uint32_t xorshift32(uint32_t* rng)
{
	uint32_t x = *rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*rng = x;
	return x;
}

double random(uint32_t* rng)
{
	return (double)xorshift32(rng) / std::numeric_limits<uint32_t>::max();
}

struct Buffer
{
	size_t width, height;
	uint32_t* data;
};

struct Sprite
{
	size_t width, height;
	uint32_t color;
	uint8_t* data;
};

struct Alien
{
	size_t x, y;
	size_t type;
};

struct Bullet
{
	size_t x, y;
	int dir;
};

struct Player
{
	size_t x, y;
	size_t life;
};

#define GAME_MAX_BULLETS 128

struct Game
{
	size_t width, height;
	size_t num_aliens;
	size_t num_bullets;
	Alien* aliens;
	Player player;
	Bullet bullets[GAME_MAX_BULLETS];
};

struct SpriteAnimation
{
	bool loop;
	size_t num_frames;
	size_t frame_duration;
	size_t time;
	Sprite** frames;
};

enum AlienType : uint8_t
{
	ALIEN_DEAD = 0,
	ALIEN_TYPE_A = 1,
	ALIEN_TYPE_B = 2,
	ALIEN_TYPE_C = 3
};

void buffer_clear(Buffer* buffer, uint32_t color)
{
	for (size_t i = 0; i < buffer->width * buffer->height; ++i)
	{
		buffer->data[i] = color;
	}
}

bool sprite_overlap_check(
	const Sprite& sp_a, size_t x_a, size_t y_a,
	const Sprite& sp_b, size_t x_b, size_t y_b
)
{
	// NOTE: For simplicity we just check for overlap of the sprite
	// rectangles. Instead, if the rectangles overlap, we should
	// further check if any pixel of sprite A overlap with any of
	// sprite B.
	if (x_a < x_b + sp_b.width && x_a + sp_a.width > x_b &&
		y_a < y_b + sp_b.height && y_a + sp_a.height > y_b)
	{
		return true;
	}

	return false;
}

void buffer_draw_sprite(Buffer* buffer, const Sprite& sprite, size_t x, size_t y, uint32_t color = 0)
{
	if (!color)
		color = sprite.color;
	for (size_t xi = 0; xi < sprite.width; ++xi)
	{
		for (size_t yi = 0; yi < sprite.height; ++yi)
		{
			if (sprite.data[yi * sprite.width + xi] &&
				(sprite.height - 1 + y - yi) < buffer->height &&
				(x + xi) < buffer->width)
			{
				buffer->data[(sprite.height - 1 + y - yi) * buffer->width + (x + xi)] = color;
			}
		}
	}
}

void buffer_draw_number(
	Buffer* buffer,
	const Sprite& number_spritesheet, size_t number,
	size_t x, size_t y,
	uint32_t color)
{
	uint8_t digits[64];
	size_t num_digits = 0;

	size_t current_number = number;
	do
	{
		digits[num_digits++] = current_number % 10;
		current_number = current_number / 10;
	} while (current_number > 0);

	size_t xp = x;
	size_t stride = number_spritesheet.width * number_spritesheet.height;
	Sprite sprite = number_spritesheet;
	for (size_t i = 0; i < num_digits; ++i)
	{
		uint8_t digit = digits[num_digits - i - 1];
		sprite.data = number_spritesheet.data + digit * stride;
		buffer_draw_sprite(buffer, sprite, xp, y, color);
		xp += sprite.width + 1;
	}
}

void buffer_draw_text(
	Buffer* buffer,
	const Sprite& text_spritesheet,
	const char* text,
	size_t x, size_t y,
	uint32_t color)
{
	size_t xp = x;
	size_t stride = text_spritesheet.width * text_spritesheet.height;
	Sprite sprite = text_spritesheet;
	for (const char* charp = text; *charp != '\0'; ++charp)
	{
		char character = *charp - 32;
		if (character < 0 || character >= 65) continue;

		sprite.data = text_spritesheet.data + character * stride;
		buffer_draw_sprite(buffer, sprite, xp, y, color);
		xp += sprite.width + 1;
	}
}

uint32_t rgb_to_uint32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return (r << 24) | (g << 16) | (b << 8) | a;
}

int main(int argc, char* argv[])
{
	const size_t buffer_width = 224;
	const size_t buffer_height = 256;

	glfwSetErrorCallback(error_callback);

	if (!glfwInit()) return -1;

	screen_width = int(glfwGetVideoMode(glfwGetPrimaryMonitor())->width / 1.25);
	screen_height = int(glfwGetVideoMode(glfwGetPrimaryMonitor())->height / 1.25);



	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	/* Create a windowed mode window and its OpenGL context */
	GLFWwindow* window = glfwCreateWindow(screen_width, screen_height, GAME_NAME, NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		return -1;
	}
	//center initial window to screen
	glfwSetWindowPos(window, (glfwGetVideoMode(glfwGetPrimaryMonitor())->width-screen_width)/2, (glfwGetVideoMode(glfwGetPrimaryMonitor())->height-screen_height)/2);

	//Hide Mouse Cursor, Still allows mouse to exit window
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
	glfwSetKeyCallback(window, key_callback);
	glfwSetWindowSizeCallback(window, window_size_callback);

	glfwMakeContextCurrent(window);

	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		fprintf(stderr, "Error initializing GLEW.\n");
		glfwTerminate();
		return -1;
	}

	int glVersion[2] = { -1, 1 };
	glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]);
	glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]);

	gl_debug(__FILE__, __LINE__);

	printf("Using OpenGL: %d.%d\n", glVersion[0], glVersion[1]);
	printf("Renderer used: %s\n", glGetString(GL_RENDERER));
	printf("Shading Language: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	//change to 1 to enable vsync
	glfwSwapInterval(0);

	glClearColor(1.0, 0.0, 0.0, 1.0);

	// Create graphics buffer
	Buffer buffer;
	buffer.width = buffer_width;
	buffer.height = buffer_height;
	buffer.data = new uint32_t[buffer.width * buffer.height];

	buffer_clear(&buffer, 0);

	// Create texture for presenting buffer to OpenGL
	GLuint buffer_texture;
	glGenTextures(1, &buffer_texture);
	glBindTexture(GL_TEXTURE_2D, buffer_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, buffer.width, buffer.height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buffer.data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


	// Create vao for generating fullscreen triangle
	GLuint fullscreen_triangle_vao;
	glGenVertexArrays(1, &fullscreen_triangle_vao);


	// Create shader for displaying buffer
	static const char* fragment_shader =
		"\n"
		"#version 330\n"
		"\n"
		"uniform sampler2D buffer;\n"
		"noperspective in vec2 TexCoord;\n"
		"\n"
		"out vec3 outColor;\n"
		"\n"
		"void main(void){\n"
		"    outColor = texture(buffer, TexCoord).rgb;\n"
		"}\n";

	static const char* vertex_shader =
		"\n"
		"#version 330\n"
		"\n"
		"noperspective out vec2 TexCoord;\n"
		"\n"
		"void main(void){\n"
		"\n"
		"    TexCoord.x = (gl_VertexID == 2)? 2.0: 0.0;\n"
		"    TexCoord.y = (gl_VertexID == 1)? 2.0: 0.0;\n"
		"    \n"
		"    gl_Position = vec4(2.0 * TexCoord - 1.0, 0.0, 1.0);\n"
		"}\n";

	GLuint shader_id = glCreateProgram();

	{
		//Create vertex shader
		GLuint shader_vp = glCreateShader(GL_VERTEX_SHADER);

		glShaderSource(shader_vp, 1, &vertex_shader, 0);
		glCompileShader(shader_vp);
		validate_shader(shader_vp, vertex_shader);
		glAttachShader(shader_id, shader_vp);

		glDeleteShader(shader_vp);
	}

	{
		//Create fragment shader
		GLuint shader_fp = glCreateShader(GL_FRAGMENT_SHADER);

		glShaderSource(shader_fp, 1, &fragment_shader, 0);
		glCompileShader(shader_fp);
		validate_shader(shader_fp, fragment_shader);
		glAttachShader(shader_id, shader_fp);

		glDeleteShader(shader_fp);
	}

	glLinkProgram(shader_id);

	if (!validate_program(shader_id)) {
		fprintf(stderr, "Error while validating shader.\n");
		glfwTerminate();
		glDeleteVertexArrays(1, &fullscreen_triangle_vao);
		delete[] buffer.data;
		return -1;
	}

	glUseProgram(shader_id);

	GLint location = glGetUniformLocation(shader_id, "buffer");
	glUniform1i(location, 0);


	//OpenGL setup
	glDisable(GL_DEPTH_TEST);
	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(fullscreen_triangle_vao);

	// Prepare game
	Sprite alien_sprites[6];

	alien_sprites[0].width = 8;
	alien_sprites[0].height = 8;
	alien_sprites[0].color = rgb_to_uint32(255, 154, 0);
	alien_sprites[0].data = new uint8_t[64]
	{
		0,0,0,1,1,0,0,0, // ...@@...
		0,0,1,1,1,1,0,0, // ..@@@@..
		0,1,1,1,1,1,1,0, // .@@@@@@.
		1,1,0,1,1,0,1,1, // @@.@@.@@
		1,1,1,1,1,1,1,1, // @@@@@@@@
		0,1,0,1,1,0,1,0, // .@.@@.@.
		1,0,0,0,0,0,0,1, // @......@
		0,1,0,0,0,0,1,0  // .@....@.
	};

	alien_sprites[1].width = 8;
	alien_sprites[1].height = 8;
	alien_sprites[1].color = rgb_to_uint32(255, 154, 0);
	alien_sprites[1].data = new uint8_t[64]
	{
		0,0,0,1,1,0,0,0, // ...@@...
		0,0,1,1,1,1,0,0, // ..@@@@..
		0,1,1,1,1,1,1,0, // .@@@@@@.
		1,1,0,1,1,0,1,1, // @@.@@.@@
		1,1,1,1,1,1,1,1, // @@@@@@@@
		0,0,1,0,0,1,0,0, // ..@..@..
		0,1,0,1,1,0,1,0, // .@.@@.@.
		1,0,1,0,0,1,0,1  // @.@..@.@
	};

	alien_sprites[2].width = 11;
	alien_sprites[2].height = 8;
	alien_sprites[2].color = rgb_to_uint32(0, 120, 255);
	alien_sprites[2].data = new uint8_t[88]
	{
		0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
		0,0,0,1,0,0,0,1,0,0,0, // ...@...@...
		0,0,1,1,1,1,1,1,1,0,0, // ..@@@@@@@..
		0,1,1,0,1,1,1,0,1,1,0, // .@@.@@@.@@.
		1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
		1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
		1,0,1,0,0,0,0,0,1,0,1, // @.@.....@.@
		0,0,0,1,1,0,1,1,0,0,0  // ...@@.@@...
	};

	alien_sprites[3].width = 11;
	alien_sprites[3].height = 8;
	alien_sprites[3].color = rgb_to_uint32(0, 120, 255);
	alien_sprites[3].data = new uint8_t[88]
	{
		0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
		1,0,0,1,0,0,0,1,0,0,1, // @..@...@..@
		1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
		1,1,1,0,1,1,1,0,1,1,1, // @@@.@@@.@@@
		1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
		0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
		0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
		0,1,0,0,0,0,0,0,0,1,0  // .@.......@.
	};

	alien_sprites[4].width = 12;
	alien_sprites[4].height = 8;
	alien_sprites[4].color = rgb_to_uint32(189, 0, 255);
	alien_sprites[4].data = new uint8_t[96]
	{
		0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
		0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
		1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
		1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
		1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
		0,0,0,1,1,0,0,1,1,0,0,0, // ...@@..@@...
		0,0,1,1,0,1,1,0,1,1,0,0, // ..@@.@@.@@..
		1,1,0,0,0,0,0,0,0,0,1,1  // @@........@@
	};


	alien_sprites[5].width = 12;
	alien_sprites[5].height = 8;
	alien_sprites[5].color = rgb_to_uint32(189, 0, 255);
	alien_sprites[5].data = new uint8_t[96]
	{
		0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
		0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
		1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
		1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
		1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
		0,0,1,1,1,0,0,1,1,1,0,0, // ..@@@..@@@..
		0,1,1,0,0,1,1,0,0,1,1,0, // .@@..@@..@@.
		0,0,1,1,0,0,0,0,1,1,0,0  // ..@@....@@..
	};

	Sprite alien_death_sprite;
	alien_death_sprite.width = 13;
	alien_death_sprite.height = 7;
	alien_death_sprite.color = rgb_to_uint32(255, 0, 0);
	alien_death_sprite.data = new uint8_t[91]
	{
		0,1,0,0,1,0,0,0,1,0,0,1,0, // .@..@...@..@.
		0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
		0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
		1,1,0,0,0,0,0,0,0,0,0,1,1, // @@.........@@
		0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
		0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
		0,1,0,0,1,0,0,0,1,0,0,1,0  // .@..@...@..@.
	};

	Sprite player_sprite;
	player_sprite.width = 11;
	player_sprite.height = 7;
	player_sprite.data = new uint8_t[77]
	{
		0,0,0,0,0,1,0,0,0,0,0, // .....@.....
		0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
		0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
		0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
		1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
		1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
		1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
	};


	Sprite text_spritesheet;
	text_spritesheet.width = 5;
	text_spritesheet.height = 7;
	text_spritesheet.data = new uint8_t[65 * 35]
	{
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // ' '
		0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0, // '!'
		0,1,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // '"'
		0,1,0,1,0,0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,0,1,0,1,0, // '#'
		0,0,1,0,0,0,1,1,1,0,1,0,1,0,0,0,1,1,1,0,0,0,1,0,1,0,1,1,1,0,0,0,1,0,0, // '$'
		1,1,0,1,0,1,1,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,1,0,1,0,1,1, // '%'
		0,1,1,0,0,1,0,0,1,0,1,0,0,1,0,0,1,1,0,0,1,0,0,1,0,1,0,0,0,1,0,1,1,1,1, // '&'
		0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // '`'
		0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1, // '{'
		1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0, // '}'
		0,0,1,0,0,1,0,1,0,1,0,1,1,1,0,0,0,1,0,0,0,1,1,1,0,1,0,1,0,1,0,0,1,0,0, // '*'
		0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0, // '+'
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0, // ','
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // '-'
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0, // '.'
		0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0, // '/'

		0,1,1,1,0,1,0,0,0,1,1,0,0,1,1,1,0,1,0,1,1,1,0,0,1,1,0,0,0,1,0,1,1,1,0, // '0'
		0,0,1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,0, // '1'
		0,1,1,1,0,1,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,1,0,0,0,1,0,0,0,0,1,1,1,1,1, // '2'
		1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // '3'
		0,0,0,1,0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,1,0, // '4'
		1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // '5'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // '6'
		1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0, // '7'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // '8'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // '9'

		0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0, // ':'
		0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0, // ';'
		0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1, // '<'
		0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0, // '='
		1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0, // '>'
		0,1,1,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0, // '?'
		0,1,1,1,0,1,0,0,0,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,0,1,0,0,0,1,0,1,1,1,0, // '@'

		0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,0,0,0,1, // 'A'
		1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0, // 'B'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,1,1,0, // 'C'
		1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0, // 'D'
		1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1, // 'E'
		1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0, // 'F'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,1,0,1,1,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // 'G'
		1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1, // 'H'
		0,1,1,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,0, // 'I'
		0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // 'J'
		1,0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,1,0,0,0,1,0,1,0,0,1,0,0,1,0,1,0,0,0,1, // 'K'
		1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1, // 'L'
		1,0,0,0,1,1,1,0,1,1,1,0,1,0,1,1,0,1,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1, // 'M'
		1,0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,0,1,0,1,1,0,0,1,1,1,0,0,0,1,1,0,0,0,1, // 'N'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // 'O'
		1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0, // 'P'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,0,0,1,1,0,1,1,1,1, // 'Q'
		1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,1,0,1,0,0,1,0,0,1,0,1,0,0,0,1, // 'R'
		0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,0,1,1,1,0,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // 'S'
		1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0, // 'T'
		1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0, // 'U'
		1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0, // 'V'
		1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,0,1,0,1,1,1,0,1,1,1,0,0,0,1, // 'W'
		1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,1,0,0,0,1, // 'X'
		1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0, // 'Y'
		1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,1,1,1,1, // 'Z'

		0,0,0,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,1, // ']'
		0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0, // '\'
		1,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,0, // ']'
		0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // '^'
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1, // '_'
		0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  // '''
	};

	Sprite number_spritesheet = text_spritesheet;
	number_spritesheet.data += 16 * 35;

	Sprite player_bullet_sprite;
	player_bullet_sprite.width = 1;
	player_bullet_sprite.height = 3;
	player_bullet_sprite.data = new uint8_t[3]
	{
		1, 1, 1
	};

	Sprite alien_bullet_sprite[2];
	alien_bullet_sprite[0].width = 3;
	alien_bullet_sprite[0].height = 7;
	alien_bullet_sprite[0].data = new uint8_t[21]
	{
		0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,0,
	};

	alien_bullet_sprite[1].width = 3;
	alien_bullet_sprite[1].height = 7;
	alien_bullet_sprite[1].data = new uint8_t[21]
	{
		0,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,
	};


	SpriteAnimation alien_bullet_animation;
	alien_bullet_animation.loop = true;
	alien_bullet_animation.num_frames = 2;
	alien_bullet_animation.frame_duration = 5;
	alien_bullet_animation.time = 0;

	alien_bullet_animation.frames = new Sprite * [2];
	alien_bullet_animation.frames[0] = &alien_bullet_sprite[0];
	alien_bullet_animation.frames[1] = &alien_bullet_sprite[1];

	SpriteAnimation alien_animation[3];

	size_t alien_update_frequency = 120;

	for (size_t i = 0; i < 3; ++i)
	{
		alien_animation[i].loop = true;
		alien_animation[i].num_frames = 2;
		alien_animation[i].frame_duration = alien_update_frequency;
		alien_animation[i].time = 0;

		alien_animation[i].frames = new Sprite * [2];
		alien_animation[i].frames[0] = &alien_sprites[2 * i];
		alien_animation[i].frames[1] = &alien_sprites[2 * i + 1];
	}

	Game game;
	game.width = buffer_width;
	game.height = buffer_height;
	game.num_bullets = 0;
	game.num_aliens = 55;
	game.aliens = new Alien[game.num_aliens];

	game.player.x = 112 - 5;
	game.player.y = 32;

	game.player.life = 3;
	std::string move_audio[4] = {
		"audio/move1.wav",
		"audio/move2.wav",
		"audio/move3.wav",
		"audio/move4.wav"
	};
	size_t move_audio_i = 0;

	size_t alien_swarm_position = 24;
	size_t alien_swarm_max_position = game.width - 16 * 11 - 3;

	size_t aliens_killed = 0;
	size_t alien_update_timer = 0;
	bool should_change_speed = false;

	for (size_t xi = 0; xi < 11; ++xi)
	{
		for (size_t yi = 0; yi < 5; ++yi)
		{
			Alien& alien = game.aliens[xi * 5 + yi];
			alien.type = (5 - int(yi)) / 2 + 1;
			const Sprite& sprite = alien_sprites[2 * (alien.type - 1)];

			alien.x = 16 * xi + alien_swarm_position + (alien_death_sprite.width - sprite.width) / 2;
			alien.y = 17 * yi + 128;
		}
	}

	uint8_t* death_counters = new uint8_t[game.num_aliens];
	for (size_t i = 0; i < game.num_aliens; ++i)
	{
		death_counters[i] = 10;
	}

	const uint32_t alien_color = rgb_to_uint32(255, 255, 255); // White
	const uint32_t player_color = rgb_to_uint32(0, 255, 0); // Green
	const uint32_t red_color = rgb_to_uint32(255, 0, 0); // Red
	const uint32_t clear_color = rgb_to_uint32(0, 0, 30); // Navy BLue

	uint32_t rng = 13;

	int alien_move_dir = 4;

	High_Score high_score;
	read_high_score(high_score);
	size_t score = 0;
	size_t level = 1;

	game_running = true;

	int player_move_dir = 0;
	static double limitFPS = 1.0 / 60.0;

	double lastTime = glfwGetTime(), timer = lastTime;
	double deltaTime = 0, nowTime = 0;
	size_t frames = 0, updates = 0;


	// - While window is alive
	while (!glfwWindowShouldClose(window) && game_running) {

		// - Measure time
		nowTime = glfwGetTime();
		deltaTime += (nowTime - lastTime) / limitFPS;
		lastTime = nowTime;

		// - Only update at 60 frames / s
		while (deltaTime >= 1.0) {
			updates++;
			deltaTime--;

			if (window_resize)
			{
				GLsizei my_ratio = screen_height / buffer_height;
				GLsizei my_width = buffer_width * my_ratio;
				GLsizei black_bar = (screen_width - my_width) / 2;
				glViewport(black_bar, 0, my_width, screen_height);
				window_resize = false;
			}
			buffer_clear(&buffer, clear_color);

			const int text_border_offset = 10;
			const int score_txt_width = std::string("SCORE").length() * (text_spritesheet.width + 1);
			int score_txt_pos = text_border_offset;
			int score_width = std::to_string(score).length() * (number_spritesheet.width + 1);
			int score_pos = score_txt_pos + (score_txt_width / 2 - score_width / 2);
			buffer_draw_text(&buffer, text_spritesheet, "SCORE", score_txt_pos, game.height - text_spritesheet.height - 7, red_color);
			buffer_draw_number(&buffer, number_spritesheet, score, score_pos, game.height - 2 * number_spritesheet.height - 12, red_color);

			//Draw High_Score - there is a 1px space between each character
			const int high_score_txt_width = std::string("HIGH SCORE").length() * (text_spritesheet.width + 1);
			int high_score_txt_pos = game.width - text_border_offset - high_score_txt_width;
			int high_score_width = std::to_string(high_score.hs).length() * (number_spritesheet.width + 1);
			int high_score_pos = (game.width - high_score_width) - (high_score_txt_width / 2 - high_score_width / 2) - text_border_offset;
			buffer_draw_text(&buffer, text_spritesheet, "HIGH SCORE", high_score_txt_pos, game.height - text_spritesheet.height - 7, red_color);
			buffer_draw_number(&buffer, number_spritesheet, high_score.hs, high_score_pos, game.height - 2 * number_spritesheet.height - 12, red_color);

			std::string level_text = "LEVEL " + std::to_string(level);
			int level_text_width = level_text.length() * (number_spritesheet.width + 1);
			int level_text_pos = (game.width - level_text_width) - text_border_offset;
			buffer_draw_text(&buffer, text_spritesheet, level_text.c_str() , level_text_pos, text_spritesheet.height, red_color);


			if (game_over)
				game.player.life = 0;

			if (game.player.life == 0)
			{
				game_over = false;

				buffer_draw_text(&buffer, text_spritesheet, "GAME OVER", game.width / 2 - 30, game.height / 2, red_color);
				glTexSubImage2D(
					GL_TEXTURE_2D, 0, 0, 0,
					buffer.width, buffer.height,
					GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
					buffer.data
				);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

				glfwSwapBuffers(window);
				glfwPollEvents();
				if (reset)
					//Exit this IF Statement... will be change properly further down.
					game.player.life = 1;
				continue;
			}

			buffer_draw_number(&buffer, number_spritesheet, game.player.life, 4, 7, red_color);
			size_t xp = 11 + number_spritesheet.width;
			for (size_t i = 0; i < game.player.life - 1; ++i)
			{
				//Lives Sprite
				buffer_draw_sprite(&buffer, player_sprite, xp, 7, player_color);
				xp += player_sprite.width + 2;
			}

			//Line on Bottom
			for (size_t i = 0; i < game.width; ++i)
			{
				buffer.data[game.width * 16 + i] = player_color;
			}


			for (size_t ai = 0; ai < game.num_aliens; ++ai)
			{
				if (death_counters[ai] == 0) continue;

				const Alien& alien = game.aliens[ai];
				if (alien.type == ALIEN_DEAD)
				{
					buffer_draw_sprite(&buffer, alien_death_sprite, alien.x, alien.y);
				}
				else
				{
					const SpriteAnimation& animation = alien_animation[alien.type - 1];
					size_t current_frame = animation.time / animation.frame_duration;
					const Sprite& sprite = *animation.frames[current_frame];
					buffer_draw_sprite(&buffer, sprite, alien.x, alien.y);
				}
			}

			for (size_t bi = 0; bi < game.num_bullets; ++bi)
			{
				const Bullet& bullet = game.bullets[bi];
				const Sprite* sprite;
				if (bullet.dir > 0)
					sprite = &player_bullet_sprite;
				else
				{
					size_t cf = alien_bullet_animation.time / alien_bullet_animation.frame_duration;
					sprite = &alien_bullet_sprite[cf];
				}

				//if player bullet
				if (bullet.dir > 0)
					buffer_draw_sprite(&buffer, *sprite, bullet.x, bullet.y, player_color);
				else
					buffer_draw_sprite(&buffer, *sprite, bullet.x, bullet.y, alien_color);
			}
			buffer_draw_sprite(&buffer, player_sprite, game.player.x, game.player.y, player_color);

			// Simulate bullets
			for (size_t bi = 0; bi < game.num_bullets; ++bi)
			{
				game.bullets[bi].y += game.bullets[bi].dir;
				if (game.bullets[bi].y >= game.height || game.bullets[bi].y < player_bullet_sprite.height)
				{
					game.bullets[bi] = game.bullets[game.num_bullets - 1];
					--game.num_bullets;
					continue;
				}

				// Alien bullet
				if (game.bullets[bi].dir < 0)
				{
					bool overlap = sprite_overlap_check(
						alien_bullet_sprite[0], game.bullets[bi].x, game.bullets[bi].y,
						player_sprite, game.player.x, game.player.y
					);

					if (overlap)
					{
						SoundEngine->play2D("audio/explosion.wav", false);
						--game.player.life;
						game.bullets[bi] = game.bullets[game.num_bullets - 1];
						--game.num_bullets;
						//NOTE: The rest of the frame is still going to be simulated.
						//perhaps we need to check if the game is over or not.
						break;
					}
				}
				// Player bullet
				else
				{
					// Check if player bullet hits an alien bullet
					for (size_t bj = 0; bj < game.num_bullets; ++bj)
					{
						if (bi == bj) continue;

						bool overlap = sprite_overlap_check(
							player_bullet_sprite, game.bullets[bi].x, game.bullets[bi].y,
							alien_bullet_sprite[0], game.bullets[bj].x, game.bullets[bj].y
						);

						if (overlap)
						{
							// NOTE: Make sure it works.
							if (bj == game.num_bullets - 1)
							{
								game.bullets[bi] = game.bullets[game.num_bullets - 2];
							}
							else if (bi == game.num_bullets - 1)
							{
								game.bullets[bj] = game.bullets[game.num_bullets - 2];
							}
							else
							{
								game.bullets[(bi < bj) ? bi : bj] = game.bullets[game.num_bullets - 1];
								game.bullets[(bi < bj) ? bj : bi] = game.bullets[game.num_bullets - 2];
							}
							game.num_bullets -= 2;
							break;
						}
					}

					// Check hit
					for (size_t ai = 0; ai < game.num_aliens; ++ai)
					{
						const Alien& alien = game.aliens[ai];
						if (alien.type == ALIEN_DEAD) continue;

						const SpriteAnimation& animation = alien_animation[alien.type - 1];
						size_t current_frame = animation.time / animation.frame_duration;
						const Sprite& alien_sprite = *animation.frames[current_frame];
						bool overlap = sprite_overlap_check(
							player_bullet_sprite, game.bullets[bi].x, game.bullets[bi].y,
							alien_sprite, alien.x, alien.y
						);

						if (overlap)
						{
							//if top row
							if (game.aliens[ai].type == 1)
								score += 40;
							else
								score += 10 * (4 - game.aliens[ai].type);
							game.aliens[ai].type = ALIEN_DEAD;
							// NOTE: Hack to recenter death sprite
							game.aliens[ai].x -= (alien_death_sprite.width - alien_sprite.width) / 2;
							game.bullets[bi] = game.bullets[game.num_bullets - 1];
							--game.num_bullets;
							++aliens_killed;
							SoundEngine->play2D("audio/invader_killed.wav", false);

							if (aliens_killed % 15 == 0) should_change_speed = true;

							break;
						}
					}
				}
			}

			// Simulate aliens
			if (should_change_speed)
			{
				should_change_speed = false;
				alien_update_frequency /= 2;
				for (size_t i = 0; i < 3; ++i)
				{
					alien_animation[i].frame_duration = alien_update_frequency;
				}
			}

			// Update death counters
			for (size_t ai = 0; ai < game.num_aliens; ++ai)
			{
				const Alien& alien = game.aliens[ai];
				if (alien.type == ALIEN_DEAD && death_counters[ai])
				{
					--death_counters[ai];
				}
			}

			if (alien_update_timer >= alien_update_frequency)
			{
				SoundEngine->play2D(move_audio[move_audio_i].c_str(), false);
				move_audio_i++;
				if (move_audio_i == 4)
					move_audio_i = 0;
				alien_update_timer = 0;

				if ((int)alien_swarm_position + alien_move_dir < 0)
				{
					alien_move_dir *= -1;
					//TODO: Perhaps if aliens get close enough to player, we need to check
					//for overlap. What happens when alien moves over line y = 0 line?
					for (size_t ai = 0; ai < game.num_aliens; ++ai)
					{
						Alien& alien = game.aliens[ai];
						alien.y -= 8;
					}
				}
				else if (alien_swarm_position > alien_swarm_max_position - alien_move_dir)
				{
					alien_move_dir *= -1;
				}
				alien_swarm_position += alien_move_dir;

				for (size_t ai = 0; ai < game.num_aliens; ++ai)
				{
					Alien& alien = game.aliens[ai];
					alien.x += alien_move_dir;
				}

				if (aliens_killed < game.num_aliens)
				{
					size_t rai = game.num_aliens * random(&rng);
					while (game.aliens[rai].type == ALIEN_DEAD)
					{
						rai = game.num_aliens * random(&rng);
					}
					if (game.num_bullets < GAME_MAX_BULLETS) {
						const Sprite& alien_sprite = *alien_animation[game.aliens[rai].type - 1].frames[0];
						game.bullets[game.num_bullets].x = game.aliens[rai].x + alien_sprite.width / 2;
						game.bullets[game.num_bullets].y = game.aliens[rai].y - alien_bullet_sprite[0].height;
						game.bullets[game.num_bullets].dir = -2;
						++game.num_bullets;
					}
				}
			}

			// Update animations
			for (size_t i = 0; i < 3; ++i)
			{
				++alien_animation[i].time;
				if (alien_animation[i].time >= alien_animation[i].num_frames * alien_animation[i].frame_duration)
				{
					alien_animation[i].time = 0;
				}
			}
			++alien_bullet_animation.time;
			if (alien_bullet_animation.time >= alien_bullet_animation.num_frames * alien_bullet_animation.frame_duration)
			{
				alien_bullet_animation.time = 0;
			}

			++alien_update_timer;

			// Simulate player
			player_move_dir = 2 * move_dir;

			if (player_move_dir != 0)
			{
				if (game.player.x + player_sprite.width + player_move_dir >= game.width)
				{
					game.player.x = game.width - player_sprite.width;
				}
				else if ((int)game.player.x + player_move_dir <= 0)
				{
					game.player.x = 0;
				}
				else game.player.x += player_move_dir;
			}

			if (aliens_killed < game.num_aliens && !reset)
			{
				if (score > high_score.hs)
					high_score.hs = score;
				size_t ai = 0;
				while (game.aliens[ai].type == ALIEN_DEAD) ++ai;
				const Sprite& sprite = alien_sprites[2 * (game.aliens[ai].type - 1)];
				size_t pos = game.aliens[ai].x - (alien_death_sprite.width - sprite.width) / 2;
				if (pos > alien_swarm_position) alien_swarm_position = pos;

				ai = game.num_aliens - 1;
				while (game.aliens[ai].type == ALIEN_DEAD) --ai;
				pos = game.width - game.aliens[ai].x - 13 + pos;
				if (pos > alien_swarm_max_position) alien_swarm_max_position = pos;
				ASSERT(alien_swarm_max_position <= buffer_width);
			}
			else
			{
				if (reset)
				{
					reset = false;
					game.player.life = 3;
					score = 0;
					fire_pressed = false;
					level = 0;
				}
				should_change_speed = true;
				level++;
				game.num_bullets = 0;
				alien_swarm_max_position = game.width - 16 * 11 - 3; //Reset max alien width
				if (level <= 8)
					alien_update_frequency = 120 - (level * 10);
				else if (level <= 36) // 120-80-36 = 4 since each level doubles in speed twice, this is maximum speed.
					alien_update_frequency = 120 - 8 * 10 - level;
				else
					alien_update_frequency = 4;

				alien_swarm_position = 24;

				aliens_killed = 0;
				alien_update_timer = 0;

				alien_move_dir = 4;

				for (size_t xi = 0; xi < 11; ++xi)
				{
					for (size_t yi = 0; yi < 5; ++yi)
					{
						size_t ai = xi * 5 + yi;

						death_counters[ai] = 10;

						Alien& alien = game.aliens[ai];
						alien.type = (5 - yi) / 2 + 1;

						const Sprite& sprite = alien_sprites[2 * (alien.type - 1)];

						alien.x = 16 * xi + alien_swarm_position + (alien_death_sprite.width - sprite.width) / 2;
						alien.y = 17 * yi + 128;
					}
				}
			}

			// Process events
			if (fire_pressed && game.num_bullets < GAME_MAX_BULLETS)
			{
				game.bullets[game.num_bullets].x = game.player.x + player_sprite.width / 2;
				game.bullets[game.num_bullets].y = game.player.y + player_sprite.height;
				game.bullets[game.num_bullets].dir = 2;
				++game.num_bullets;
				SoundEngine->play2D("audio/player_shoot.wav", false);
			}
			fire_pressed = false;
			glfwPollEvents();
		}
		// - Render at maximum possible frames

		glTexSubImage2D(
			GL_TEXTURE_2D, 0, 0, 0,
			buffer.width, buffer.height,
			GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
			buffer.data
		);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glfwSwapBuffers(window);
		frames++;


		// - Reset after one second
		if (glfwGetTime() - timer > 1.0) {
			timer++;
			updateWindowTitle(window, frames, alien_update_frequency);
			std::cout << "FPS: " << frames << " Updates:" << updates << std::endl;
			updates = 0, frames = 0;
		}
	}
	write_high_score(high_score);
	glfwDestroyWindow(window);
	glfwTerminate();

	glDeleteVertexArrays(1, &fullscreen_triangle_vao);

	for (size_t i = 0; i < 6; ++i)
	{
		delete[] alien_sprites[i].data;
	}

	delete[] text_spritesheet.data;
	delete[] alien_death_sprite.data;
	delete[] player_bullet_sprite.data;
	delete[] alien_bullet_sprite[0].data;
	delete[] alien_bullet_sprite[1].data;
	delete[] alien_bullet_animation.frames;

	for (size_t i = 0; i < 3; ++i)
	{
		delete[] alien_animation[i].frames;
	}
	delete[] buffer.data;
	delete[] game.aliens;
	delete[] death_counters;
	return 0;
}