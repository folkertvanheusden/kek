// (C) 2026 by Folkert van Heusden
// Released under MIT license

#if defined(USE_IMGUI)
#include "gen.h"
#include <atomic>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>

#include "console_imgui.h"


console_imgui::console_imgui(std::atomic_uint32_t *const stop_event): console(stop_event)
{
}

console_imgui::~console_imgui()
{
	if (th) {
		stop = true;
		th->join();
		delete th;
	}
}

void console_imgui::begin()
{
	th = new std::thread(&console_imgui::gui_event_loop, this);
}

int console_imgui::wait_for_char_ll(const int timeout)
{
	auto rc = kb_buffer.pop(timeout);
	if (rc.has_value() == false)
		return -1;
	return rc.value();
}

void console_imgui::put_char_ll(const char c)
{
	// TODO trigger refresh
}

void console_imgui::put_string_lf(const std::string & what)
{
	put_string(what + "\r\n");
}

void console_imgui::resize_terminal()
{
}

void console_imgui::panel_update_thread()
{
}

void console_imgui::refresh_virtual_terminal()
{
}

void console_imgui::gui_event_loop()
{
	set_thread_name("IMGUI");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("Error: SDL_Init(): %s\n", SDL_GetError());
		return;
	}

	float           main_scale   = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	SDL_Window     *window       = SDL_CreateWindow("KEK", int(1280 * main_scale), int(800 * main_scale), window_flags);
	if (window == nullptr) {
		printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
		return;
	}
	SDL_Renderer   *renderer     = SDL_CreateRenderer(window, nullptr);
	if (renderer == nullptr) {
		SDL_Log("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
		return;
	}
	// SDL_SetRenderVSync(renderer, 1);
	SDL_ShowWindow(window);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.Fonts->AddFontDefaultVector();

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);
	style.FontScaleDpi = main_scale;

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer3_Init(renderer);

	while(!stop) {
		SDL_Delay(1);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT) 
				stop = true;
			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
				stop = true;
			if (event.type == SDL_EVENT_KEY_DOWN) {
				SDL_Keycode keycode = SDL_GetKeyFromScancode(event.key.scancode, event.key.mod, false);
				if (keycode < 127)
					kb_buffer.push(keycode);
			}
		}

		if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
			SDL_Delay(10);
			continue;
		}

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		/// TODO
		ImGui::Begin("Terminal");
		char *buffer_copy = new char[t_width * t_height];
		for(int i=0; i<t_width * t_height; i++)
			buffer_copy[i] = screen_buffer[i] ? screen_buffer[i] : ' ';
		for(int y=0; y<t_height; y++) {
			auto offset = y * t_width;
			ImGui::TextUnformatted(&buffer_copy[offset], &buffer_copy[offset + t_width]);
		}
		delete [] buffer_copy;
		ImGui::End();

	        // Rendering
		ImGui::Render();
		SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
		SDL_RenderPresent(renderer);
	}

	*stop_event = EVENT_TERMINATE;

	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
#endif
