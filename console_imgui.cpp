// (C) 2026 by Folkert van Heusden
// Released under MIT license

#if defined(USE_IMGUI)
#include "gen.h"
#include <atomic>
#include <SDL3/SDL.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"

#include "bus.h"
#include "console_imgui.h"
#include "cpu.h"


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
	set_thread_name("panel");

	constexpr const auto pixel_format = SDL_PIXELFORMAT_ARGB8888;
	uint32_t             c_red_bright = 0xff0000;  // FIXME
	uint32_t             c_red_low    = 0x0f0000;  // FIXME

	cpu                 *const c      = b->getCpu();

	while(!stop) {
		if (panel_w <= 0) {
			SDL_Delay(100);
			continue;
		}

		SDL_Surface *new_surface = nullptr;
		{
			my_unique_lock lck(&panel_lock);
			new_surface = SDL_CreateSurface(panel_w, panel_h, pixel_format);
		}

		uint16_t           current_PSW   = c->getPSW();
		int                run_mode      = current_PSW >> 14;
		uint16_t           current_PC    = c->getPC();
		memory_addresses_t rc            = b->getMMU()->calculate_physical_address(run_mode, current_PC);
		auto               current_instr = b->peek_word(run_mode, current_PC);

		int pix_w = new_surface->w * 80 / 100;
		int led_d = pix_w / 22;
		printf("%d %d %d\n", new_surface->w, pix_w, led_d);
		for(int i=0; i<22; i++) {
			SDL_Rect rect { 0 + led_d * i, 0, led_d, led_d };
			SDL_FillSurfaceRect(new_surface, &rect, rc.physical_instruction & (1 << i) ? c_red_bright : c_red_low);
		}

		{
			my_unique_lock lck(&panel_lock);
			SDL_DestroySurface(panel);
			panel = new_surface;
		}

		SDL_Delay(1000 / refreshrate);
	}

	my_unique_lock lck(&panel_lock);
	SDL_DestroySurface(panel);
	panel = nullptr;
}

void console_imgui::refresh_virtual_terminal()
{
}

void console_imgui::gui_event_loop()
{
	set_thread_name("imgui");

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
				if (event.key.mod & SDL_KMOD_CTRL)
					kb_buffer.push(toupper(keycode & 0xff) - 'A' + 1);
				else if (keycode < 127)
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

		///
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

		ImGui::Begin("Front panel");
		SDL_Texture *texture = nullptr;
		{
			my_unique_lock lck(&panel_lock);
			texture = panel ? SDL_CreateTextureFromSurface(renderer, panel) : nullptr;
		}
		if (texture) {
			ImGui::Image(ImTextureID(intptr_t(panel)), ImVec2(float(panel->w), float(panel->h)));
			SDL_DestroyTexture(texture);
		}
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
