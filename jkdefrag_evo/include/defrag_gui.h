#pragma once

#include "constants.h"
#include "defrag_log.h"
#include "defrag_state.h"
#include "file_node.h"

class DefragGui {
public:
    // Constructor and destructor
    DefragGui();

    ~DefragGui() = default;

    // Get instance of the class
    static DefragGui *get_instance();

    void clear_screen(std::wstring &&text);

    void draw_cluster(const DefragState &data, uint64_t cluster_start, uint64_t cluster_end,
                      DrawColor color);

    void show_debug(DebugLevel level, const FileNode *item, std::wstring &&text);

    void show_always(std::wstring &&text) {
        show_debug(DebugLevel::AlwaysLog, nullptr, std::move(text));
    }

    void log_detailed_progress(std::wstring &&text) {
        show_debug(DebugLevel::DetailedProgress, nullptr, std::move(text));
    }

    void show_status(const DefragState &data);

    void show_analyze(const DefragState &data, const FileNode *item);

    void show_analyze_no_state(const FileNode *item);

    void show_analyze_update_item_text(const FileNode *item);

    void show_move(const FileNode *item, cluster_count64_t clusters, lcn64_t from_lcn,
                   lcn64_t to_lcn, vcn64_t from_vcn);

    void show_diskmap(DefragState &ex);

    int initialize(HINSTANCE instance, int cmd_show, DebugLevel debug_level);

    void set_display_data(HDC dc);

    WPARAM windows_event_loop();

    void on_paint(HDC dc, const PAINTSTRUCT &ps) const;

    void full_redraw_window(HDC dc);

    static LRESULT CALLBACK process_messagefn(HWND WindowHandle, UINT message, WPARAM w_param,
                                              LPARAM l_param);

    DiskMap &get_color_map() { return color_map_; }

    /// For non-interactive mode, write a log message. For interactive mode pop up a messagebox and exit if exit code is provided
    void message_box_error(const wchar_t *text, const wchar_t *caption,
                           std::optional<int> exit_code);

    static void exit_now(int exit_code = 1);

protected:
    void write_stats(const DefragState &data);

    void paint_top_background(Graphics *graphics) const;

    void paint_background(Graphics *graphics, const POINT &diskmap_org,
                          const POINT &diskmap_end) const;

    void paint_diskmap_outline(Graphics *graphics, const POINT &m_org, const POINT &m_end) const;

    void paint_strings(Graphics *graphics) const;

    void paint_diskmap(Graphics *graphics);

    void paint_empty_cell(Graphics *graphics, const POINT &cell_pos, COLORREF col, const Pen &pen,
                          const Pen &pen_empty) const;

    void paint_cell(Graphics *graphics, const POINT &cell_pos, const COLORREF col,
                    const Pen &pen) const;

    static void paint_set_gradient_colors(COLORREF col, Color &c1, Color &c2);

    // Marks top panel area for redraw
    void request_delayed_redraw_top_area();

    void request_delayed_redraw();

    // Re-render strings in the top area into bitmap; Do not update the window yet. Combine this with
    // invalidate_top_area() to update the window.
    void repaint_top_area() const;

private:
    HWND wnd_{};
    WNDCLASSEX wnd_class_{};
    MSG message_{};
    bool redraw_top_area_{};
    bool redraw_disk_map_{};

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

    //
    // graphics data
    //

    // Height of the top status area
    int top_area_height_{};
    int square_size_;
    // Used for invalidating; Updated when drawing/redrawing
    RECT top_area_rect_{};

    // Offsets of the drawing area
    POINT drawing_area_offset_;

    // Calculated offset of the drawing area
    POINT diskmap_pos_;

    // Size of the drawing area
    Rect disk_area_size_;

    DiskMap color_map_;

    // 0:no, 1:request, 2: busy
    //	int RedrawScreen;

    // Current window size
    Rect client_size_;

    // Mutex to make the display single-threaded.
    std::mutex display_mutex_{};

    // Handle to graphics device context; TODO: Do not store it here, store in local variable
    HDC dc_{};

    // Bitmap used for double buffering
    std::unique_ptr<Bitmap> bmp_;

    // Non-owning pointer to library
    DefragRunner *defrag_lib_;

    // static member that is an instance of itself
    static DefragGui *instance_;
};
