#pragma once

#include "constants.h"

constexpr COLORREF display_colors[9] = {
        RGB(150, 150, 150),     // 0 COLOREMPTY         Empty diskspace
        RGB(200, 200, 200),     // 1 COLORALLOCATED     Used diskspace / system files
        RGB(0, 150, 0),         // 2 COLORUNFRAGMENTED  Unfragmented files
        RGB(128, 0, 0),         // 3 COLORUNMOVABLE     Unmovable files
        RGB(200, 100, 60),      // 4 COLORFRAGMENTED    Fragmented files
        RGB(0, 0, 255),         // 5 COLORBUSY          Busy color
        RGB(255, 0, 255),       // 6 COLORMFT           MFT reserved zones
        RGB(0, 150, 150),       // 7 COLORSPACEHOG      Spacehogs
        RGB(255, 255, 255)      // 8 background
};

struct ClusterSquareStruct {
    bool dirty_;
    byte color_;
};

class DefragGui {
public:
    // Constructor and destructor
    DefragGui();

    ~DefragGui() = default;

    // Get instance of the class
    static DefragGui *get_instance();

    void clear_screen(const wchar_t *format, ...);

    void draw_cluster(const DefragDataStruct *data, uint64_t cluster_start, uint64_t cluster_end, int color);

    void fill_squares(int clusterStartSquareNum, int clusterEndSquareNum);

    void show_debug(DebugLevel level, const ItemStruct *item, const wchar_t *format, ...);

    void show_status(const DefragDataStruct *data);

    void show_analyze(const DefragDataStruct *data, const ItemStruct *item);

    void show_move(const ItemStruct *item, uint64_t clusters, uint64_t from_lcn, uint64_t to_lcn, uint64_t from_vcn);

    void show_diskmap(DefragDataStruct *data);

    int initialize(HINSTANCE instance, int cmd_show, DefragLog *log, DebugLevel debug_level);

    void set_display_data(HDC dc);

    WPARAM do_modal();

    void on_paint(HDC dc) const;

    void paint_image(HDC dc);

    static LRESULT CALLBACK process_messagefn(HWND WindowHandle, UINT message, WPARAM w_param, LPARAM l_param);

private:

    HWND wnd_{};
    WNDCLASSEX wnd_class_{};
    MSG message_{};
    // UINT_PTR size_timer_;

    static constexpr size_t MESSAGES_BUF_SIZE = 32768;
    // The texts displayed on the screen
    wchar_t messages_[6][MESSAGES_BUF_SIZE]{};

    DebugLevel debug_level_;

    uint64_t progress_start_time_;       /* The time at percentage zero. */
    uint64_t progress_time_;            /* When ProgressDone/ProgressTodo were last updated. */
    uint64_t progress_todo_{};            /* Number of clusters to do. */
    uint64_t progress_done_;            /* Number of clusters already done. */

    std::unique_ptr<DefragStruct> defrag_struct_;

    /* graphics data */

    int top_height_{};
    int square_size_;

    // Offsets of drawing area of disk
    int offset_x_;
    int offset_y_;

    // Calculated offset of drawing area of disk
    int real_offset_x_{};
    int real_offset_y_{};

    // Size of drawing area of disk
    Rect disk_area_size_;

    // Number of squares in horizontal direction of disk area
    int num_disk_squares_x_{};

    // Number of squares in horizontal direction of disk area
    int num_disk_squares_y_{};

    // Total number of squares in disk area
    int num_disk_squares_;

    // Color of each square in disk area and status if it is "dirty"
    std::vector<ClusterSquareStruct> cluster_squares_;

    // Color of each disk cluster
    std::unique_ptr<uint8_t> cluster_info_;

    // Number of disk clusters
    uint64_t num_clusters_;

    /* 0:no, 1:request, 2: busy. */
    //	int RedrawScreen;

    // Current window size
    Rect client_window_size_;

    // Mutex to make the display single-threaded.
    HANDLE display_mutex_{};

    // Handle to graphics device context
    HDC dc_{};

    // Bitmap used for double buffering
    std::unique_ptr<Bitmap> bmp_;

    // pointer to logger
    DefragLog *log_{};

    // pointer to library
    DefragLib *defrag_lib_;

    // static member that is an instance of itself
    static DefragGui *instance_;
};
