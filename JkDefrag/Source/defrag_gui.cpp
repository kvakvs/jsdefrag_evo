#include "std_afx.h"
#include "defrag_data_struct.h"

DefragGui::DefragGui(): debug_level_() {
    defrag_lib_ = DefragLib::getInstance();

    bmp_ = nullptr;

    defrag_struct_ = new DefragStruct();

    square_size_ = 12;
    //	m_clusterSquares = nullptr;
    num_disk_squares_ = 0;

    offset_x_ = 26;
    offset_y_ = 16;

    cluster_info_ = nullptr;
    num_clusters_ = 1;

    progress_start_time_ = 0;
    progress_time_ = 0;
    progress_done_ = 0;

    int i = 0;

    for (i = 0; i < 6; i++) *messages_[i] = '\0';

    //	RedrawScreen = 0;
}

DefragGui::~DefragGui() = default;

DefragGui* DefragGui::get_instance() {
    if (gui_ == nullptr) {
        gui_.reset(new DefragGui());
    }

    return gui_.get();
}

int DefragGui::initialize(const HINSTANCE instance, const int cmd_show, DefragLog* log, const DebugLevel debug_level) {
    ULONG_PTR gdiplus_token;

    const GdiplusStartupInput gdiplus_startup_input;

    GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, nullptr);

    log_ = log;
    debug_level_ = debug_level;

    static const auto mutex_name = "JKDefrag";
    static const auto window_class_name = "MyClass";

    display_mutex_ = CreateMutex(nullptr,FALSE, mutex_name);

    wnd_class_.cbClsExtra = 0;
    wnd_class_.cbWndExtra = 0;
    wnd_class_.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wnd_class_.hCursor = LoadCursor(nullptr,IDC_ARROW);
    wnd_class_.hIcon = LoadIcon(nullptr,MAKEINTRESOURCE(1));
    wnd_class_.hInstance = instance;
    wnd_class_.lpfnWndProc = (WNDPROC)DefragGui::process_messagefn;
    wnd_class_.lpszClassName = window_class_name;
    wnd_class_.lpszMenuName = nullptr;
    wnd_class_.style = CS_HREDRAW | CS_VREDRAW;
    wnd_class_.cbSize = sizeof(WNDCLASSEX);
    wnd_class_.hIconSm = LoadIcon(instance,MAKEINTRESOURCE(1));

    CHAR version_str[100];

    LoadString(instance, 2, version_str, 99);

    if (RegisterClassEx(&wnd_class_) == 0) {
        MessageBoxW(nullptr, L"Cannot register class", defrag_struct_->versiontext_,MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    wnd_ = CreateWindowW(L"MyClass", defrag_struct_->versiontext_, WS_TILEDWINDOW,
                         CW_USEDEFAULT, 0, 1024, 768, nullptr, nullptr, instance, nullptr);

    if (wnd_ == nullptr) {
        MessageBoxW(nullptr, L"Cannot create window", defrag_struct_->versiontext_,MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    /* Show the window in the state that Windows has specified, minimized or maximized. */
    ShowWindow(wnd_, cmd_show);
    UpdateWindow(wnd_);

    SetTimer(wnd_, 1, 300, nullptr);

    return 1;
}

WPARAM DefragGui::do_modal() {
    /* The main message thread. */
    while (true) {
        const int get_message_result = GetMessage(&message_, nullptr, 0, 0);

        if (get_message_result == 0) break;
        if (get_message_result == -1) break;
        if (message_.message == WM_QUIT) break;

        TranslateMessage(&message_);
        DispatchMessage(&message_);
    }

    return message_.wParam;
}

void DefragGui::set_display_data(const HDC dc) {
    const Graphics graphics(dc);
    Rect client_window_size;

    Status status = graphics.GetVisibleClipBounds(&client_window_size);

    client_window_size_ = client_window_size;

    /*
        if (m_clusterSquares != nullptr)
        {
            delete[] m_clusterSquares;
        }
    */

    top_height_ = 33;

    if (debug_level_ > DebugLevel::Warning) {
        top_height_ = 49;
    }

    disk_area_size_.Width = client_window_size.Width - offset_x_ * 2;
    disk_area_size_.Height = client_window_size.Height - top_height_ - offset_y_ * 2;

    num_disk_squares_x_ = (int)(disk_area_size_.Width / square_size_);
    num_disk_squares_y_ = (int)(disk_area_size_.Height / square_size_);

    num_disk_squares_ = num_disk_squares_x_ * num_disk_squares_y_;

    //	m_clusterSquares = new clusterSquareStruct[m_numDiskSquares];

    for (int ii = 0; ii < num_disk_squares_; ii++) {
        cluster_squares_[ii].color_ = 0;
        cluster_squares_[ii].dirty_ = true;
    }

    real_offset_x_ = (int)((client_window_size_.Width - num_disk_squares_x_ * square_size_) * 0.5);
    real_offset_y_ = (int)((client_window_size_.Height - top_height_ - num_disk_squares_y_ * square_size_) * 0.5);

    bmp_.reset(new Bitmap(client_window_size_.Width, client_window_size_.Height));

    //	Color bottomPartColor;
    //	bottomPartColor.SetFromCOLORREF(RGB(255,255,255));

    //	SolidBrush bottomPartBrush(bottomPartColor);

    //	Rect drawArea(0, 0, m_clientWindowSize.Width, m_clientWindowSize.Height);
    //	graphics.FillRectangle(&bottomPartBrush, drawArea);

    /* Ask defragger to completely redraw the screen. */
    //	RedrawScreen = 0;
}

/* Callback: clear the screen. */
void DefragGui::clear_screen(WCHAR* format, ...) {
    va_list var_args;

    /* If there is no message then return. */
    if (format == nullptr) return;

    /* Clear all the messages. */
    for (int i = 0; i < 6; i++) *messages_[i] = '\0';

    /* Save the message in Messages 0. */
    va_start(var_args, format);
    vswprintf_s(messages_[0], 50000, format, var_args);

    /* If there is no logfile then return. */
    if (log_ != nullptr) {
        log_->log_message(format, var_args);
    }

    va_end(var_args);

    paint_image(dc_);
    //	InvalidateRect(m_hWnd,nullptr,FALSE);
}

/* Callback: whenever an item (file, directory) is moved on disk. */
void DefragGui::show_move(const ItemStruct* item, const uint64_t clusters, const uint64_t from_lcn,
                          const uint64_t to_lcn, const uint64_t from_vcn) {
    /* Save the message in Messages 3. */
    if (clusters == 1) {
        swprintf_s(messages_[3], 50000, L"Moving 1 cluster from %I64d to %I64d.", from_lcn, to_lcn);
    }
    else {
        swprintf_s(messages_[3], 50000, L"Moving %I64d clusters from %I64d to %I64d.",
                   clusters, from_lcn, to_lcn);
    }

    /* Save the name of the file in Messages 4. */
    if (item != nullptr && item->long_path_ != nullptr) {
        swprintf_s(messages_[4], 50000, L"%s", item->long_path_);
    }
    else {
        *messages_[4] = '\0';
    }

    /* If debug mode then write a message to the logfile. */
    if (debug_level_ < DebugLevel::DetailedProgress) return;

    if (from_vcn > 0) {
        if (clusters == 1) {
            log_->log_message(L"%s\n  Moving 1 cluster from %I64d to %I64d, VCN=%I64d.",
                              item->long_path_, from_lcn, to_lcn, from_vcn);
        }
        else {
            log_->log_message(L"%s\n  Moving %I64d clusters from %I64d to %I64d, VCN=%I64d.",
                              item->long_path_, clusters, from_lcn, to_lcn, from_vcn);
        }
    }
    else {
        if (clusters == 1) {
            log_->log_message(L"%s\n  Moving 1 cluster from %I64d to %I64d.",
                              item->long_path_, from_lcn, to_lcn);
        }
        else {
            log_->log_message(L"%s\n  Moving %I64d clusters from %I64d to %I64d.",
                              item->long_path_, clusters, from_lcn, to_lcn);
        }
    }
    paint_image(dc_);
}


/* Callback: for every file during analysis.
This subroutine is called one last time with Item=nullptr when analysis has finished. */
void DefragGui::show_analyze(const DefragDataStruct* data, const ItemStruct* item) {
    if (data != nullptr && data->count_all_files_ != 0) {
        swprintf_s(messages_[3], 50000, L"Files %I64d, Directories %I64d, Clusters %I64d",
                   data->count_all_files_, data->count_directories_, data->count_all_clusters_);
    }
    else {
        swprintf_s(messages_[3], 50000, L"Applying Exclude and SpaceHogs masks....");
    }

    /* Save the name of the file in Messages 4. */
    if (item != nullptr && item->long_path_ != nullptr) {
        swprintf_s(messages_[4], 50000, L"%s", item->long_path_);
    }
    else {
        *messages_[4] = '\0';
    }
    paint_image(dc_);
}

/* Callback: show a debug message. */
void DefragGui::show_debug(const DebugLevel level, const ItemStruct* item, WCHAR* format, ...) {
    va_list var_args;

    if (debug_level_ < level) return;

    /* Save the name of the file in Messages 4. */
    if (item != nullptr && item->long_path_ != nullptr) {
        swprintf_s(messages_[4], 50000, L"%s", item->long_path_);
    }

    /* If there is no message then return. */
    if (format == nullptr) return;

    /* Save the debug message in Messages 5. */
    va_start(var_args, format);
    vswprintf_s(messages_[5], 50000, format, var_args);
    log_->log_message(format, var_args);
    va_end(var_args);
    paint_image(dc_);
}

/* Callback: paint a cluster on the screen in a color. */
void DefragGui::draw_cluster(DefragDataStruct* data, uint64_t cluster_start, const uint64_t cluster_end,
                             const int color) {
    __timeb64 now{};
    [[maybe_unused]] Rect window_size = client_window_size_;

    /* Save the PhaseTodo and PhaseDone counters for later use by the progress counter. */
    if (data->phase_todo_ != 0) {
        _ftime64_s(&now);

        progress_time_ = now.time * 1000 + now.millitm;
        progress_done_ = data->phase_done_;
        progress_todo_ = data->phase_todo_;
    }

    /* Sanity check. */
    if (data->total_clusters_ == 0) return;
    if (dc_ == nullptr) return;
    if (cluster_start == cluster_end) return;
    //	if (ClusterStart > Data->TotalClusters) ClusterStart = 0;

    WaitForSingleObject(display_mutex_, 100);

    display_mutex_ = CreateMutex(nullptr,FALSE, "JKDefrag");

    if (num_clusters_ != data->total_clusters_ || cluster_info_ == nullptr) {
        if (cluster_info_ != nullptr) {
            free(cluster_info_);

            cluster_info_ = nullptr;
        }

        num_clusters_ = data->total_clusters_;

        cluster_info_ = (byte*)malloc((size_t)num_clusters_);

        for (int ii = 0; ii <= num_clusters_; ii++) {
            cluster_info_[ii] = DefragStruct::COLOREMPTY;
        }

        return;
    }

    for (uint64_t ii = cluster_start; ii <= cluster_end; ii++) {
        cluster_info_[ii] = color;
    }

    const auto cluster_per_square = (float)(num_clusters_ / num_disk_squares_);
    const int cluster_start_square_num = (int)((uint64_t)cluster_start / (uint64_t)cluster_per_square);
    const int cluster_end_square_num = (int)((uint64_t)cluster_end / (uint64_t)cluster_per_square);

    fill_squares(cluster_start_square_num, cluster_end_square_num);

    ReleaseMutex(display_mutex_);
    paint_image(dc_);
}

/* Callback: just before the defragger starts a new Phase, and when it finishes. */
void DefragGui::show_status(const DefragDataStruct* data) {
    __timeb64 now{};

    int i;

    /* Reset the progress counter. */
    _ftime64_s(&now);

    progress_start_time_ = now.time * 1000 + now.millitm;
    progress_time_ = progress_start_time_;
    progress_done_ = 0;
    progress_todo_ = 0;

    /* Reset all the messages. */
    for (i = 0; i < 6; i++) *messages_[i] = '\0';

    /* Update Message 0 and 1. */
    if (data != nullptr) {
        swprintf_s(messages_[0], 50000, L"%s", data->disk_.mount_point_);

        switch (data->phase_) {
        case 1: wcscpy_s(messages_[1], 50000, L"Phase 1: Analyze");
            break;
        case 2: wcscpy_s(messages_[1], 50000, L"Phase 2: Defragment");
            break;
        case 3: wcscpy_s(messages_[1], 50000, L"Phase 3: ForcedFill");
            break;
        case 4: swprintf_s(messages_[1], 50000, L"Zone %u: Sort", data->zone_ + 1);
            break;
        case 5: swprintf_s(messages_[1], 50000, L"Zone %u: Fast Optimize", data->zone_ + 1);
            break;
        case 6: wcscpy_s(messages_[1], 50000, L"Phase 3: Move Up");
            break;
        case 7:
            wcscpy_s(messages_[1], 50000, L"Finished.");
            swprintf_s(messages_[4], 50000, L"Logfile: %s", log_->get_log_filename());
            break;
        case 8: wcscpy_s(messages_[1], 50000, L"Phase 3: Fixup");
            break;
        }

        log_->log_message(messages_[1]);
    }

    /* Write some statistics to the logfile. */
    if (data != nullptr && data->phase_ == 7) {
        ItemStruct* largest_items[25];
        uint64_t total_clusters;
        uint64_t total_bytes;
        uint64_t total_fragments;
        int fragments;
        ItemStruct* item;
        log_->log_message(L"- Total disk space: %I64d bytes (%.04f gigabytes), %I64d clusters",
                          data->bytes_per_cluster_ * data->total_clusters_,
                          (double)(data->bytes_per_cluster_ * data->total_clusters_) / (1024 * 1024 * 1024),
                          data->total_clusters_);

        log_->log_message(L"- Bytes per cluster: %I64d bytes", data->bytes_per_cluster_);

        log_->log_message(L"- Number of files: %I64d", data->count_all_files_);
        log_->log_message(L"- Number of directories: %I64d", data->count_directories_);
        log_->log_message(L"- Total size of analyzed items: %I64d bytes (%.04f gigabytes), %I64d clusters",
                          data->count_all_clusters_ * data->bytes_per_cluster_,
                          (double)(data->count_all_clusters_ * data->bytes_per_cluster_) / (1024 * 1024 * 1024),
                          data->count_all_clusters_);

        if (data->count_all_files_ + data->count_directories_ > 0) {
            log_->log_message(L"- Number of fragmented items: %I64d (%.04f%% of all items)",
                              data->count_fragmented_items_,
                              (double)(data->count_fragmented_items_ * 100) / (data->count_all_files_ + data->
                                  count_directories_));
        }
        else {
            log_->log_message(L"- Number of fragmented items: %I64d", data->count_fragmented_items_);
        }

        if (data->count_all_clusters_ > 0 && data->total_clusters_ > 0) {
            log_->log_message(
                L"- Total size of fragmented items: %I64d bytes, %I64d clusters, %.04f%% of all items, %.04f%% of disk",
                data->count_fragmented_clusters_ * data->bytes_per_cluster_,
                data->count_fragmented_clusters_,
                (double)(data->count_fragmented_clusters_ * 100) / data->count_all_clusters_,
                (double)(data->count_fragmented_clusters_ * 100) / data->total_clusters_);
        }
        else {
            log_->log_message(L"- Total size of fragmented items: %I64d bytes, %I64d clusters",
                              data->count_fragmented_clusters_ * data->bytes_per_cluster_,
                              data->count_fragmented_clusters_);
        }

        if (data->total_clusters_ > 0) {
            log_->log_message(L"- Free disk space: %I64d bytes, %I64d clusters, %.04f%% of disk",
                              data->count_free_clusters_ * data->bytes_per_cluster_,
                              data->count_free_clusters_,
                              (double)(data->count_free_clusters_ * 100) / data->total_clusters_);
        }
        else {
            log_->log_message(L"- Free disk space: %I64d bytes, %I64d clusters",
                              data->count_free_clusters_ * data->bytes_per_cluster_,
                              data->count_free_clusters_);
        }

        log_->log_message(L"- Number of gaps: %I64d", data->count_gaps_);

        if (data->count_gaps_ > 0) {
            log_->log_message(L"- Number of small gaps: %I64d (%.04f%% of all gaps)",
                              data->count_gaps_less16_,
                              (double)(data->count_gaps_less16_ * 100) / data->count_gaps_);
        }
        else {
            log_->log_message(L"- Number of small gaps: %I64d",
                              data->count_gaps_less16_);
        }

        if (data->count_free_clusters_ > 0) {
            log_->log_message(L"- Size of small gaps: %I64d bytes, %I64d clusters, %.04f%% of free disk space",
                              data->count_clusters_less16_ * data->bytes_per_cluster_,
                              data->count_clusters_less16_,
                              (double)(data->count_clusters_less16_ * 100) / data->count_free_clusters_);
        }
        else {
            log_->log_message(L"- Size of small gaps: %I64d bytes, %I64d clusters",
                              data->count_clusters_less16_ * data->bytes_per_cluster_,
                              data->count_clusters_less16_);
        }

        if (data->count_gaps_ > 0) {
            log_->log_message(L"- Number of big gaps: %I64d (%.04f%% of all gaps)",
                              data->count_gaps_ - data->count_gaps_less16_,
                              (double)((data->count_gaps_ - data->count_gaps_less16_) * 100) / data->count_gaps_);
        }
        else {
            log_->log_message(L"- Number of big gaps: %I64d",
                              data->count_gaps_ - data->count_gaps_less16_);
        }

        if (data->count_free_clusters_ > 0) {
            log_->log_message(L"- Size of big gaps: %I64d bytes, %I64d clusters, %.04f%% of free disk space",
                              (data->count_free_clusters_ - data->count_clusters_less16_) * data->bytes_per_cluster_,
                              data->count_free_clusters_ - data->count_clusters_less16_,
                              (double)((data->count_free_clusters_ - data->count_clusters_less16_) * 100) / data->
                              count_free_clusters_);
        }
        else {
            log_->log_message(L"- Size of big gaps: %I64d bytes, %I64d clusters",
                              (data->count_free_clusters_ - data->count_clusters_less16_) * data->bytes_per_cluster_,
                              data->count_free_clusters_ - data->count_clusters_less16_);
        }

        if (data->count_gaps_ > 0) {
            log_->log_message(L"- Average gap size: %.04f clusters",
                              (double)data->count_free_clusters_ / data->count_gaps_);
        }

        if (data->count_free_clusters_ > 0) {
            log_->log_message(L"- Biggest gap: %I64d bytes, %I64d clusters, %.04f%% of free disk space",
                              data->biggest_gap_ * data->bytes_per_cluster_,
                              data->biggest_gap_,
                              (double)(data->biggest_gap_ * 100) / data->count_free_clusters_);
        }
        else {
            log_->log_message(L"- Biggest gap: %I64d bytes, %I64d clusters",
                              data->biggest_gap_ * data->bytes_per_cluster_,
                              data->biggest_gap_);
        }

        if (data->total_clusters_ > 0) {
            log_->log_message(L"- Average end-begin distance: %.0f clusters, %.4f%% of volume size",
                              data->average_distance_, 100.0 * data->average_distance_ / data->total_clusters_);
        }
        else {
            log_->log_message(L"- Average end-begin distance: %.0f clusters", data->average_distance_);
        }

        for (item = defrag_lib_->tree_smallest(data->item_tree_); item != nullptr; item = defrag_lib_->tree_next(item)) {
            if (item->is_unmovable_ != true) continue;
            if (item->is_excluded_ == true) continue;
            if (item->is_dir_ == true && data->cannot_move_dirs_ > 20) continue;
            break;
        }

        if (item != nullptr) {
            log_->log_message(L"These items could not be moved:");
            log_->log_message(L"  Fragments       Bytes  Clusters Name");

            total_fragments = 0;
            total_bytes = 0;
            total_clusters = 0;

            for (item = defrag_lib_->tree_smallest(data->item_tree_); item != nullptr; item = defrag_lib_->
                 tree_next(item)) {
                if (item->is_unmovable_ != true) continue;
                if (item->is_excluded_ == true) continue;
                if (item->is_dir_ == true && data->cannot_move_dirs_ > 20) continue;
                if (item->long_filename_ != nullptr &&
                    (_wcsicmp(item->long_filename_, L"$BadClus") == 0 ||
                        _wcsicmp(item->long_filename_, L"$BadClus:$Bad:$DATA") == 0))
                    continue;

                fragments = defrag_lib_->fragment_count(item);

                if (item->long_path_ == nullptr) {
                    log_->log_message(L"  %9lu %11I64u %9I64u [at cluster %I64u]", fragments, item->bytes_,
                                      item->clusters_count_,
                                      defrag_lib_->get_item_lcn(item));
                }
                else {
                    log_->log_message(L"  %9lu %11I64u %9I64u %s", fragments, item->bytes_, item->clusters_count_,
                                      item->long_path_);
                }

                total_fragments = total_fragments + fragments;
                total_bytes = total_bytes + item->bytes_;
                total_clusters = total_clusters + item->clusters_count_;
            }

            log_->log_message(L"  --------- ----------- --------- -----");
            log_->log_message(L"  %9I64u %11I64u %9I64u Total", total_fragments, total_bytes, total_clusters);
        }

        for (item = defrag_lib_->tree_smallest(data->item_tree_); item != nullptr; item = defrag_lib_->tree_next(item)) {
            if (item->is_excluded_ == true) continue;
            if (item->is_dir_ == true && data->cannot_move_dirs_ > 20) continue;

            fragments = defrag_lib_->fragment_count(item);

            if (fragments <= 1) continue;

            break;
        }

        if (item != nullptr) {
            log_->log_message(L"These items are still fragmented:");
            log_->log_message(L"  Fragments       Bytes  Clusters Name");

            total_fragments = 0;
            total_bytes = 0;
            total_clusters = 0;

            for (item = defrag_lib_->tree_smallest(data->item_tree_); item != nullptr; item = defrag_lib_->
                 tree_next(item)) {
                if (item->is_excluded_ == true) continue;
                if (item->is_dir_ == true && data->cannot_move_dirs_ > 20) continue;

                fragments = defrag_lib_->fragment_count(item);

                if (fragments <= 1) continue;

                if (item->long_path_ == nullptr) {
                    log_->log_message(L"  %9lu %11I64u %9I64u [at cluster %I64u]", fragments, item->bytes_,
                                      item->clusters_count_,
                                      defrag_lib_->get_item_lcn(item));
                }
                else {
                    log_->log_message(L"  %9lu %11I64u %9I64u %s", fragments, item->bytes_, item->clusters_count_,
                                      item->long_path_);
                }

                total_fragments = total_fragments + fragments;
                total_bytes = total_bytes + item->bytes_;
                total_clusters = total_clusters + item->clusters_count_;
            }

            log_->log_message(L"  --------- ----------- --------- -----");
            log_->log_message(L"  %9I64u %11I64u %9I64u Total", total_fragments, total_bytes, total_clusters);
        }

        int last_largest = 0;

        for (item = defrag_lib_->tree_smallest(data->item_tree_); item != nullptr; item = defrag_lib_->tree_next(item)) {
            if (item->long_filename_ != nullptr &&
                (_wcsicmp(item->long_filename_, L"$BadClus") == 0 ||
                    _wcsicmp(item->long_filename_, L"$BadClus:$Bad:$DATA") == 0)) {
                continue;
            }

            for (i = last_largest - 1; i >= 0; i--) {
                if (item->clusters_count_ < largest_items[i]->clusters_count_) break;

                if (item->clusters_count_ == largest_items[i]->clusters_count_ &&
                    item->bytes_ < largest_items[i]->bytes_)
                    break;

                if (item->clusters_count_ == largest_items[i]->clusters_count_ &&
                    item->bytes_ == largest_items[i]->bytes_ &&
                    item->long_path_ != nullptr &&
                    largest_items[i]->long_path_ != nullptr &&
                    _wcsicmp(item->long_path_, largest_items[i]->long_path_) > 0)
                    break;
            }

            if (i < 24) {
                if (last_largest < 25) last_largest++;

                for (int j = last_largest - 1; j > i + 1; j--) {
                    largest_items[j] = largest_items[j - 1];
                }

                largest_items[i + 1] = item;
            }
        }

        if (last_largest > 0) {
            log_->log_message(L"The 25 largest items on disk:");
            log_->log_message(L"  Fragments       Bytes  Clusters Name");

            for (i = 0; i < last_largest; i++) {
                if (largest_items[i]->long_path_ == nullptr) {
                    log_->log_message(L"  %9u %11I64u %9I64u [at cluster %I64u]",
                                      defrag_lib_->fragment_count(largest_items[i]),
                                      largest_items[i]->bytes_, largest_items[i]->clusters_count_,
                                      defrag_lib_->get_item_lcn(largest_items[i]));
                }
                else {
                    log_->log_message(L"  %9u %11I64u %9I64u %s", defrag_lib_->fragment_count(largest_items[i]),
                                      largest_items[i]->bytes_, largest_items[i]->clusters_count_, largest_items[i]->long_path_);
                }
            }
        }
    }
}


void DefragGui::paint_image(HDC dc) {
    Graphics* graphics = Graphics::FromImage(bmp_.get());
    Rect window_size = client_window_size_;
    Rect draw_area;

    [[maybe_unused]] const float square_size_unit = (float)(1 / (float)square_size_);

    /* Reset the display idle timer (screen saver) and system idle timer (power saver). */
    SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    if (progress_todo_ > 0) {
        double done;
        done = (double)((double)progress_done_ / (double)progress_todo_);

        if (done > 1) done = 1;

        swprintf_s(messages_[2], 50000, L"%.4f%%", 100 * done);
    }

    Color back_color1;
    back_color1.SetFromCOLORREF(RGB(0, 0, 255));

    Color back_color2;
    back_color2.SetFromCOLORREF(RGB(255, 0, 0));

    LinearGradientBrush bg_brush(window_size, Color::DarkBlue, Color::LightBlue, LinearGradientModeForwardDiagonal);

    draw_area = window_size;

    draw_area.Height = top_height_ + 1;

    Color busy_color;
    busy_color.SetFromCOLORREF(Colors[DefragStruct::COLORBUSY]);

    SolidBrush busy_brush(busy_color);

    /*
        graphics->FillRectangle(&busyBrush, drawArea);
    */
    graphics->FillRectangle(&bg_brush, draw_area);

    SolidBrush brush(Color::White);

    FontFamily font_family(L"Tahoma");
    Font font(&font_family, 12, FontStyleRegular, UnitPixel);
    WCHAR* text;
    PointF point_f(2.0f, 0.0f);

    text = messages_[0];
    graphics->DrawString(text, -1, &font, point_f, &brush);

    point_f = PointF(40.0f, 0.0f);
    text = messages_[1];
    graphics->DrawString(text, -1, &font, point_f, &brush);

    point_f = PointF(200.0f, 0.0f);
    text = messages_[2];
    graphics->DrawString(text, -1, &font, point_f, &brush);

    point_f = PointF(280.0f, 0.0f);
    text = messages_[3];
    graphics->DrawString(text, -1, &font, point_f, &brush);

    point_f = PointF(2.0f, 17.0f);
    text = messages_[4];
    graphics->DrawString(text, -1, &font, point_f, &brush);

    if (debug_level_ > DebugLevel::Warning) {
        point_f = PointF(2.0f, 33.0f);
        text = messages_[5];
        graphics->DrawString(text, -1, &font, point_f, &brush);
    }


    int xx1 = real_offset_x_ - 1;
    int yy1 = real_offset_y_ + top_height_ - 1;

    int xx2 = xx1 + num_disk_squares_x_ * square_size_ + 1;
    int yy2 = yy1 + num_disk_squares_y_ * square_size_ + 1;

    /*
        Color bottomPartColor;
        bottomPartColor.SetFromCOLORREF(Colors[JKDefragStruct::COLORBUSY]);
    
        SolidBrush bottomPartBrush(bottomPartColor);
    */

    draw_area = Rect(0, top_height_ + 1, client_window_size_.Width, yy1 - top_height_ - 2);
    /*
        graphics->FillRectangle(&bottomPartBrush, drawArea);
    */
    graphics->FillRectangle(&bg_brush, draw_area);

    draw_area = Rect(0, yy2 + 2, client_window_size_.Width, client_window_size_.Height - yy2 - 2);
    /*
        graphics->FillRectangle(&bottomPartBrush, drawArea);
    */
    graphics->FillRectangle(&bg_brush, draw_area);

    draw_area = Rect(0, yy1 - 1, xx1 - 1, yy2 - yy1 + 3);
    /*
        graphics->FillRectangle(&bottomPartBrush, drawArea);
    */
    graphics->FillRectangle(&bg_brush, draw_area);

    draw_area = Rect(xx2, yy1 - 1, client_window_size_.Width - xx2, yy2 - yy1 + 3);
    /*
        graphics->FillRectangle(&bottomPartBrush, drawArea);
    */
    graphics->FillRectangle(&bg_brush, draw_area);

    Pen pen1(Color(0, 0, 0));
    Pen pen2(Color(255, 255, 255));

    graphics->DrawLine(&pen1, xx1, yy2, xx1, yy1);
    graphics->DrawLine(&pen1, xx1, yy1, xx2, yy1);
    graphics->DrawLine(&pen1, xx2, yy1, xx2, yy2);
    graphics->DrawLine(&pen1, xx2, yy2, xx1, yy2);

    graphics->DrawLine(&pen2, xx1 - 1, yy2 + 1, xx1 - 1, yy1 - 1);
    graphics->DrawLine(&pen2, xx1 - 1, yy1 - 1, xx2 + 1, yy1 - 1);
    graphics->DrawLine(&pen2, xx2 + 1, yy1 - 1, xx2 + 1, yy2 + 1);
    graphics->DrawLine(&pen2, xx2 + 1, yy2 + 1, xx1 - 1, yy2 + 1);

    COLORREF color_empty_ref = Colors[DefragStruct::COLOREMPTY];
    Color color_empty;
    color_empty.SetFromCOLORREF(color_empty_ref);

    Pen pen(Color(210, 210, 210));
    Pen pen_empty(color_empty);

    for (int jj = 0; jj < num_disk_squares_; jj++) {
        if (cluster_squares_[jj].dirty_ == false) {
            continue;
        }

        cluster_squares_[jj].dirty_ = false;

        int x1 = jj % num_disk_squares_x_;
        int y1 = jj / num_disk_squares_x_;

        int xx3 = real_offset_x_ + x1 * square_size_;
        int yy3 = real_offset_y_ + y1 * square_size_ + top_height_;

        [[maybe_unused]] byte cluster_empty = (cluster_squares_[jj].color_ & 1 << 7) >> 7;
        [[maybe_unused]] byte cluster_allocated = (cluster_squares_[jj].color_ & 1 << 6) >> 6;
        byte cluster_unfragmented = (cluster_squares_[jj].color_ & 1 << 5) >> 5;
        byte cluster_unmovable = (cluster_squares_[jj].color_ & 1 << 4) >> 4;
        byte cluster_fragmented = (cluster_squares_[jj].color_ & 1 << 3) >> 3;
        byte cluster_busy = (cluster_squares_[jj].color_ & 1 << 2) >> 2;
        byte cluster_mft = (cluster_squares_[jj].color_ & 1 << 1) >> 1;
        byte cluster_spacehog = cluster_squares_[jj].color_ & 1;

        COLORREF col = Colors[DefragStruct::COLOREMPTY];

        int empty_cluster = true;

        if (cluster_busy == 1) {
            col = Colors[DefragStruct::COLORBUSY];
            empty_cluster = false;
        }
        else if (cluster_unmovable == 1) {
            col = Colors[DefragStruct::COLORUNMOVABLE];
            empty_cluster = false;
        }
        else if (cluster_fragmented == 1) {
            col = Colors[DefragStruct::COLORFRAGMENTED];
            empty_cluster = false;
        }
        else if (cluster_mft == 1) {
            col = Colors[DefragStruct::COLORMFT];
            empty_cluster = false;
        }
        else if (cluster_unfragmented == 1) {
            col = Colors[DefragStruct::COLORUNFRAGMENTED];
            empty_cluster = false;
        }
        else if (cluster_spacehog == 1) {
            col = Colors[DefragStruct::COLORSPACEHOG];
            empty_cluster = false;
        }

        Color c1;
        Color c2;

        c1.SetFromCOLORREF(col);

        int rr = GetRValue(col) + 200;
        rr = rr > 255 ? 255 : rr;

        int gg = GetGValue(col) + 200;
        gg = gg > 255 ? 255 : gg;

        int bb = GetBValue(col) + 100;
        bb = bb > 255 ? 255 : bb;

        c2.SetFromCOLORREF(RGB((byte)rr, (byte)gg, (byte)bb));

        if (empty_cluster) {
            Rect draw_area2(xx3, yy3, square_size_ - 0, square_size_ - 0);

            LinearGradientBrush bb2(draw_area2, c1, c2, LinearGradientModeVertical);
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
        else {
            Rect draw_area2(xx3, yy3, square_size_ - 0, square_size_ - 0);

            LinearGradientBrush bb1(draw_area2, c2, c1, LinearGradientModeForwardDiagonal);

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
    }

    delete graphics;
}

void DefragGui::on_paint(HDC dc) const {
    /*
        Bitmap bmp(m_clientWindowSize.Width, m_clientWindowSize.Height);
    
        Graphics *graphics2 = Graphics::FromImage(&bmp);
    */

    Graphics graphics(dc);

    /*
        graphics2->DrawImage(m_bmp,0,0);
    */

    /*
        Color busyColor(128,128,128,128);
    
        SolidBrush busyBrush(busyColor);
    
        Rect rr = Rect(100, 100, 400, 100);
    
        graphics2->FillRectangle(&busyBrush, rr);
    
        SolidBrush brush(Color::White);
    
        FontFamily fontFamily(L"Tahoma");
        Font       font(&fontFamily,12,FontStyleRegular, UnitPixel);
        PointF     pointF(132.0f, 120.0f);
    
        WCHAR      *text;
    
        text = Messages[2];
    
        graphics2->DrawString(text, -1, &font, pointF, &brush);
    */

    graphics.DrawImage(bmp_.get(), 0, 0);

    /*
        delete graphics2;
    */

    return;
}

/* Message handler. */
LRESULT CALLBACK DefragGui::process_messagefn(const HWND wnd, const UINT message, const WPARAM w_param,
                                              const LPARAM l_param) {
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:

        /*
                if (wParam == 333)
                {
                    PAINTSTRUCT ps;
        
                    WaitForSingleObject(m_jkDefragGui->m_displayMutex,100);
        
                    m_jkDefragGui->m_displayMutex = CreateMutex(nullptr,FALSE,"JKDefrag");
        
                    m_jkDefragGui->m_hDC = BeginPaint(hWnd, &ps);
        
                    m_jkDefragGui->setDisplayData(m_jkDefragGui->m_hDC);
        
                    m_jkDefragGui->FillSquares( 0, m_jkDefragGui->m_numDiskSquares);
        
                    m_jkDefragGui->PaintImage(m_jkDefragGui->m_hDC);
        
                    EndPaint(hWnd, &ps);
        
                    ReleaseMutex(m_jkDefragGui->m_displayMutex);
        
                    KillTimer(m_jkDefragGui->m_hWnd, m_jkDefragGui->m_sizeTimer);
                }
        */


        InvalidateRect(wnd, nullptr,FALSE);

        return 0;

    case WM_PAINT: {
        /* Grab the display mutex, to make sure that we are the only thread changing the window. */
        WaitForSingleObject(gui_->display_mutex_, 100);

        gui_->display_mutex_ = CreateMutex(nullptr,FALSE, "JKDefrag");

        PAINTSTRUCT ps;

        gui_->dc_ = BeginPaint(wnd, &ps);

        gui_->on_paint(gui_->dc_);

        EndPaint(wnd, &ps);

        ReleaseMutex(gui_->display_mutex_);
    }

        return 0;


    case WM_ERASEBKGND: {
        //			m_jkDefragGui->RedrawScreen = 0;
        InvalidateRect(gui_->wnd_, nullptr,FALSE);
    }

        return 0;
    /*
        case WM_WINDOWPOSCHANGED:
            {
                m_jkDefragGui->RedrawScreen = 0;
                InvalidateRect(m_jkDefragGui->m_hWnd,nullptr,FALSE);
            }
    
            return 0;
    */

    case WM_SIZE: {
        /*
                    m_jkDefragGui->m_sizeTimer = SetTimer(m_jkDefragGui->m_hWnd,333,500,nullptr);
        */


        PAINTSTRUCT ps;

        WaitForSingleObject(gui_->display_mutex_, 100);

        gui_->display_mutex_ = CreateMutex(nullptr,FALSE, "JKDefrag");

        gui_->dc_ = BeginPaint(wnd, &ps);

        gui_->set_display_data(gui_->dc_);

        gui_->fill_squares(0, gui_->num_disk_squares_);

        gui_->paint_image(gui_->dc_);

        EndPaint(wnd, &ps);

        ReleaseMutex(gui_->display_mutex_);
    }

        return 0;
    }

    return DefWindowProc(wnd, message, w_param, l_param);
}

void DefragGui::fill_squares(int clusterStartSquareNum, int clusterEndSquareNum) {
    float clusterPerSquare = (float)(num_clusters_ / num_disk_squares_);

    for (int ii = clusterStartSquareNum; ii <= clusterEndSquareNum; ii++) {
        byte currentColor = DefragStruct::COLOREMPTY;

        byte clusterEmpty = 0;
        byte clusterAllocated = 0;
        byte clusterUnfragmented = 0;
        byte clusterUnmovable = 0;
        byte clusterFragmented = 0;
        byte clusterBusy = 0;
        byte clusterMft = 0;
        byte clusterSpacehog = 0;

        for (int kk = (int)(ii * clusterPerSquare); kk < num_clusters_ && kk < (int)((ii + 1) * clusterPerSquare);
             kk++) {
            switch (cluster_info_[kk]) {
            case DefragStruct::COLOREMPTY:
                clusterEmpty = 1;
                break;
            case DefragStruct::COLORALLOCATED:
                clusterAllocated = 1;
                break;
            case DefragStruct::COLORUNFRAGMENTED:
                clusterUnfragmented = 1;
                break;
            case DefragStruct::COLORUNMOVABLE:
                clusterUnmovable = 1;
                break;
            case DefragStruct::COLORFRAGMENTED:
                clusterFragmented = 1;
                break;
            case DefragStruct::COLORBUSY:
                clusterBusy = 1;
                break;
            case DefragStruct::COLORMFT:
                clusterMft = 1;
                break;
            case DefragStruct::COLORSPACEHOG:
                clusterSpacehog = 1;
                break;
            }
        }

        if (ii < num_disk_squares_) {
            cluster_squares_[ii].dirty_ = true;
            cluster_squares_[ii].color_ = //maxColor;
                    clusterEmpty << 7 |
                    clusterAllocated << 6 |
                    clusterUnfragmented << 5 |
                    clusterUnmovable << 4 |
                    clusterFragmented << 3 |
                    clusterBusy << 2 |
                    clusterMft << 1 |
                    clusterSpacehog;
        }
    }
}

/*

Show a map on the screen of all the clusters on disk. The map shows
which clusters are free and which are in use.
The Data->RedrawScreen flag controls redrawing of the screen. It is set
to "2" (busy) when the subroutine starts. If another thread changes it to
"1" (request) while the subroutine is busy then it will immediately exit
without completing the redraw. When redrawing is completely finished the
flag is set to "0" (no).

*/
void DefragGui::ShowDiskmap(DefragDataStruct* Data) {
    ItemStruct* Item;

    STARTING_LCN_INPUT_BUFFER BitmapParam;

    struct {
        uint64_t StartingLcn;
        uint64_t BitmapSize;

        BYTE Buffer[65536]; /* Most efficient if binary multiple. */
    } BitmapData;

    uint64_t Lcn;
    uint64_t ClusterStart;

    uint32_t ErrorCode;

    int Index;
    int IndexMax;

    BYTE Mask;

    int InUse;
    int PrevInUse;

    DWORD w;

    int i;

    //	*Data->RedrawScreen = 2;                       /* Set the flag to "busy". */

    /* Exit if the library is not processing a disk yet. */
    if (Data->disk_.volume_handle_ == nullptr) {
        //		*Data->RedrawScreen = 0;                       /* Set the flag to "no". */
        return;
    }

    /* Clear screen. */
    clear_screen(nullptr);

    /* Show the map of all the clusters in use. */
    Lcn = 0;
    ClusterStart = 0;
    PrevInUse = 1;

    do {
        if (*Data->running_ != RunningState::RUNNING) break;
        //		if (*Data->RedrawScreen != 2) break;
        if (Data->disk_.volume_handle_ == INVALID_HANDLE_VALUE) break;

        /* Fetch a block of cluster data. */
        BitmapParam.StartingLcn.QuadPart = Lcn;

        ErrorCode = DeviceIoControl(Data->disk_.volume_handle_,FSCTL_GET_VOLUME_BITMAP,
                                    &BitmapParam, sizeof BitmapParam, &BitmapData, sizeof BitmapData, &w,
                                    nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        }
        else {
            ErrorCode = GetLastError();
        }

        if (ErrorCode != NO_ERROR && ErrorCode != ERROR_MORE_DATA) break;

        /* Sanity check. */
        if (Lcn >= BitmapData.StartingLcn + BitmapData.BitmapSize) break;

        /* Analyze the clusterdata. We resume where the previous block left off. */
        Lcn = BitmapData.StartingLcn;
        Index = 0;
        Mask = 1;

        IndexMax = sizeof BitmapData.Buffer;

        if (BitmapData.BitmapSize / 8 < IndexMax) IndexMax = (int)(BitmapData.BitmapSize / 8);

        while (Index < IndexMax && *Data->running_ == RunningState::RUNNING) {
            InUse = BitmapData.Buffer[Index] & Mask;

            /* If at the beginning of the disk then copy the InUse value as our
            starting value. */
            if (Lcn == 0) PrevInUse = InUse;

            /* At the beginning and end of an Exclude draw the cluster. */
            if (Lcn == Data->mft_excludes_[0].start_ || Lcn == Data->mft_excludes_[0].end_ ||
                Lcn == Data->mft_excludes_[1].start_ || Lcn == Data->mft_excludes_[1].end_ ||
                Lcn == Data->mft_excludes_[2].start_ || Lcn == Data->mft_excludes_[2].end_) {
                if (Lcn == Data->mft_excludes_[0].end_ ||
                    Lcn == Data->mft_excludes_[1].end_ ||
                    Lcn == Data->mft_excludes_[2].end_) {
                    draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLORUNMOVABLE);
                }
                else if (PrevInUse == 0) {
                    draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLOREMPTY);
                }
                else {
                    draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLORALLOCATED);
                }

                InUse = 1;
                PrevInUse = 1;
                ClusterStart = Lcn;
            }

            if (PrevInUse == 0 && InUse != 0) /* Free */
            {
                draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLOREMPTY);

                ClusterStart = Lcn;
            }

            if (PrevInUse != 0 && InUse == 0) /* In use */
            {
                draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLORALLOCATED);

                ClusterStart = Lcn;
            }

            PrevInUse = InUse;

            if (Mask == 128) {
                Mask = 1;
                Index = Index + 1;
            }
            else {
                Mask = Mask << 1;
            }

            Lcn = Lcn + 1;
        }
    }
    while (ErrorCode == ERROR_MORE_DATA && Lcn < BitmapData.StartingLcn + BitmapData.BitmapSize);

    if (Lcn > 0/* && (*Data->RedrawScreen == 2)*/) {
        if (PrevInUse == 0) /* Free */
        {
            draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLOREMPTY);
        }

        if (PrevInUse != 0) /* In use */
        {
            draw_cluster(Data, ClusterStart, Lcn, DefragStruct::COLORALLOCATED);
        }
    }

    /* Show the MFT zones. */
    for (i = 0; i < 3; i++) {
        //		if (*Data->RedrawScreen != 2) break;
        if (Data->mft_excludes_[i].start_ <= 0) continue;

        draw_cluster(Data, Data->mft_excludes_[i].start_, Data->mft_excludes_[i].end_, DefragStruct::COLORMFT);
    }

    /* Colorize all the files on the screen.
    Note: the "$BadClus" file on NTFS disks maps the entire disk, so we have to
    ignore it. */
    for (Item = defrag_lib_->tree_smallest(Data->item_tree_); Item != nullptr; Item = defrag_lib_->tree_next(Item)) {
        if (*Data->running_ != RunningState::RUNNING) break;
        //		if (*Data->RedrawScreen != 2) break;

        if (Item->long_filename_ != nullptr &&
            (_wcsicmp(Item->long_filename_, L"$BadClus") == 0 ||
                _wcsicmp(Item->long_filename_, L"$BadClus:$Bad:$DATA") == 0))
            continue;

        defrag_lib_->colorize_item(Data, Item, 0, 0, false);
    }

    /* Set the flag to "no". */
    //	if (*Data->RedrawScreen == 2) *Data->RedrawScreen = 0;
}
