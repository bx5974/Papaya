
#include "ui.h"

#include "components/metrics_window.h"
#include "components/brush.h"
#include "components/color_panel.h"
#include "components/eye_dropper.h"
#include "components/graph_panel.h"
#include "libs/stb_image.h"
#include "libs/stb_image_write.h"
#include "libs/imgui/imgui.h"
#include "libs/mathlib.h"
#include "libs/linmath.h"
#include "libpapaya.h"
#include "pagl.h"
#include "gl_lite.h"
#include <inttypes.h>

static void compile_shaders(PapayaMemory* mem);


void core::resize_doc(PapayaMemory* mem, i32 width, i32 height)
{
    // Free existing texture memory
    if (mem->misc.fbo_sample_tex) {
        GLCHK( glDeleteTextures(1, &mem->misc.fbo_sample_tex) );
    }
    if (mem->misc.fbo_render_tex) {
        GLCHK( glDeleteTextures(1, &mem->misc.fbo_render_tex) );
    }

    // Allocate new memory
    mem->misc.fbo_sample_tex = pagl_alloc_texture(width, height, 0);
    mem->misc.fbo_render_tex = pagl_alloc_texture(width, height, 0);

    // Set up meshes for rendering to texture
    Vec2 sz = Vec2((f32)width, (f32)height);
    resize_brush_meshes(mem->brush, sz);
}

bool core::open_doc(const char* path, PapayaMemory* mem)
{
    timer::start(Timer_ImageOpen);
    // TODO: Implement
    timer::stop(Timer_ImageOpen);
    return true;
}

void core::close_doc(PapayaMemory* mem)
{
    free(mem->doc->nodes);
    free(mem->doc);
}

