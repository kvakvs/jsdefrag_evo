#pragma once

#include "constants.h"
#include "defrag_state.h"
#include "defrag_log.h"
#include "itemstruct.h"
#include "defrag_struct.h"

class DefragGui {
public:
    // Constructor and destructor
    DefragGui();

    ~DefragGui() = default;

    // Get instance of the class
    static DefragGui *get_instance();

    void clear_screen(std::wstring &&text);

    void draw_cluster(const DefragState &data, uint64_t cluster_start, uint64_t cluster_end, DrawColor color);

    void prepare_cells_for_cluster_range(uint64_t cluster_start_square_num, uint64_t cluster_end_square_num);

    void show_debug(DebugLevel level, const ItemStruct *item, std::wstring &&text);

    void log_fatal(std::wstring &&text) {
        show_debug(DebugLevel::AlwaysLog, nullptr, std::move(text));
    }

    void log_detailed_progress(std::wstring &&text) {
        show_debug(DebugLevel::DetailedProgress, nullptr, std::move(text));
    }

    void show_status(const DefragState &data);

    void show_analyze(const DefragState &data, const ItemStruct *item);

    void show_analyze_no_state(const ItemStruct *item);

    void show_analyze_update_item_text(const ItemStruct *item);

    void show_move(const ItemStruct *item, uint64_t clusters, uint64_t from_lcn, uint64_t to_lcn, uint64_t from_vcn);

    void show_diskmap(DefragState &data);

    int initialize(HINSTANCE instance, const int cmd_show, const DebugLevel debug_level);

    void set_display_data(HDC dc);

    WPARAM windows_event_loop();

    void on_paint(HDC dc, const PAINTSTRUCT &ps) const;

    void full_redraw_window(HDC dc);

    static LRESULT CALLBACK process_messagefn(HWND WindowHandle, UINT message, WPARAM w_param, LPARAM l_param);

protected:
    void write_stats(const DefragState &data);

    void
    paint_background(Rect &window_size, Graphics *graphics, const POINT &diskmap_org, const POINT &diskmap_end) const;

    void paint_diskmap_outline(Graphics *graphics, const POINT &m_org, const POINT &m_end) const;

    void paint_strings(Graphics *graphics) const;

    void paint_diskmap(Graphics *graphics);

    void paint_empty_cell(Graphics *graphics, const POINT &cell_pos, const COLORREF col, const Pen &pen,
                          const Pen &pen_empty) const;

    void paint_cell(Graphics *graphics, const POINT &cell_pos, const COLORREF col, const Pen &pen) const;

    static void paint_set_gradient_colors(COLORREF col, Color &c1, Color &c2);

    // Marks top panel area for redraw
    void invalidate_top_area() const;

    // Re-render strings in the top area into bitmap; Do not update the window yet. Combine this with
    // invalidate_top_area() to update the window.
    void repaint_top_area() const;

private:
    HWND wnd_{};
    WNDCLASSEX wnd_class_{};
    MSG message_{};

    // The texts displayed on the screen
    // Messages[5] is the debug row, only displayed if LogLevel is > Warning
    std::wstring messages_[6] = {};

    // The time at percentage zero
    Clock::time_point progress_start_time_{};
    // When ProgressDone/ProgressTodo were last updated
    Clock::time_point progress_time_{};

    // Number of clusters to do
    uint64_t progress_todo_{};
    // Number of clusters already done
    uint64_t progress_done_{};

    // Owning pointer
    std::unique_ptr<DefragStruct> defrag_struct_;

    //
    // graphics data
    //

    // Height of the top status area
    int top_area_height_{};
    int square_size_;
    // Used for invalidating; Updated when drawing/redrawing
    RECT top_area_rect_{};

    // Offsets of drawing area of disk
    POINT drawing_area_offset_;

    // Calculated offset of drawing area of disk
    POINT diskmap_pos_;

    // Size of drawing area of disk
    Rect disk_area_size_;

    DiskColorMap color_map_;

    // Color of each disk cluster
    std::unique_ptr<DrawColor[]> cluster_info_;

    // Number of disk clusters
    uint64_t num_clusters_;

    // 0:no, 1:request, 2: busy
    //	int RedrawScreen;

    // Current window size
    Rect client_size_;

    // Mutex to make the display single-threaded.
    std::mutex display_mutex_{};

    // Handle to graphics device context
    HDC dc_{};

    // Bitmap used for double buffering
    std::unique_ptr<Bitmap> bmp_;

    // Non-owning pointer to library
    DefragLib *defrag_lib_;

    // static member that is an instance of itself
    static DefragGui *instance_;
};
