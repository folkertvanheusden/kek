// (C) 2026 by Folkert van Heusden
// Released under MIT license

#if defined(USE_IMGUI)
#include "gen.h"
#include <atomic>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>

#include "console_imgui.h"


console_imgui::console_imgui(std::atomic_uint32_t *const stop_event): console(stop_event)
{
}

console_imgui::~console_imgui()
{
}

void console_imgui::begin()
{
	th = new std::thread(std::ref(*this));
}

int console_imgui::wait_for_char_ll(const short timeout)
{
}

void console_imgui::put_char_ll(const char c)
{
}

void console_imgui::put_string_lf(const std::string & what)
{
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

void console_imgui::operator()()
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

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);
	style.FontScaleDpi = main_scale;

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer3_Init(renderer);

	while(*stop_event == EVENT_NONE) {
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT)
				*stop_event = EVENT_TERMINATE;
			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
				*stop_event = EVENT_TERMINATE;
		}

		if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
			SDL_Delay(10);
			continue;
		}

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		/// TODO

	        // Rendering
		ImGui::Render();
		SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
		SDL_RenderPresent(renderer);
	}

	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
#endif