void core::init(PapayaMemory* mem)
{
    // TODO: Temporary only
    {
        mem->doc = (Document*) calloc(1, sizeof(Document));
        mem->doc->num_nodes = 3;
        mem->doc->nodes = (PapayaNode*) calloc(1, mem->doc->num_nodes *
                                               sizeof(PapayaNode));
        int w0, w1, h0, h1, c0, c1;
        u8* img0 = stbi_load("/home/apoorvaj/Pictures/o0.png", &w0, &h0, &c0, 4);
        u8* img1 = stbi_load("/home/apoorvaj/Pictures/o2.png", &w1, &h1, &c1, 4);

        PapayaNode* n = mem->doc->nodes;
        init_bitmap_node(&n[0], "Base image", img0, w0, h0, c0);
        init_invert_color_node(&n[1], "Color inversion");
        init_bitmap_node(&n[2], "Yellow circle", img1, w1, h1, c1);

        papaya_connect(&n[0].slots[1], &n[1].slots[0]);
        papaya_connect(&n[2].slots[1], &n[1].slots[2]);

        n[0].pos_x = 108; n[0].pos_y = 158;
        n[1].pos_x = 108; n[1].pos_y = 108;
        n[2].pos_x = 158; n[2].pos_y = 158;

        // Create texture
        GLCHK( glGenTextures(1, &mem->misc.canvas_tex) );
        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->misc.canvas_tex) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );

        mem->misc.w = w0;
        mem->misc.h = h0;
        mem->doc->canvas_size = Vec2(w0, h0);
        mem->doc->canvas_zoom = 1.0f;
        mem->doc->canvas_pos = Vec2((mem->window.width - mem->doc->canvas_size.x) / 2.0f,
                                    (mem->window.height - mem->doc->canvas_size.y) / 2.0f);
    }

    compile_shaders(mem);

    // Init values and load textures
    {
        pagl_init();
        mem->misc.preview_height = mem->misc.preview_width = 512;

        mem->current_tool = PapayaTool_Brush;

        crop_rotate::init(mem);

        mem->brush = init_brush(mem);
        mem->eye_dropper = init_eye_dropper(mem);
        mem->color_panel = init_color_panel(mem);
        mem->graph_panel = init_graph_panel();

        mem->misc.draw_overlay = false;
        mem->misc.show_metrics = false;
        mem->misc.show_undo_buffer = false;
        mem->misc.menu_open = false;
        mem->misc.prefs_open = false;
        mem->misc.show_nodes = true;
        mem->misc.preview_image_size = false;

        f32 ortho_mtx[4][4] =
        {
            { 2.0f,   0.0f,   0.0f,   0.0f },
            { 0.0f,  -2.0f,   0.0f,   0.0f },
            { 0.0f,   0.0f,  -1.0f,   0.0f },
            { -1.0f,  1.0f,   0.0f,   1.0f },
        };
        memcpy(mem->window.proj_mtx, ortho_mtx, sizeof(ortho_mtx));

        mem->colors[PapayaCol_Clear]             = Color(45, 45, 48);
        mem->colors[PapayaCol_Workspace]         = Color(30, 30, 30);
        mem->colors[PapayaCol_Button]            = Color(92, 92, 94);
        mem->colors[PapayaCol_ButtonHover]       = Color(64, 64, 64);
        mem->colors[PapayaCol_ButtonActive]      = Color(0, 122, 204);
        mem->colors[PapayaCol_AlphaGrid1]        = Color(141, 141, 142);
        mem->colors[PapayaCol_AlphaGrid2]        = Color(92, 92, 94);
        mem->colors[PapayaCol_ImageSizePreview1] = Color(55, 55, 55);
        mem->colors[PapayaCol_ImageSizePreview2] = Color(45, 45, 45);
        mem->colors[PapayaCol_Transparent]       = Color(0, 0, 0, 0);

        mem->window.default_imgui_flags = ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoResize
                                        | ImGuiWindowFlags_NoMove
                                        | ImGuiWindowFlags_NoScrollbar
                                        | ImGuiWindowFlags_NoCollapse
                                        | ImGuiWindowFlags_NoScrollWithMouse;

        // Load and bind image
        {
            u8* img;
            i32 ImageWidth, ImageHeight, ComponentsPerPixel;
            img = stbi_load("ui.png", &ImageWidth, &ImageHeight, &ComponentsPerPixel, 0);

            // Create texture
            GLuint Id_GLuint;
            GLCHK( glGenTextures(1, &Id_GLuint) );
            GLCHK( glBindTexture(GL_TEXTURE_2D, Id_GLuint) );
            GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
            GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
            GLCHK( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ImageWidth, ImageHeight, 0,
                                GL_RGBA, GL_UNSIGNED_BYTE, img) );

            // Store our identifier
            free(img);
            mem->textures[PapayaTex_UI] = (u32)Id_GLuint;
        }
    }

    mem->meshes[PapayaMesh_ImageSizePreview] =
        pagl_init_quad_mesh(Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);
    mem->meshes[PapayaMesh_AlphaGrid] =
        pagl_init_quad_mesh(Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);
    mem->meshes[PapayaMesh_Canvas] =
        pagl_init_quad_mesh(Vec2(0,0), Vec2(10,10), GL_DYNAMIC_DRAW);

    // TODO: Free
    mem->meshes[PapayaMesh_ImGui] = (PaglMesh*) calloc(sizeof(PaglMesh), 1);

    // Setup for ImGui
    {
        GLCHK( glGenBuffers(1, &mem->meshes[PapayaMesh_ImGui]->vbo_handle) );
        GLCHK( glGenBuffers(1, &mem->meshes[PapayaMesh_ImGui]->elements_handle) );

        // Create fonts texture
        ImGuiIO& io = ImGui::GetIO();

        u8* pixels;
        i32 width, height;
        //ImFont* my_font0 = io.Fonts->AddFontFromFileTTF("d:\\DroidSans.ttf", 15.0f);
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        GLCHK( glGenTextures(1, &mem->textures[PapayaTex_Font]) );
        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->textures[PapayaTex_Font]) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) );
        GLCHK( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels) );

        // Store our identifier
        io.Fonts->TexID = (void *)(intptr_t)mem->textures[PapayaTex_Font];

        // Cleanup
        io.Fonts->ClearInputData();
        io.Fonts->ClearTexData();
    }

    // ImGui Style Settings
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.f;

        style.Colors[ImGuiCol_WindowBg] = mem->colors[PapayaCol_Transparent];
        style.Colors[ImGuiCol_MenuBarBg] = mem->colors[PapayaCol_Transparent];
        style.Colors[ImGuiCol_HeaderHovered] = mem->colors[PapayaCol_ButtonHover];
        style.Colors[ImGuiCol_Header] = mem->colors[PapayaCol_Transparent];
        style.Colors[ImGuiCol_Button] = mem->colors[PapayaCol_Button];
        style.Colors[ImGuiCol_ButtonActive] = mem->colors[PapayaCol_ButtonActive];
        style.Colors[ImGuiCol_ButtonHovered] = mem->colors[PapayaCol_ButtonHover];
        style.Colors[ImGuiCol_SliderGrabActive] = mem->colors[PapayaCol_ButtonActive];
    }

    // TODO: Temporary
    update_canvas(mem);
}

void core::destroy(PapayaMemory* mem)
{
    for (i32 i = 0; i < PapayaShader_COUNT; i++) {
        pagl_destroy_program(mem->shaders[i]);
    }
    for (i32 i = 0; i < PapayaMesh_COUNT; i++) {
        pagl_destroy_mesh(mem->meshes[i]);
    }

    destroy_color_panel(mem->color_panel);
    destroy_eye_dropper(mem->eye_dropper);
    destroy_graph_panel(mem->graph_panel);

    pagl_destroy();
}

void core::resize(PapayaMemory* mem, i32 width, i32 height)
{
    mem->window.width = width;
    mem->window.height = height;
    ImGui::GetIO().DisplaySize = ImVec2((f32)width, (f32)height);

    // TODO: Intelligent centering. Recenter canvas only if the image was centered
    //       before window was resized.
    // TODO: Improve this code to autocenter canvas on turning the right panels
    //       on and off
    // TODO: Put common layout constants in struct
    // i32 top_margin = 53; 
    // f32 available_width = mem->window.width;
    // if (mem->misc.show_nodes) { available_width -= 400.0f; }
    // f32 available_height = mem->window.height - top_margin;

    // TODO: Adjust canvas zoom after resize
    // mem->doc->canvas_zoom = 0.8f *
    //     math::min(available_width  / (f32)mem->doc->width,
    //               available_height / (f32)mem->doc->height);
    // if (mem->doc->canvas_zoom > 1.0f) { mem->doc->canvas_zoom = 1.0f; }

    // TODO: Adjust canvas position after resize
    // i32 x = (available_width - (f32)mem->doc->width * mem->doc->canvas_zoom)
    //     / 2.0f;
    // i32 y = top_margin + 
    //    (available_height - (f32)mem->doc->height * mem->doc->canvas_zoom)
    //    / 2.0f;
    // mem->doc->canvas_pos = Vec2i(x, y);
}

