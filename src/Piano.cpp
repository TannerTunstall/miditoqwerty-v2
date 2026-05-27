//
// Created by Chris Young on 22/4/20.
//
#include "imgui.h"

#include "Piano.h"
#include "util.h"

static bool has_black(int key) {
    return (!((key - 1) % 7 == 0 || (key - 1) % 7 == 3) && key != 51);
}

void Piano::up(int key) {
    key_states[key] = 0;
}

void Piano::down(int key, int velocity) {
    key_states[key] = velocity;

}

void Piano::draw(bool *show, bool windowsEditable, ImU32 noteColor) {

    //ImU32 Red = IM_COL32(255,0,0,255); // piano pressed color

    ImU32 Red = noteColor;

    ImU32 Black = IM_COL32(0, 0, 0, 255);
    ImU32 White = IM_COL32(255, 255, 255, 255);

    ImGui::Begin("Keyboard", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   (!windowsEditable ? ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize : ImGuiWindowFlags_None));

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    // Fill the available window width across all 52 white keys. Floors so we
    // don't draw past the right edge; minimum keeps the piano usable at very
    // small window sizes.
    const float avail = ImGui::GetContentRegionAvail().x;
    int width = (int)(avail / 52.0f);
    if (width < 4) width = 4;
    int cur_key = 21;
    for (int key = 0; key < 52; key++) {
        ImU32 col = White;
        if (key_states[cur_key]) {
            col = Red;
        }
        draw_list->AddRectFilled(
                ImVec2(p.x + key * width, p.y),
                ImVec2(p.x + key * width + width, p.y + 60),
                col, 0, ImDrawCornerFlags_All);
        draw_list->AddRect(
                ImVec2(p.x + key * width, p.y),
                ImVec2(p.x + key * width + width, p.y + 60),
                Black, 0, ImDrawCornerFlags_All);
        cur_key++;
        if (has_black(key)) {
            cur_key++;
        }
    }
    cur_key = 22;
    for (int key = 0; key < 52; key++) {
        if (has_black(key)) {
            ImU32 col = Black;
            if (key_states[cur_key]) {
                col = Red;
            }
            draw_list->AddRectFilled(
                    ImVec2(p.x + key * width + width * 3 / 4, p.y),
                    ImVec2(p.x + key * width + width * 5 / 4 + 1, p.y + 40),
                    col, 0, ImDrawCornerFlags_All);
            draw_list->AddRect(
                    ImVec2(p.x + key * width + width * 3 / 4, p.y),
                    ImVec2(p.x + key * width + width * 5 / 4 + 1, p.y + 40),
                    Black, 0, ImDrawCornerFlags_All);

            cur_key += 2;
        } else {
            cur_key++;
        }
    }
    ImGui::End();
}

std::vector<int> Piano::current_notes() {
    std::vector<int> result{};
    for (int i = 0; i < 256; i++) {
        if (key_states[i]) {
            result.push_back(i);
        }
    }
    return result;
}
