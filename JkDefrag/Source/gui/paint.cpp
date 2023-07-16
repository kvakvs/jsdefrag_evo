#include "precompiled_header.h"
#include "defrag_gui.h"

void DefragGui::paint_background(Rect &window_size, Graphics *graphics,
                                 const POINT &diskmap_org, const POINT &diskmap_end) const {
    Color back_color1;
    back_color1.SetFromCOLORREF(RGB(0, 0, 255));

    Color back_color2;
    back_color2.SetFromCOLORREF(RGB(255, 0, 0));

    LinearGradientBrush bg_brush(window_size, Color::DarkBlue, Color::LightBlue, LinearGradientModeForwardDiagonal);

    Rect draw_area = window_size;
    draw_area.Height = top_area_height_ + 1;

    Color busy_color;
    busy_color.SetFromCOLORREF(display_colors[(size_t) DrawColor::Busy]);

    // SolidBrush busy_brush(busy_color);
    // graphics->FillRectangle(&busyBrush, drawArea);
    graphics->FillRectangle(&bg_brush, draw_area);

    // Paint bottom background, below the status panel
    draw_area = Rect(0, top_area_height_ + 1, client_size_.Width, diskmap_org.y - top_area_height_ - 2);
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

    graphics->DrawString(messages_[0].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(40.0f, 0.0f);
    graphics->DrawString(messages_[1].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(200.0f, 0.0f);
    graphics->DrawString(messages_[2].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(280.0f, 0.0f);
    graphics->DrawString(messages_[3].c_str(), -1, &font, point_f, &brush);

    point_f = PointF(2.0f, 17.0f);
    graphics->DrawString(messages_[4].c_str(), -1, &font, point_f, &brush);

    if (debug_level_ > DebugLevel::Warning) {
        point_f = PointF(2.0f, 33.0f);
        graphics->DrawString(messages_[5].c_str(), -1, &font, point_f, &brush);
    }
}

void DefragGui::repaint_window(HDC dc) {
    std::unique_ptr<Graphics> graphics(Graphics::FromImage(bmp_.get()));
    Rect window_size = client_size_;

    [[maybe_unused]] const auto square_size_unit = 1.f / (float) square_size_;

    // Reset the display idle timer (screen saver) and system idle timer (power saver)
    SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    if (progress_todo_ > 0) {
        auto done = (double) ((double) progress_done_ / (double) progress_todo_);

        if (done > 1.0) done = 1.0;

        messages_[2] = std::format(FLT2_FMT L"%", 100.0 * done);
    }

    // 1 pixel outside disk cluster map
    POINT diskmap_org = {
            .x = diskmap_pos_.x - 1,
            .y = diskmap_pos_.y + top_area_height_ - 1
    };

    // 1 pixel on the other side of disk cluster map
    POINT diskmap_end = {
            .x = diskmap_org.x + (int) color_map_.get_width() * square_size_ + 2,
            .y = diskmap_org.y + (int) color_map_.get_height() * square_size_ + 2
    };

    paint_background(window_size, graphics.get(), diskmap_org, diskmap_end);
    paint_strings(graphics.get());
    paint_diskmap_outline(graphics.get(), diskmap_org, diskmap_end);
    paint_diskmap(graphics.get());
}

// Fill a sequence of squares with their current state bitflags
void DefragGui::prepare_cells_for_cluster_range(uint64_t cluster_start_square_num, uint64_t cluster_end_square_num) {
    const auto cluster_per_square = (float) (num_clusters_ / color_map_.get_total_count());
    auto colors_map = cluster_info_.get();

    for (uint64_t ii = cluster_start_square_num; ii < cluster_end_square_num; ii++) {
        [[maybe_unused]] auto current_color = DrawColor::Empty;
        ClusterSquareStruct::ColorBits cluster_group_colors{};

        for (uint64_t kk = ii * cluster_per_square;
             kk < num_clusters_ && kk < (ii + 1) * cluster_per_square;
             kk++) {
            switch (colors_map[kk]) {
                case DrawColor::Empty:
                    cluster_group_colors.empty = true;
                    break;
                case DrawColor::Allocated:
                    cluster_group_colors.allocated = true;
                    break;
                case DrawColor::Unfragmented:
                    cluster_group_colors.unfragmented = true;
                    break;
                case DrawColor::Unmovable:
                    cluster_group_colors.unmovable = true;
                    break;
                case DrawColor::Fragmented:
                    cluster_group_colors.fragmented = true;
                    break;
                case DrawColor::Busy:
                    cluster_group_colors.busy = true;
                    break;
                case DrawColor::Mft:
                    cluster_group_colors.mft = true;
                    break;
                case DrawColor::SpaceHog:
                    cluster_group_colors.spacehog = true;
                    break;
            }
        }

        auto &cell = color_map_.get_cell(ii);
        cell.dirty_ = true;
        cell.color_ = cluster_group_colors;
    }
}

void DefragGui::paint_set_gradient_colors(COLORREF col, Color &c1, Color &c2) {
    c1.SetFromCOLORREF(col);
    c2.SetFromCOLORREF(col);

//    int rr = GetRValue(col) + 200;
//    rr = rr > 255 ? 255 : rr;
//
//    int gg = GetGValue(col) + 200;
//    gg = gg > 255 ? 255 : gg;
//
//    int bb = GetBValue(col) + 100;
//    bb = bb > 255 ? 255 : bb;
//
//    c2.SetFromCOLORREF(RGB((byte) rr, (byte) gg, (byte) bb));
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

        if (!cell.dirty_) {
            continue;
        }

        cell.dirty_ = false;

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

        auto &stored_color = cell.color_;
        bool is_empty = true;
        COLORREF col = display_colors[(size_t) DrawColor::Empty];

        if (stored_color.busy) {
            col = display_colors[(size_t) DrawColor::Busy];
            is_empty = false;
        } else if (stored_color.unmovable) {
            col = display_colors[(size_t) DrawColor::Unmovable];
            is_empty = false;
        } else if (stored_color.fragmented) {
            col = display_colors[(size_t) DrawColor::Fragmented];
            is_empty = false;
        } else if (stored_color.mft) {
            col = display_colors[(size_t) DrawColor::Mft];
            is_empty = false;
        } else if (stored_color.unfragmented) {
            col = display_colors[(size_t) DrawColor::Unfragmented];
            is_empty = false;
        } else if (stored_color.spacehog) {
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