void core::update(PapayaMemory* mem)
{
    // Initialize frame
    {
        // Current mouse info
        {
            mem->mouse.pos = math::round_to_vec2i(ImGui::GetMousePos());
            Vec2 mouse_pixel_pos = Vec2(floor((mem->mouse.pos.x - mem->doc->canvas_pos.x) / mem->doc->canvas_zoom),
                                        floor((mem->mouse.pos.y - mem->doc->canvas_pos.y) / mem->doc->canvas_zoom));
            mem->mouse.uv = Vec2(mouse_pixel_pos.x / (f32) mem->doc->canvas_size.x,
                                 mouse_pixel_pos.y / (f32) mem->doc->canvas_size.y);

            for (i32 i = 0; i < 3; i++) {
                mem->mouse.is_down[i] = ImGui::IsMouseDown(i);
                mem->mouse.pressed[i] = (mem->mouse.is_down[i] && !mem->mouse.was_down[i]);
                mem->mouse.released[i] = (!mem->mouse.is_down[i] && mem->mouse.was_down[i]);
            }

            // OnCanvas test
            {
                mem->mouse.in_workspace = true;

                if (mem->mouse.pos.x <= 34 ||                      // Document workspace test
                    mem->mouse.pos.x >= mem->window.width - 3 ||   // TODO: Formalize the window layout and
                    mem->mouse.pos.y <= 55 ||                      //       remove magic numbers throughout
                    mem->mouse.pos.y >= mem->window.height - 3) {  //       the code.
                    mem->mouse.in_workspace = false;
                }
                else if (mem->color_panel->is_open &&
                    mem->mouse.pos.x > mem->color_panel->pos.x &&                       // Color picker test
                    mem->mouse.pos.x < mem->color_panel->pos.x + mem->color_panel->size.x &&  //
                    mem->mouse.pos.y > mem->color_panel->pos.y &&                       //
                    mem->mouse.pos.y < mem->color_panel->pos.y + mem->color_panel->size.y) {  //
                    mem->mouse.in_workspace = false;
                }
            }
        }

        // Clear screen buffer
        {
            GLCHK( glViewport(0, 0, (i32)ImGui::GetIO().DisplaySize.x,
                              (i32)ImGui::GetIO().DisplaySize.y) );

            GLCHK( glClearColor(mem->colors[PapayaCol_Clear].r,
                                mem->colors[PapayaCol_Clear].g,
                                mem->colors[PapayaCol_Clear].b, 1.0f) );
            GLCHK( glClear(GL_COLOR_BUFFER_BIT) );

            GLCHK( glEnable(GL_SCISSOR_TEST) );
            GLCHK( glScissor(34, 3,
                             (i32)mem->window.width  - 70,
                             (i32)mem->window.height - 58) ); // TODO: Remove magic numbers

            GLCHK( glClearColor(mem->colors[PapayaCol_Workspace].r,
                mem->colors[PapayaCol_Workspace].g,
                mem->colors[PapayaCol_Workspace].b, 1.0f) );
            GLCHK( glClear(GL_COLOR_BUFFER_BIT) );

            GLCHK( glDisable(GL_SCISSOR_TEST) );
        }

        // Set projection matrix
        mem->window.proj_mtx[0][0] =  2.0f / ImGui::GetIO().DisplaySize.x;
        mem->window.proj_mtx[1][1] = -2.0f / ImGui::GetIO().DisplaySize.y;
    }

    // Title Bar Menu
    {
        ImGui::SetNextWindowSize(
                ImVec2(mem->window.width - mem->window.menu_horizontal_offset -
                       mem->window.title_bar_buttons_width - 3.0f,
                       mem->window.title_bar_height - 10.0f));
        ImGui::SetNextWindowPos(ImVec2(2.0f + mem->window.menu_horizontal_offset,
                                       6.0f));

        mem->misc.menu_open = false;

        ImGuiWindowFlags flags = mem->window.default_imgui_flags
                               | ImGuiWindowFlags_MenuBar;
        ImGui::Begin("Title Bar Menu", 0, flags);
        if (ImGui::BeginMenuBar()) {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, mem->colors[PapayaCol_Clear]);
            if (ImGui::BeginMenu("FILE")) {
                mem->misc.menu_open = true;

                // TODO: Implement
                /*if (mem->doc) {
                    // A document is already open

                    if (ImGui::MenuItem("Close")) { close_doc(mem); }
                    if (ImGui::MenuItem("Save")) {
                        char* Path = platform::save_file_dialog();
                        u8* tex = (u8*)malloc(4 * mem->doc->width * mem->doc->height);
                        if (Path) {
                            // TODO: Do this on a separate thread. Massively blocks UI for large images.
                            GLCHK(glBindTexture(GL_TEXTURE_2D,
                                                mem->doc->final_node->tex_id));
                            GLCHK(glGetTexImage(GL_TEXTURE_2D, 0,
                                                GL_RGBA, GL_UNSIGNED_BYTE,
                                                tex));

                            i32 Result = stbi_write_png(Path, mem->doc->width, mem->doc->height, 4, tex, 4 * mem->doc->width);
                            if (!Result) {
                                // TODO: Log: Save failed
                                platform::print("Save failed\n");
                            }

                            free(tex);
                            free(Path);
                        }
                    }
                } else */{
                // No document open

                    if (ImGui::MenuItem("Open")) {
                        char* Path = platform::open_file_dialog();
                        if (Path)
                        {
                            open_doc(Path, mem);
                            free(Path);
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4")) { mem->is_running = false; }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("EDIT")) {
                mem->misc.menu_open = true;
                if (ImGui::MenuItem("Preferences...", 0)) { mem->misc.prefs_open = true; }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("VIEW")) {
                mem->misc.menu_open = true;
                ImGui::MenuItem("Metrics Window", NULL, &mem->misc.show_metrics);
                ImGui::MenuItem("Undo Buffer Window", NULL, &mem->misc.show_undo_buffer);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    // Side toolbars
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, mem->colors[PapayaCol_Button]);

        // Left toolbar
        // ============
        ImGui::SetNextWindowSize(ImVec2(36, 650));
        ImGui::SetNextWindowPos (ImVec2( 1, 57));
        ImGui::Begin("Left toolbar", 0, mem->window.default_imgui_flags);

#define CALCUV(X, Y) ImVec2((f32)X/256.0f, (f32)Y/256.0f)

        ImGui::PushID(0);
        ImGui::PushStyleColor(ImGuiCol_Button, (mem->current_tool == PapayaTool_Brush) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->current_tool == PapayaTool_Brush) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(0, 0), CALCUV(20, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->current_tool = (mem->current_tool != PapayaTool_Brush) ? PapayaTool_Brush : PapayaTool_None;

        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        ImGui::PushID(1);
        ImGui::PushStyleColor(ImGuiCol_Button, (mem->current_tool == PapayaTool_EyeDropper) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->current_tool == PapayaTool_EyeDropper) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(20, 0), CALCUV(40, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->current_tool = (mem->current_tool != PapayaTool_EyeDropper) ? PapayaTool_EyeDropper : PapayaTool_None;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        ImGui::PushID(2);
        ImGui::PushStyleColor(ImGuiCol_Button       , (mem->current_tool == PapayaTool_CropRotate) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->current_tool == PapayaTool_CropRotate) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(40, 0), CALCUV(60, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->current_tool = (mem->current_tool != PapayaTool_CropRotate) ? PapayaTool_CropRotate : PapayaTool_None;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();

        ImGui::PushID(3);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(33, 33), CALCUV(0, 0), CALCUV(0, 0), 0, mem->color_panel->current_color))
        {
            mem->color_panel->is_open = !mem->color_panel->is_open;
            color_panel_set_color(mem->color_panel->current_color,
                                  mem->color_panel);
        }
        ImGui::PopID();

        ImGui::End();

        // Right toolbar
        // ============
        ImGui::SetNextWindowSize(ImVec2(36, 650));
        ImGui::SetNextWindowPos (ImVec2((f32)mem->window.width - 36, 57));
        ImGui::Begin("Right toolbar", 0, mem->window.default_imgui_flags);

        ImGui::PushID(0);
        ImGui::PushStyleColor(ImGuiCol_Button       , (mem->misc.show_nodes) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_Transparent]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (mem->misc.show_nodes) ? mem->colors[PapayaCol_Button] :  mem->colors[PapayaCol_ButtonHover]);
        if (ImGui::ImageButton((void*)(intptr_t)mem->textures[PapayaTex_UI], ImVec2(20, 20), CALCUV(40, 0), CALCUV(60, 20), 6, ImVec4(0, 0, 0, 0)))
        {
            mem->misc.show_nodes = !mem->misc.show_nodes;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
#undef CALCUV

        ImGui::End();

        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(5);
    }

    if (mem->misc.prefs_open) {
        prefs::show_panel(mem->color_panel, mem->colors, mem->window);
    }

    // Color Picker
    if (mem->color_panel->is_open) {
        update_color_panel(mem->color_panel, mem->colors, mem->mouse,
                           mem->textures[PapayaTex_UI], mem->window);
    }

    // Tool Param Bar
    {
        ImGui::SetNextWindowSize(ImVec2((f32)mem->window.width - 70, 30));
        ImGui::SetNextWindowPos(ImVec2(34, 30));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding , 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding  , ImVec2( 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing    , ImVec2(30, 0));


        bool Show = true;
        ImGui::Begin("Tool param bar", &Show, mem->window.default_imgui_flags);

        if (mem->current_tool == PapayaTool_Brush)
        {
            ImGui::PushItemWidth(85);
            ImGui::InputInt("Diameter", &mem->brush->diameter);
            mem->brush->diameter = math::clamp(mem->brush->diameter, 1, mem->brush->max_diameter);

            ImGui::PopItemWidth();
            ImGui::PushItemWidth(80);
            ImGui::SameLine();

            f32 scaled_hardness = mem->brush->hardness * 100.0f;
            ImGui::SliderFloat("Hardness", &scaled_hardness, 0.0f, 100.0f, "%.0f");
            mem->brush->hardness = scaled_hardness / 100.0f;
            ImGui::SameLine();

            f32 scaled_opacity = mem->brush->opacity * 100.0f;
            ImGui::SliderFloat("Opacity", &scaled_opacity, 0.0f, 100.0f, "%.0f");
            mem->brush->opacity = scaled_opacity / 100.0f;
            ImGui::SameLine();

            ImGui::Checkbox("Anti-alias", &mem->brush->anti_alias); // TODO: Replace this with a toggleable icon button

            ImGui::PopItemWidth();
        }
        else if (mem->current_tool == PapayaTool_CropRotate)
        {
            crop_rotate::toolbar(mem);
        }

        ImGui::End();

        ImGui::PopStyleVar(3);
    }

    if (mem->misc.show_nodes) {
        draw_graph_panel(mem);
    }

    if (!mem->doc) { goto EndOfDoc; }

    // Undo/Redo
    {
        if (ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z))) { // Pop undo op
            // TODO: Clean up this workflow
            bool refresh = false;

            if (ImGui::GetIO().KeyShift &&
                mem->doc->undo.current_index < mem->doc->undo.count - 1 &&
                mem->doc->undo.current->next != 0) {
                // Redo 
                mem->doc->undo.current = mem->doc->undo.current->next;
                mem->doc->undo.current_index++;
                mem->brush->line_segment_start_uv = mem->doc->undo.current->line_segment_start_uv;
                refresh = true;
            } else if (!ImGui::GetIO().KeyShift &&
                mem->doc->undo.current_index > 0 &&
                mem->doc->undo.current->prev != 0) {
                // Undo
                if (mem->doc->undo.current->IsSubRect) {
                    // undo::pop(mem, true);
                } else {
                    refresh = true;
                }

                mem->doc->undo.current = mem->doc->undo.current->prev;
                mem->doc->undo.current_index--;
                mem->brush->line_segment_start_uv = mem->doc->undo.current->line_segment_start_uv;
            }

            if (refresh) {
                // undo::pop(mem, false);
            }
        }

        // Visualization: Undo buffer
        if (mem->misc.show_undo_buffer) {
            undo::visualize_undo_buffer(mem);
        }
    }

    // Canvas zooming and panning
    {
        // Panning
        mem->doc->canvas_pos += math::round_to_vec2i(ImGui::GetMouseDragDelta(2));
        ImGui::ResetMouseDragDelta(2);

        // Zooming
        if (!ImGui::IsMouseDown(2) && ImGui::GetIO().MouseWheel)
        {
            f32 min_zoom = 0.01f, MaxZoom = 32.0f;
            f32 zoom_speed = 0.2f * mem->doc->canvas_zoom;
            f32 scale_delta = math::min(MaxZoom - mem->doc->canvas_zoom, ImGui::GetIO().MouseWheel * zoom_speed);
            Vec2 old_zoom = mem->doc->canvas_size * mem->doc->canvas_zoom;

            mem->doc->canvas_zoom += scale_delta;
            if (mem->doc->canvas_zoom < min_zoom) { mem->doc->canvas_zoom = min_zoom; } // TODO: Dynamically clamp min such that fully zoomed out image is 2x2 pixels?
            Vec2 new_canvas_size = mem->doc->canvas_size * mem->doc->canvas_zoom;

            if ((new_canvas_size.x > mem->window.width || new_canvas_size.y > mem->window.height))
            {
                Vec2 pre_scale_mouse_pos = Vec2(mem->mouse.pos - mem->doc->canvas_pos) / old_zoom;
                Vec2 new_pos = Vec2(mem->doc->canvas_pos) -
                    Vec2(pre_scale_mouse_pos.x * scale_delta * mem->doc->canvas_size.x,
                        pre_scale_mouse_pos.y * scale_delta * (f32)mem->doc->canvas_size.y);
                mem->doc->canvas_pos = math::round_to_vec2i(new_pos);
            }
            else // Center canvas
            {
                // TODO: Maybe disable centering on zoom out. Needs more usability testing.
                i32 top_margin = 53; // TODO: Put common layout constants in struct
                mem->doc->canvas_pos.x =
                    math::round_to_int((mem->window.width -
                                        (f32)mem->doc->canvas_size.x *
                                        mem->doc->canvas_zoom) / 2.0f);
                mem->doc->canvas_pos.y = top_margin +
                    math::round_to_int((mem->window.height - top_margin -
                                        (f32)mem->doc->canvas_size.y *
                                        mem->doc->canvas_zoom) / 2.0f);
            }
        }
    }

    // Draw alpha grid
    {
        glEnable(GL_SCISSOR_TEST);
        // TODO: Conflate PapayaMesh_AlphaGrid and PapayaMesh_Canvas?
        pagl_transform_quad_mesh(mem->meshes[PapayaMesh_AlphaGrid],
                                 mem->doc->canvas_pos,
                                 mem->doc->canvas_size * mem->doc->canvas_zoom);

        mat4x4 m;
        mat4x4_ortho(m, 0.f, (f32)mem->window.width, (f32)mem->window.height, 0.f, -1.f, 1.f);

        if (mem->current_tool == PapayaTool_CropRotate) // Rotate around center
        {
            mat4x4 r;
            Vec2 offset = mem->doc->canvas_pos + (mem->doc->canvas_size * 0.5f);

            mat4x4_translate_in_place(m, offset.x, offset.y, 0.f);
            mat4x4_rotate_Z(r, m, math::to_radians(90.0f * mem->crop_rotate.base_rotation));
            mat4x4_translate_in_place(r, -offset.x, -offset.y, 0.f);
            mat4x4_dup(m, r);
        }

        GLCHK( glEnable(GL_BLEND) );
        GLCHK( glBlendEquation(GL_FUNC_ADD) );
        GLCHK( glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );

        f32 inv_aspect = mem->doc->canvas_size.y / mem->doc->canvas_size.x;
        pagl_draw_mesh(mem->meshes[PapayaMesh_AlphaGrid],
                       mem->shaders[PapayaShader_AlphaGrid],
                       6,
                       Pagl_UniformType_Matrix4, m,
                       Pagl_UniformType_Color, mem->colors[PapayaCol_AlphaGrid1],
                       Pagl_UniformType_Color, mem->colors[PapayaCol_AlphaGrid2],
                       Pagl_UniformType_Float, mem->doc->canvas_zoom,
                       Pagl_UniformType_Float, inv_aspect,
                       Pagl_UniformType_Float, math::max(mem->doc->canvas_size.x,
                                                         mem->doc->canvas_size.y));
    }

    // Draw canvas
    {
        pagl_transform_quad_mesh(mem->meshes[PapayaMesh_Canvas],
                                 mem->doc->canvas_pos,
                                 mem->doc->canvas_size * mem->doc->canvas_zoom);

        mat4x4 m;
        mat4x4_ortho(m, 0.f, (f32)mem->window.width, (f32)mem->window.height, 0.f, -1.f, 1.f);

        if (mem->current_tool == PapayaTool_CropRotate) // Rotate around center
        {
            mat4x4 r;
            Vec2 offset = mem->doc->canvas_pos + (mem->doc->canvas_size * 0.5f);

            mat4x4_translate_in_place(m, offset.x, offset.y, 0.f);
            mat4x4_rotate_Z(r, m, mem->crop_rotate.slider_angle + 
                    math::to_radians(90.0f * mem->crop_rotate.base_rotation));
            mat4x4_translate_in_place(r, -offset.x, -offset.y, 0.f);
            mat4x4_dup(m, r);
        }

        // TODO: Node support 
        // GLCHK( glBindTexture(GL_TEXTURE_2D, mem->doc->final_node->tex_id) );
        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->misc.canvas_tex) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR ) );
        GLCHK( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
        GLCHK( glEnable(GL_BLEND) );
        GLCHK( glBlendEquation(GL_FUNC_ADD) );
        GLCHK( glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );

        pagl_draw_mesh(mem->meshes[PapayaMesh_Canvas],
                       mem->shaders[PapayaShader_ImGui],
                       1,
                       Pagl_UniformType_Matrix4, m);
        glDisable(GL_SCISSOR_TEST);
    }

    // TODO: Switch case <------

    // Brush tool
    if (mem->current_tool == PapayaTool_Brush &&
        !mem->misc.menu_open &&
        (!ImGui::GetIO().KeyAlt || mem->mouse.is_down[1] ||
         mem->mouse.released[1])) {
        update_and_render_brush(mem);
    }

    // Update and draw crop outline
    if (mem->current_tool == PapayaTool_CropRotate)
    {
        crop_rotate::crop_outline(mem);
    }

    // Eye dropper
    if ((mem->current_tool == PapayaTool_EyeDropper ||
         (mem->current_tool == PapayaTool_Brush && ImGui::GetIO().KeyAlt))
        && mem->mouse.in_workspace) {
        update_and_render_eye_dropper(mem);
    }

    // TODO: /Switch case <------

EndOfDoc:

    metrics_window::update(mem);

    ImGui::Render(mem);

    if (mem->color_panel->is_open) {
        render_color_panel(mem);
    }

    // Last mouse info
    {
        mem->mouse.last_pos = mem->mouse.pos;
        mem->mouse.last_uv = mem->mouse.uv;
        mem->mouse.was_down[0] = ImGui::IsMouseDown(0);
        mem->mouse.was_down[1] = ImGui::IsMouseDown(1);
        mem->mouse.was_down[2] = ImGui::IsMouseDown(2);
    }
}

