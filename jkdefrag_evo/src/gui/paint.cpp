/*
 JkDefrag  --  Defragment and optimize all harddisks.

 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 For the full text of the license see the "License gpl.txt" file.

 Jeroen C. Kessels, Internet Engineer
 http://www.kessels.com/
 */

#include "precompiled_header.h"
#include "defrag_gui.h"

void DefragGui::paint_top_background(Graphics *graphics) const {
    // Fill top area
    SolidBrush bg_brush(Color::Gray);
    Rect top_area(client_size_.X, client_size_.Y, client_size_.Width, top_area_height_ + 1);
    graphics->FillRectangle(&bg_brush, top_area);
}

void DefragGui::paint_background(Graphics *graphics, const POINT &diskmap_org, const POINT &diskmap_end) const {
    // LinearGradientBrush bg_brush(window_size, Color::DarkBlue, Color::LightBlue, LinearGradientModeForwardDiagonal);
    SolidBrush bg_brush(Color::Gray);

    // Color busy_color;
    // busy_color.SetFromCOLORREF(display_colors[(size_t) DrawColor::Busy]);

    // Paint bottom background, below the status panel

    // Narrow horizontal rectangle above the disk map
    Rect draw_area = Rect(0, top_area_height_ + 1, client_size_.Width, diskmap_org.y - top_area_height_ - 2);
    graphics->FillRectangle(&bg_brush, draw_area);

    draw_area = Rect(0, diskmap_end.y + 2, client_size_.Width, client_size_.Height - diskmap_end.y - 2);
    graphics->FillRectangle(&bg_brush, draw_area);

    draw_area = Rect(0, diskmap_org.y - 1, diskmap_org.x - 1, diskmap_end.y - diskmap_org.y + 3);
    graphics->FillRectangle(&bg_brush, draw_area);

    draw_area = Rect(diskmap_end.x, diskmap_org.y - 1, client_size_.Width - diskmap_end.x,
                     diskmap_end.y - diskmap_org.y + 3);
    graphics->FillRectangle(&bg_brush, draw_area);
}

void DefragGui::paint_diskmap_outline(Graphics *graphics, const POINT &m_org, const POINT &m_end) const {
    Pen pen1(Color(0, 0, 0));
    Pen pen2(Color(255, 255, 255));

    graphics->DrawLine(&pen1, (int) m_org.x, (int) m_end.y, (int) m_org.x, (int) m_org.y);
    graphics->DrawLine(&pen1, (int) m_org.x, (int) m_org.y, (int) m_end.x, (int) m_org.y);
    graphics->DrawLine(&pen1, (int) m_end.x, (int) m_org.y, (int) m_end.x, (int) m_end.y);
    graphics->DrawLine(&pen1, (int) m_end.x, (int) m_end.y, (int) m_org.x, (int) m_end.y);

    graphics->DrawLine(&pen2, (int) m_org.x - 1, (int) m_end.y + 1, (int) m_org.x - 1, (int) m_org.y - 1);
    graphics->DrawLine(&pen2, (int) m_org.x - 1, (int) m_org.y - 1, (int) m_end.x + 1, (int) m_org.y - 1);
    graphics->DrawLine(&pen2, (int) m_end.x + 1, (int) m_org.y - 1, (int) m_end.x + 1, (int) m_end.y + 1);
    graphics->DrawLine(&pen2, (int) m_end.x + 1, (int) m_end.y + 1, (int) m_org.x - 1, (int) m_end.y + 1);
}

