// (C) 2026 by Folkert van Heusden
// Released under MIT license

#if defined(USE_IMGUI)
#include "gen.h"
#include <atomic>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
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

void console_imgui::put_char_ll(const char)
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
	uint32_t             c_red_bright = 0;
	uint32_t             c_red_dim    = 0;
	cpu                 *const c      = b->getCpu();

	TTF_Init();
	TTF_Font    *font             = TTF_OpenFont("/usr/share/vlc/skins2/fonts/FreeSans.ttf", 32);

	SDL_Color    white { 255, 255, 255, 255 };
	SDL_Surface *text_address     = TTF_RenderText_Blended(font, "address",    0, white);
	SDL_Surface *text_instruction = TTF_RenderText_Blended(font, "instr.",     0, white);
	SDL_Surface *text_psw         = TTF_RenderText_Blended(font, "psw",        0, white);
	SDL_Surface *text_disk_read   = TTF_RenderText_Blended(font, "disk read",  0, white);
	SDL_Surface *text_disk_write  = TTF_RenderText_Blended(font, "disk write", 0, white);
	SDL_Surface *text_network     = TTF_RenderText_Blended(font, "network",    0, white);
	SDL_Surface *text_mmu_ena     = TTF_RenderText_Blended(font, "mmu enabl.", 0, white);

	uint64_t prev_instr_count = c->get_instructions_executed_count();

	while(!stop && *stop_event != EVENT_TERMINATE) {
		if (panel_w <= 0) {
			SDL_Delay(100);
			continue;
		}

		{
			uint64_t cur_instr_count = c->get_instructions_executed_count();
			uint64_t done_n_instr    = prev_instr_count <= cur_instr_count ? cur_instr_count - prev_instr_count : 0;

			my_unique_lock lck(&loads_lock);
			while(loads.size() >= 500)
				loads.erase(loads.begin());
			loads.push_back(done_n_instr);

			prev_instr_count = cur_instr_count;
		}

		SDL_Surface *new_surface = nullptr;
		{
			my_unique_lock lck(&panel_lock);
			new_surface = SDL_CreateSurface(panel_w, panel_h, pixel_format);
		}

		const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(new_surface->format);
		c_red_bright = SDL_MapRGB(format, nullptr, 255, 0, 0);
		c_red_dim    = SDL_MapRGB(format, nullptr, 16, 0, 0);

		uint16_t           current_PSW   = c->getPSW();
		int                run_mode      = current_PSW >> 14;
		uint16_t           current_PC    = c->getPC();
		memory_addresses_t rc            = b->getMMU()->calculate_physical_address(run_mode, current_PC);
		auto               current_instr = b->peek_word(run_mode, current_PC);

		int pix_w  = new_surface->w * 80 / 100;
		int text_w = new_surface->w * 20 / 100;
		int led_d  = pix_w / 22;
		int text_h = pix_w / 26;
		// address
		for(int i=0; i<22; i++) {
			SDL_Rect rect { 0 + led_d * i, 0, led_d, led_d };
			SDL_FillSurfaceRect(new_surface, &rect, rc.physical_instruction & (1 << (21 - i)) ? c_red_bright : c_red_dim);
		}
		SDL_Rect text_address_to     { 23 * led_d,     0, text_w, text_h };
		SDL_BlitSurface(text_address,     nullptr, new_surface, &text_address_to    );

		// data
		if (current_instr.has_value()) {
			for(int i=0; i<16; i++) {
				SDL_Rect rect { 0 + led_d * i, led_d, led_d, led_d };
				SDL_FillSurfaceRect(new_surface, &rect, current_instr.value() & (1 << (15 - i)) ? c_red_bright : c_red_dim);
			}
		}
		SDL_Rect text_instruction_to { 23 * led_d, led_d, text_w, text_h };
		SDL_BlitSurface(text_instruction, nullptr, new_surface, &text_instruction_to);

		// PSW
		for(int i=0; i<16; i++) {
			SDL_Rect rect { 0 + led_d * i, led_d * 2, led_d, led_d };
			SDL_FillSurfaceRect(new_surface, &rect, current_PSW & (1 << (15 - i)) ? c_red_bright : c_red_dim);
		}
		SDL_Rect text_psw_to { 23 * led_d, led_d * 2, text_w, text_h };
		SDL_BlitSurface(text_psw, nullptr, new_surface, &text_psw_to);

		// activity LEDs
		SDL_Rect rect_dr { 0 + 0, led_d * 3, led_d, led_d };
		SDL_FillSurfaceRect(new_surface, &rect_dr, disk_read_activity_flag ? c_red_bright : c_red_dim);
		disk_read_activity_flag  = false;
		SDL_Rect text_disk_read_to { 23 * led_d, led_d * 3, text_w, text_h };
		SDL_BlitSurface(text_disk_read, nullptr, new_surface, &text_disk_read_to);
		//
		SDL_Rect rect_dw { 0 + 0, led_d * 4, led_d, led_d };
		SDL_FillSurfaceRect(new_surface, &rect_dw, disk_write_activity_flag ? c_red_bright : c_red_dim);
		disk_write_activity_flag  = false;
		SDL_Rect text_disk_write_to { 23 * led_d, led_d * 4, text_w, text_h };
		SDL_BlitSurface(text_disk_write, nullptr, new_surface, &text_disk_write_to);
		//
		SDL_Rect rect_n { 0 + 0, led_d * 5, led_d, led_d };
		SDL_FillSurfaceRect(new_surface, &rect_n, network_activity_flag ? c_red_bright : c_red_dim);
		network_activity_flag  = false;
		SDL_Rect text_network_to { 23 * led_d, led_d * 5, text_w, text_h };
		SDL_BlitSurface(text_network, nullptr, new_surface, &text_network_to);

		// MMU
		SDL_Rect rect_mmu_ena { 0 + 0, led_d * 6, led_d, led_d };
		SDL_FillSurfaceRect(new_surface, &rect_mmu_ena, b->getMMU()->is_enabled() ? c_red_bright : c_red_dim);
		SDL_Rect text_mmu_ena_to { 23 * led_d, led_d * 6, text_w, text_h };
		SDL_BlitSurface(text_mmu_ena, nullptr, new_surface, &text_mmu_ena_to);

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

	SDL_DestroySurface(text_address    );
	SDL_DestroySurface(text_instruction);
	SDL_DestroySurface(text_psw        );
	SDL_DestroySurface(text_disk_read  );
	SDL_DestroySurface(text_disk_write );
	SDL_DestroySurface(text_network    );
	SDL_DestroySurface(text_mmu_ena    );
	TTF_CloseFont(font);
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

	while(!stop && *stop_event != EVENT_TERMINATE) {
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
		ImGui::Begin("Front panel");
		auto         available_space = ImGui::GetContentRegionAvail();
		SDL_Texture *texture         = nullptr;
		{
			my_unique_lock lck(&panel_lock);
			texture  = panel ? SDL_CreateTextureFromSurface(renderer, panel) : nullptr;
			panel_w  = available_space.x;
			panel_h  = available_space.y;
		}
		if (texture)
			ImGui::Image(ImTextureID(intptr_t(texture)), ImVec2(float(texture->w), float(texture->h)));
		ImGui::End();

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

		ImGui::Begin("System load");
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImVec2 dim = ImGui::GetWindowSize();

		{
			uint64_t max_count = 0;
			my_unique_lock lck(&loads_lock);
			for(auto element : loads)
				max_count = std::max(max_count, element);

			if (max_count > 0) {
				auto  n_elem = loads.size();
				float prev_x = 0;
				float prev_y = pos.y + dim.y;
				for(size_t i=0; i<n_elem; i++) {
					float x = pos.x + dim.x * i / n_elem;
					float y = pos.y + dim.y - dim.y * loads[i] / max_count;

					ImGui::GetWindowDrawList()->AddLine(
							{ prev_x, prev_y }, { x, y },
							IM_COL32(255, 0, 0, 255),
							2.f
							);
					prev_x = x;
					prev_y = y;
				}
			}
		}
		ImGui::End();

	        // Rendering
		ImGui::Render();
		SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
		SDL_RenderPresent(renderer);

		if (texture)
			SDL_DestroyTexture(texture);
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