void core::render_imgui(ImDrawData* draw_data, void* mem_ptr)
{
    PapayaMemory* mem = (PapayaMemory*)mem_ptr;

    pagl_push_state();
    // Backup GL state
    GLint last_program, last_texture, last_array_buffer, last_element_array_buffer;
    GLCHK( glGetIntegerv(GL_CURRENT_PROGRAM, &last_program) );
    GLCHK( glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture) );
    GLCHK( glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer) );
    GLCHK( glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer) );

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
    pagl_enable(2, GL_BLEND, GL_SCISSOR_TEST);
    pagl_disable(2, GL_CULL_FACE, GL_DEPTH_TEST);
    
    GLCHK( glBlendEquation(GL_FUNC_ADD) );
    GLCHK( glBlendFunc    (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );
    GLCHK( glActiveTexture(GL_TEXTURE0) );

    // Handle cases of screen coordinates != from framebuffer coordinates (e.g. retina displays)
    ImGuiIO& io     = ImGui::GetIO();
    f32 fb_height = io.DisplaySize.y * io.DisplayFramebufferScale.y;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    GLCHK( glUseProgram      (mem->shaders[PapayaShader_ImGui]->id) );
    GLCHK( glUniform1i       (mem->shaders[PapayaShader_ImGui]->uniforms[1], 0) );
    GLCHK( glUniformMatrix4fv(mem->shaders[PapayaShader_ImGui]->uniforms[0], 1, GL_FALSE, &mem->window.proj_mtx[0][0]) );

    for (i32 n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        GLCHK( glBindBuffer(GL_ARRAY_BUFFER, mem->meshes[PapayaMesh_ImGui]->vbo_handle) );
        GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.size() * sizeof(ImDrawVert), (GLvoid*)&cmd_list->VtxBuffer.front(), GL_STREAM_DRAW) );

        GLCHK( glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mem->meshes[PapayaMesh_ImGui]->elements_handle) );
        GLCHK( glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx), (GLvoid*)&cmd_list->IdxBuffer.front(), GL_STREAM_DRAW) );

        pagl_set_vertex_attribs(mem->shaders[PapayaShader_ImGui]);

        for (const ImDrawCmd* pcmd = cmd_list->CmdBuffer.begin(); pcmd != cmd_list->CmdBuffer.end(); pcmd++)
        {
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                GLCHK( glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId) );
                GLCHK( glScissor((i32)pcmd->ClipRect.x, (i32)(fb_height - pcmd->ClipRect.w), (i32)(pcmd->ClipRect.z - pcmd->ClipRect.x), (i32)(pcmd->ClipRect.w - pcmd->ClipRect.y)) );
                GLCHK( glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, GL_UNSIGNED_SHORT, idx_buffer_offset) );
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
    }

    // Restore modified GL state
    GLCHK( glUseProgram     (last_program) );
    GLCHK( glBindTexture    (GL_TEXTURE_2D, last_texture) );
    GLCHK( glBindBuffer     (GL_ARRAY_BUFFER, last_array_buffer) );
    GLCHK( glBindBuffer     (GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer) );
    pagl_pop_state();
}