void DefragGui::paint_strings(Graphics *graphics) const {
    SolidBrush brush(Color::White);
    FontFamily font_family(L"Tahoma");
    Font font(&font_family, 12, FontStyleRegular, UnitPixel);
    PointF point_f(2.0f, 0.0f);

    // [Messages[0]] [Messages[1].............] [Messages[2]...] [Messages[3]]
    // [Messages[4]..........................................................]
    // [Messages[5] debug only...............................................]
    graphics->DrawString(messages_[0].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(40.0f, 0.0f);
    graphics->DrawString(messages_[1].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(200.0f, 0.0f);
    graphics->DrawString(messages_[2].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(280.0f, 0.0f);
    graphics->DrawString(messages_[3].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(2.0f, 17.0f);
    graphics->DrawString(messages_[4].c_str(), -1, &font, point_f, &brush);

    if (DefragLog::debug_level_ >= DebugLevel::Debug) {
        point_f = PointF(2.0f, 33.0f);
        graphics->DrawString(messages_[5].c_str(), -1, &font, point_f, &brush);
    }
}

// Make sure this function does not run more often than 100ms
static bool full_redraw_throttle_timer() {
    static std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto duration = current_time - last_time;

    if (duration < std::chrono::milliseconds(100)) {
        return false;
    }

    last_time = current_time;
    return true;
}

void DefragGui::full_redraw_window(HDC dc) {
    if (!full_redraw_throttle_timer()) return;

    std::unique_ptr<Graphics> graphics(Graphics::FromImage(bmp_.get()));

    [[maybe_unused]] const auto square_size_unit = 1.f / (float) square_size_;

    // Reset the display idle timer (screen saver) and system idle timer (power saver)
    SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    if (progress_todo_ > 0) {
        auto done = (double) ((double) progress_done_ / (double) progress_todo_);

        if (done > 1.0) done = 1.0;

        // Display percentage progress
        messages_[2] = std::format(FLT2_FMT L"%", 100.0 * done);
    }

    // 1 pixel outside disk cluster map
    POINT diskmap_org = {
            .x = diskmap_pos_.x - 1,
            .y = diskmap_pos_.y + top_area_height_ - 1
    };
    top_area_rect_ = {
            .left=0,
            .top=0,
            .right=client_size_.Width,
            .bottom = top_area_height_
    };

    // 1 pixel on the other side of disk cluster map
    POINT diskmap_end = {
            .x = diskmap_org.x + (int) color_map_.get_width() * square_size_ + 2,
            .y = diskmap_org.y + (int) color_map_.get_height() * square_size_ + 2
    };

    if (redraw_top_area_ || redraw_disk_map_) {
        paint_background(graphics.get(), diskmap_org, diskmap_end);
    }
    if (redraw_top_area_) {
        paint_top_background(graphics.get());
        paint_strings(graphics.get());

        redraw_top_area_ = false;
    }
    if (redraw_disk_map_) {
        paint_diskmap_outline(graphics.get(), diskmap_org, diskmap_end);
        paint_diskmap(graphics.get());

        redraw_disk_map_ = false;
    }
}

void DefragGui::repaint_top_area() const {
    std::unique_ptr<Graphics> graphics(Graphics::FromImage(bmp_.get()));

    paint_top_background(graphics.get());
    paint_strings(graphics.get());
}

void DefragGui::paint_set_gradient_colors(COLORREF col, Color &c1, Color &c2) {
    c1.SetFromCOLORREF(col);
    c2.SetFromCOLORREF(col);
}

void DefragGui::paint_cell(Graphics *graphics, const POINT &cell_pos, const COLORREF col, const Pen &pen) const {
    Color c1;
    Color c2;
    paint_set_gradient_colors(col, c1, c2);

    Rect draw_area2(cell_pos.x, cell_pos.y, square_size_ - 0, square_size_ - 0);

    // LinearGradientBrush bb1(draw_area2, c2, c1, LinearGradientModeForwardDiagonal);
    SolidBrush bb1(c1);

    graphics->FillRectangle(&bb1, draw_area2);

    int line_x1 = draw_area2.X;
    int line_y1 = draw_area2.Y + square_size_ - 1;
    int line_x2 = draw_area2.X + square_size_ - 1;
    int line_y2 = draw_area2.Y;
    int line_x3 = draw_area2.X + square_size_ - 1;
    int line_y3 = draw_area2.Y + square_size_ - 1;

    graphics->DrawLine(&pen, line_x1, line_y1, line_x3, line_y3);
    graphics->DrawLine(&pen, line_x2, line_y2, line_x3, line_y3);
}

void DefragGui::paint_empty_cell(Graphics *graphics, const POINT &cell_pos, const COLORREF col, const Pen &pen,
                                 const Pen &pen_empty) const {
    Color c1;
    Color c2;
    paint_set_gradient_colors(col, c1, c2);

    Rect draw_area2(cell_pos.x, cell_pos.y, square_size_ - 0, square_size_ - 0);

    // LinearGradientBrush bb2(draw_area2, c1, c2, LinearGradientModeVertical);
    SolidBrush bb2(c1);
    graphics->FillRectangle(&bb2, draw_area2);

    int line_x1 = draw_area2.X;
    int line_y1 = draw_area2.Y;
    int line_x2 = draw_area2.X + square_size_ - 1;
    int line_y2 = draw_area2.Y;
    int line_x3 = draw_area2.X;
    int line_y3 = draw_area2.Y + square_size_ - 1;
    int line_x4 = draw_area2.X + square_size_ - 1;
    int line_y4 = draw_area2.Y + square_size_ - 1;

    graphics->DrawLine(&pen_empty, line_x1, line_y1, line_x2, line_y2);
    graphics->DrawLine(&pen, line_x3, line_y3, line_x4, line_y4);
}

void DefragGui::paint_diskmap(Graphics *graphics) {
    COLORREF color_empty_ref = display_colors[(size_t) DrawColor::Empty];
    Color color_empty;
    color_empty.SetFromCOLORREF(color_empty_ref);

    Pen pen(Color(210, 210, 210));
    Pen pen_empty(color_empty);
    const size_t map_width = color_map_.get_width();

    for (size_t cell_index = 0; cell_index < color_map_.get_total_count(); cell_index++) {
        auto &cell = color_map_.get_cell(cell_index);

        if (!cell.dirty) {
            continue;
        }

        cell.dirty = false;

        // Integer x:y index in the disk map array (not screen position!)
        POINT map_xy = {
                .x = (LONG) (cell_index % map_width),
                .y = (LONG) (cell_index / map_width)
        };
        // Screen position where to display the cell
        POINT cell_pos = {
                .x = diskmap_pos_.x + map_xy.x * square_size_,
                .y = diskmap_pos_.y + map_xy.y * square_size_ + top_area_height_
        };

        bool is_empty = true;
        COLORREF col = display_colors[(size_t) DrawColor::Empty];

        if (cell.busy) {
            col = display_colors[(size_t) DrawColor::Busy];
            is_empty = false;
        } else if (cell.unmovable) {
            col = display_colors[(size_t) DrawColor::Unmovable];
            is_empty = false;
        } else if (cell.fragmented) {
            col = display_colors[(size_t) DrawColor::Fragmented];
            is_empty = false;
        } else if (cell.mft) {
            col = display_colors[(size_t) DrawColor::Mft];
            is_empty = false;
        } else if (cell.unfragmented) {
            col = display_colors[(size_t) DrawColor::Unfragmented];
            is_empty = false;
        } else if (cell.spacehog) {
            col = display_colors[(size_t) DrawColor::SpaceHog];
            is_empty = false;
        }

        if (is_empty) {
            paint_empty_cell(graphics, cell_pos, col, pen, pen_empty);
        } else {
            paint_cell(graphics, cell_pos, col, pen);
        }
    }
}

// Actual re-rendering happens on WM_TIMER event
void DefragGui::request_delayed_redraw() {
    redraw_top_area_ = true;
    redraw_disk_map_ = true;
    InvalidateRect(wnd_, nullptr, FALSE);
}

// Actual re-rendering happens on WM_TIMER event
void DefragGui::request_delayed_redraw_top_area() {
    redraw_top_area_ = true;
    InvalidateRect(wnd_, &top_area_rect_, FALSE);
}

void DefragGui::message_box_error(const wchar_t *text, const wchar_t *caption, std::optional<int> exit_code) {
    // TODO: if interactive, post a messagebox, otherwise log a message
    show_debug(DebugLevel::AlwaysLog, nullptr, text);
    ::MessageBoxW(wnd_, text, caption, MB_OK);
    if (exit_code.has_value()) {
        exit_now(exit_code.value());
    }
}

void DefragGui::exit_now(int exit_code) {
    ::PostQuitMessage(exit_code);
    ::exit(exit_code);
}