void core::update_canvas(PapayaMemory* mem)
{
        int w = mem->misc.w;
        int h = mem->misc.h;
        uint8_t* img = (uint8_t*) malloc(4 * w * h);
        papaya_evaluate_node(&mem->doc->nodes[mem->graph_panel->cur_node],
                             w, h, img);

        GLCHK( glBindTexture(GL_TEXTURE_2D, mem->misc.canvas_tex) );
        GLCHK( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                            GL_RGBA, GL_UNSIGNED_BYTE, img) );
}

static void compile_shaders(PapayaMemory* mem)
{
    // TODO: Move the GLSL strings to their respective cpp files
    const char* vertex_src =
"   #version 120                                                            \n"
"   uniform mat4 proj_mtx; // uniforms[0]                                   \n"
"                                                                           \n"
"   attribute vec2 pos;    // attributes[0]                                 \n"
"   attribute vec2 uv;     // attributes[1]                                 \n"
"   attribute vec4 col;    // attributes[2]                                 \n"
"                                                                           \n"
"   varying vec2 frag_uv;                                                   \n"
"   varying vec4 frag_col;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       frag_uv = uv;                                                       \n"
"       frag_col = col;                                                     \n"
"       gl_Position = proj_mtx * vec4(pos.xy, 0, 1);                        \n"
"   }                                                                       \n";

    mem->misc.vertex_shader = pagl_compile_shader("default vertex", vertex_src,
                                   GL_VERTEX_SHADER);
    // New image preview
    {
        const char* frag_src =
"   #version 120                                                            \n"
"                                                                           \n"
"   uniform vec4  col1;   // Uniforms[1]                                    \n"
"   uniform vec4  col2;   // Uniforms[2]                                    \n"
"   uniform float width;  // Uniforms[3]                                    \n"
"   uniform float height; // Uniforms[4]                                    \n"
"                                                                           \n"
"   varying  vec2 frag_uv;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       float d = mod(frag_uv.x * width + frag_uv.y * height, 150);         \n"
"       gl_FragColor = (d < 75) ? col1 : col2;                              \n"
"   }                                                                       \n";
        const char* name = "new-image preview";
        u32 frag = pagl_compile_shader(name, frag_src, GL_FRAGMENT_SHADER);
        mem->shaders[PapayaShader_ImageSizePreview] =
            pagl_init_program(name, mem->misc.vertex_shader, frag, 2, 5,
                              "pos", "uv",
                              "proj_mtx", "col1", "col2", "width", "height");
    }

    // Alpha grid
    {
        const char* frag_src =
"   #version 120                                                            \n"
"                                                                           \n"
"   uniform vec4  col1;       // Uniforms[1]                                \n"
"   uniform vec4  col2;       // Uniforms[2]                                \n"
"   uniform float zoom;       // Uniforms[3]                                \n"
"   uniform float inv_aspect; // Uniforms[4]                                \n"
"   uniform float max_dim;    // Uniforms[5]                                \n"
"                                                                           \n"
"   varying  vec2 frag_uv;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       vec2 aspect_uv;                                                     \n"
"       if (inv_aspect < 1.0) {                                             \n"
"           aspect_uv = vec2(frag_uv.x, frag_uv.y * inv_aspect);            \n"
"       } else {                                                            \n"
"           aspect_uv = vec2(frag_uv.x / inv_aspect, frag_uv.y);            \n"
"       }                                                                   \n"
"       vec2 uv = floor(aspect_uv.xy * 0.1 * max_dim * zoom);               \n"
"       float a = mod(uv.x + uv.y, 2.0);                                    \n"
"       gl_FragColor = mix(col1, col2, a);                                  \n"
"   }                                                                       \n";
        const char* name = "alpha grid";
        u32 frag = pagl_compile_shader(name, frag_src, GL_FRAGMENT_SHADER);
        mem->shaders[PapayaShader_AlphaGrid] =
            pagl_init_program(name, mem->misc.vertex_shader, frag, 2, 6,
                              "pos", "uv",
                              "proj_mtx", "col1", "col2", "zoom",
                              "inv_aspect", "max_dim");
    }

    // PreMultiply alpha
    {
        const char* frag_src =
"   #version 120                                                            \n"
"                                                                           \n"
"   uniform sampler2D tex; // Uniforms[1]                                   \n"
"                                                                           \n"
"   varying vec2 frag_uv;                                                   \n"
"   varying vec4 frag_col;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       vec4 col = frag_col * texture2D( tex, frag_uv.st);                  \n"
"       gl_FragColor = vec4(col.r, col.g, col.b, 1.0) * col.a;              \n"
"   }                                                                       \n";
        const char* name = "premultiply alpha";
        u32 frag = pagl_compile_shader(name, frag_src, GL_FRAGMENT_SHADER);
        mem->shaders[PapayaShader_PreMultiplyAlpha] =
            pagl_init_program(name, mem->misc.vertex_shader, frag, 3, 2,
                              "pos", "uv", "col",
                              "proj_mtx", "tex");
    }

    // DeMultiply alpha
    {
        const char* frag_src =
"   #version 120                                                            \n"
"                                                                           \n"
"   uniform sampler2D tex; // Uniforms[1]                                   \n"
"                                                                           \n"
"   varying vec2 frag_uv;                                                   \n"
"   varying vec4 frag_col;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       vec4 col = frag_col * texture2D( tex, frag_uv.st);                  \n"
"       gl_FragColor = vec4(col.rgb/col.a, col.a);                          \n"
"   }                                                                       \n";

        const char* name = "demultiply alpha";
        u32 frag = pagl_compile_shader(name, frag_src, GL_FRAGMENT_SHADER);
        mem->shaders[PapayaShader_DeMultiplyAlpha] =
            pagl_init_program(name, mem->misc.vertex_shader, frag, 3, 2,
                              "pos", "uv", "col",
                              "proj_mtx", "tex");
    }

    // default fragment
    {
        const char* frag_src =
"   #version 120                                                            \n"
"                                                                           \n"
"   uniform sampler2D tex; // Uniforms[1]                                   \n"
"                                                                           \n"
"   varying vec2 frag_uv;                                                   \n"
"   varying vec4 frag_col;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       gl_FragColor = frag_col * texture2D( tex, frag_uv.st);              \n"
"   }                                                                       \n";

        const char* name = "default fragment";
        u32 frag = pagl_compile_shader(name, frag_src, GL_FRAGMENT_SHADER);
        mem->shaders[PapayaShader_ImGui] =
            pagl_init_program(name, mem->misc.vertex_shader, frag, 3, 2,
                              "pos", "uv", "col",
                              "proj_mtx", "tex");
    }

    // Unlit shader
    {
        const char* frag_src =
"   #version 120                                                            \n"
"                                                                           \n"
"   uniform sampler2D tex; // Uniforms[1]                                   \n"
"                                                                           \n"
"   varying vec2 frag_uv;                                                   \n"
"   varying vec4 frag_col;                                                  \n"
"                                                                           \n"
"   void main()                                                             \n"
"   {                                                                       \n"
"       gl_FragColor = frag_col;                                            \n"
"   }                                                                       \n";

        const char* name = "unlit";
        u32 frag = pagl_compile_shader(name, frag_src, GL_FRAGMENT_SHADER);
        mem->shaders[PapayaShader_VertexColor] =
            pagl_init_program(name, mem->misc.vertex_shader, frag, 3, 2,
                              "pos", "uv", "col",
                              "proj_mtx", "tex");
    }
}