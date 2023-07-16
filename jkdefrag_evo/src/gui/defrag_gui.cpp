#include "precompiled_header.h"

#include <memory>
#include <format>

DefragGui *DefragGui::instance_ = nullptr;

DefragGui::DefragGui() : debug_level_(), color_map_() {
    defrag_lib_ = DefragLib::get_instance();
    defrag_struct_ = std::make_unique<DefragStruct>();

    square_size_ = 6;

    offset_x_ = 26;
    offset_y_ = 16;

    num_clusters_ = 1;

    progress_start_time_ = 0;
    progress_time_ = 0;
    progress_done_ = 0;

    for (auto &m: messages_) m[0] = L'\0';
}

DefragGui *DefragGui::get_instance() {
    if (instance_ == nullptr) {
        instance_ = new DefragGui();
    }

    return instance_;
}

int DefragGui::initialize(HINSTANCE instance, const int cmd_show, DefragLog *log, const DebugLevel debug_level) {
    ULONG_PTR gdiplus_token;

    const GdiplusStartupInput gdiplus_startup_input;

    GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, nullptr);

    log_ = log;
    debug_level_ = debug_level;

    static const auto mutex_name = APP_NAME "Mutex";
    static const auto window_class_name = APP_NAME "Class";

    display_mutex_ = CreateMutex(nullptr, FALSE, mutex_name);

    wnd_class_.cbClsExtra = 0;
    wnd_class_.cbWndExtra = 0;
    wnd_class_.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
    wnd_class_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wnd_class_.hIcon = LoadIcon(nullptr, MAKEINTRESOURCE(1));
    wnd_class_.hInstance = instance;
    wnd_class_.lpfnWndProc = (WNDPROC) DefragGui::process_messagefn;
    wnd_class_.lpszClassName = window_class_name;
    wnd_class_.lpszMenuName = nullptr;
    wnd_class_.style = CS_HREDRAW | CS_VREDRAW;
    wnd_class_.cbSize = sizeof(WNDCLASSEX);
    wnd_class_.hIconSm = LoadIcon(instance, MAKEINTRESOURCE(1));

    CHAR version_str[100];

    LoadString(instance, 2, version_str, 99);

    if (RegisterClassEx(&wnd_class_) == 0) {
        MessageBoxW(nullptr, L"Cannot register class", defrag_struct_->versiontext_.c_str(),
                    MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    const wchar_t *defrag_window_class = APP_NAME_W L"Class";
    wnd_ = CreateWindowW(defrag_window_class, defrag_struct_->versiontext_.c_str(), WS_TILEDWINDOW,
                         CW_USEDEFAULT, 0, 1024, 768, nullptr, nullptr, instance, nullptr);

    if (wnd_ == nullptr) {
        MessageBoxW(nullptr, L"Cannot create window", defrag_struct_->versiontext_.c_str(),
                    MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Show the window in the state that Windows has specified, minimized or maximized
    ShowWindow(wnd_, cmd_show);
    UpdateWindow(wnd_);

    SetTimer(wnd_, 1, 300, nullptr);

    return 1;
}

WPARAM DefragGui::do_modal() {
    // The main message thread
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

void DefragGui::set_display_data(HDC dc) {
    const Graphics graphics(dc);
    Rect client_window_size;

    graphics.GetVisibleClipBounds(&client_window_size);

    client_size_ = client_window_size;
    top_area_height_ = 33;

    if (debug_level_ > DebugLevel::Warning) {
        top_area_height_ = 49;
    }

    disk_area_size_.Width = client_window_size.Width - offset_x_ * 2;
    disk_area_size_.Height = client_window_size.Height - top_area_height_ - offset_y_ * 2;

    color_map_.set_size((size_t) (disk_area_size_.Width / square_size_),
                        (size_t) (disk_area_size_.Height / square_size_));

    // Find centered position for the disk map
    diskmap_pos_ = {
            .x = (client_size_.Width - (int) color_map_.get_width() * square_size_) / 2,
            .y = (client_size_.Height - top_area_height_ - (int) color_map_.get_height() * square_size_) / 2
    };

    bmp_ = std::make_unique<Bitmap>(client_size_.Width, client_size_.Height);
}

// Callback: clear the screen
void DefragGui::clear_screen(std::wstring &&text) {
    // Save the message in messages[0]
    messages_[0] = std::move(text);

    // Clear all the other messages
    for (auto i = 1; i < sizeof(messages_) / sizeof(messages_[0]); i++) {
        messages_[i].clear();
    }

    // If there is no logfile then return. */
    if (log_ != nullptr) {
        log_->log(messages_[0].c_str());
    }

    repaint_window(dc_);
}

// Callback: whenever an item (file, directory) is moved on disk.
void DefragGui::show_move(const ItemStruct *item, const uint64_t clusters, const uint64_t from_lcn,
                          const uint64_t to_lcn, const uint64_t from_vcn) {
    // Save the message in Messages 3
    if (clusters == 1) {
        messages_[3] = std::format(MOVING_1_CLUSTER_FMT, from_lcn, to_lcn);
    } else {
        messages_[3] = std::format(MOVING_CLUSTERS_FMT, clusters, from_lcn, to_lcn);
    }

    // Save the name of the file in Messages 4
    if (item != nullptr && item->have_long_path()) {
        messages_[4] = item->get_long_path();
    } else {
        messages_[4].clear();
    }

    // If debug mode then write a message to the logfile.
    if (debug_level_ < DebugLevel::DetailedProgress) return;

    if (from_vcn > 0) {
        if (clusters % 10 == 1) {
            log_->log(std::format(L"{}\n  Moving 1 cluster from " NUM_FMT " to " NUM_FMT ", VCN=" NUM_FMT,
                                  item->get_long_path(), from_lcn, to_lcn, from_vcn));
        } else {
            log_->log(std::format(L"{}\n  Moving " NUM_FMT " clusters from " NUM_FMT " to " NUM_FMT ", VCN=" NUM_FMT,
                                  item->get_long_path(), clusters, from_lcn, to_lcn, from_vcn));
        }
    } else {
        if (clusters % 10 == 1) {
            log_->log(std::format(L"{}\n  " MOVING_1_CLUSTER_FMT, item->get_long_path(), from_lcn, to_lcn));
        } else {
            log_->log(std::format(L"{}\n  " MOVING_CLUSTERS_FMT, item->get_long_path(), clusters, from_lcn, to_lcn));
        }
    }
    repaint_window(dc_);
}


/* Callback: for every file during analysis.
This subroutine is called one last time with Item=nullptr when analysis has finished. */
void DefragGui::show_analyze(const DefragDataStruct *data, const ItemStruct *item) {
    if (data != nullptr && data->count_all_files_ != 0) {
        messages_[3] = std::format(L"Files " NUM_FMT ", Directories " NUM_FMT ", Clusters " NUM_FMT,
                                   data->count_all_files_, data->count_directories_, data->count_all_clusters_);
    } else {
        messages_[3] = L"Applying Exclude and SpaceHogs masks....";
    }

    // Save the name of the file in Messages 4
    if (item != nullptr && item->have_long_path()) {
        messages_[4] = item->get_long_path();
    } else {
        messages_[4].clear();
    }
    repaint_window(dc_);
}

// Callback: show a debug message
void DefragGui::show_debug(const DebugLevel level, const ItemStruct *item, std::wstring &&text) {
    if (debug_level_ < level) return;

    // Save the name of the file in messages[4]
    if (item != nullptr && item->have_long_path()) {
        messages_[4] = item->get_long_path();
    }

    // Save the debug message in Messages 5.
    messages_[5] = std::move(text);
    log_->log(messages_[5].c_str());

    repaint_window(dc_);
}

// Callback: paint a cluster on the screen in a given palette color
void DefragGui::draw_cluster(const DefragDataStruct *data, const uint64_t cluster_start, const uint64_t cluster_end,
                             const DrawColor color) {
    __timeb64 now{};
    [[maybe_unused]] Rect window_size = client_size_;

    // Save the PhaseTodo and PhaseDone counters for later use by the progress counter
    if (data->phase_todo_ != 0) {
        _ftime64_s(&now);

        progress_time_ = now.time * 1000 + now.millitm;
        progress_done_ = data->phase_done_;
        progress_todo_ = data->phase_todo_;
    }

#ifndef _WIN64
    // 32-bit drive is too big check
    if (data->total_clusters_ > 0x7FFFFFFF) {
        messages_[3] = L"Drive is too big for the 32-bit version to load";
        paint_image(dc_);
        return;
    }
#endif

    // Sanity check
    if (data->total_clusters_ == 0) return;
    if (dc_ == nullptr) return;
    if (cluster_start == cluster_end) return;

    WaitForSingleObject(display_mutex_, 100);

    display_mutex_ = CreateMutex(nullptr, FALSE, DISPLAY_MUTEX);

    if (num_clusters_ != data->total_clusters_ || cluster_info_ == nullptr) {
        num_clusters_ = data->total_clusters_;
        cluster_info_ = std::make_unique<DrawColor[]>(num_clusters_);

        auto ci_mem = cluster_info_.get();
        std::fill(cluster_info_.get(), cluster_info_.get() + num_clusters_, DrawColor::Empty);

        return;
    }

    auto ci_mem = cluster_info_.get();
    for (uint64_t ii = cluster_start; ii <= cluster_end; ii++) {
        ci_mem[ii] = color;
    }

    const auto cluster_per_square = (float) (num_clusters_ / color_map_.get_total_count());
    const int cluster_start_square_num = (int) ((uint64_t) cluster_start / (uint64_t) cluster_per_square);
    const int cluster_end_square_num = (int) ((uint64_t) cluster_end / (uint64_t) cluster_per_square);

    prepare_cells_for_cluster_range(cluster_start_square_num, cluster_end_square_num);

    ReleaseMutex(display_mutex_);
    repaint_window(dc_);
}

// Callback: just before the defragger starts a new Phase, and when it finishes
void DefragGui::show_status(const DefragDataStruct *data) {
    // Reset the progress counter
    __timeb64 now{};
    _ftime64_s(&now);

    progress_start_time_ = now.time * 1000 + now.millitm;
    progress_time_ = progress_start_time_;
    progress_done_ = 0;
    progress_todo_ = 0;

    // Reset all the messages.
    for (auto &message: messages_) message.clear();

    // Update Message 0 and 1
    if (data != nullptr) {
        messages_[0] = data->disk_.mount_point_.get();

        switch (data->phase_) {
            case 1:
                messages_[1] = L"Phase 1: Analyze";
                break;
            case 2:
                messages_[1] = L"Phase 2: Defragment";
                break;
            case 3:
                messages_[1] = L"Phase 3: ForcedFill";
                break;
            case 4:
                messages_[1] = std::format(L"Zone {}: Sort", data->zone_ + 1);
                break;
            case 5:
                messages_[1] = std::format(L"Zone {}: Fast Optimize", data->zone_ + 1);
                break;
            case 6:
                messages_[1] = L"Phase 3: Move Up";
                break;
            case 7:
                messages_[1] = L"Finished.";
                messages_[4] = std::format(L"Logfile: {}", log_->get_log_filename());
                break;
            case 8:
                messages_[1] = L"Phase 3: Fixup";
                break;
        }

        log_->log(messages_[1]);
    }

    // Write some statistics to the logfile
    if (data != nullptr && data->phase_ == 7) {
        ItemStruct *largest_items[25];
        uint64_t total_clusters;
        uint64_t total_bytes;
        uint64_t total_fragments;
        int fragments;
        ItemStruct *item;
        log_->log(std::format(L"- Total disk space: " NUM_FMT " bytes ({:.3} gigabytes), " NUM_FMT " clusters",
                              data->bytes_per_cluster_ * data->total_clusters_,
                              (double) (data->bytes_per_cluster_ * data->total_clusters_) / (1024 * 1024 * 1024),
                              data->total_clusters_));
        log_->log(std::format(L"- Bytes per cluster: " NUM_FMT " bytes", data->bytes_per_cluster_));
        log_->log(std::format(L"- Number of files: " NUM_FMT, data->count_all_files_));
        log_->log(std::format(L"- Number of directories: " NUM_FMT, data->count_directories_));
        log_->log(std::format(
                L"- Total size of analyzed items: " NUM_FMT " bytes ({:.3} gigabytes), " NUM_FMT " clusters",
                data->count_all_clusters_ * data->bytes_per_cluster_,
                (double) (data->count_all_clusters_ * data->bytes_per_cluster_) / (1024 * 1024 * 1024),
                data->count_all_clusters_));

        if (data->count_all_files_ + data->count_directories_ > 0) {
            log_->log(std::format(L"- Number of fragmented items: " NUM_FMT " (" FLT4_FMT "% of all items)",
                                  data->count_fragmented_items_,
                                  (double) (data->count_fragmented_items_ * 100)
                                  / (data->count_all_files_ + data->count_directories_)));
        } else {
            log_->log(std::format(L"- Number of fragmented items: " NUM_FMT, data->count_fragmented_items_));
        }

        if (data->count_all_clusters_ > 0 && data->total_clusters_ > 0) {
            log_->log(
                    std::format(
                            L"- Total size of fragmented items: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of all items, " FLT4_FMT "% of disk",
                            data->count_fragmented_clusters_ * data->bytes_per_cluster_,
                            data->count_fragmented_clusters_,
                            (double) (data->count_fragmented_clusters_ * 100) / data->count_all_clusters_,
                            (double) (data->count_fragmented_clusters_ * 100) / data->total_clusters_));
        } else {
            log_->log(std::format(L"- Total size of fragmented items: " NUM_FMT " bytes, " NUM_FMT " clusters",
                                  data->count_fragmented_clusters_ * data->bytes_per_cluster_,
                                  data->count_fragmented_clusters_));
        }

        if (data->total_clusters_ > 0) {
            log_->log(std::format(L"- Free disk space: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of disk",
                                  data->count_free_clusters_ * data->bytes_per_cluster_,
                                  data->count_free_clusters_,
                                  (double) (data->count_free_clusters_ * 100) / data->total_clusters_));
        } else {
            log_->log(std::format(L"- Free disk space: " NUM_FMT " bytes, " NUM_FMT " clusters",
                                  data->count_free_clusters_ * data->bytes_per_cluster_, data->count_free_clusters_));
        }

        log_->log(std::format(L"- Number of gaps: " NUM_FMT, data->count_gaps_));

        if (data->count_gaps_ > 0) {
            log_->log(std::format(L"- Number of small gaps: " NUM_FMT " (" FLT4_FMT "% of all gaps)",
                                  data->count_gaps_less16_,
                                  (double) (data->count_gaps_less16_ * 100) / data->count_gaps_));
        } else {
            log_->log(std::format(L"- Number of small gaps: " NUM_FMT, data->count_gaps_less16_));
        }

        if (data->count_free_clusters_ > 0) {
            log_->log(std::format(
                    L"- Size of small gaps: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of free disk space",
                    data->count_clusters_less16_ * data->bytes_per_cluster_, data->count_clusters_less16_,
                    (double) (data->count_clusters_less16_ * 100) / data->count_free_clusters_));
        } else {
            log_->log(std::format(L"- Size of small gaps: " NUM_FMT " bytes, " NUM_FMT " clusters",
                                  data->count_clusters_less16_ * data->bytes_per_cluster_,
                                  data->count_clusters_less16_));
        }

        if (data->count_gaps_ > 0) {
            log_->log(std::format(L"- Number of big gaps: " NUM_FMT " (" FLT4_FMT "% of all gaps)",
                                  data->count_gaps_ - data->count_gaps_less16_,
                                  (double) ((data->count_gaps_ - data->count_gaps_less16_) * 100) / data->count_gaps_));
        } else {
            log_->log(std::format(L"- Number of big gaps: " NUM_FMT, data->count_gaps_ - data->count_gaps_less16_));
        }

        if (data->count_free_clusters_ > 0) {
            log_->log(std::format(
                    L"- Size of big gaps: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of free disk space",
                    (data->count_free_clusters_ - data->count_clusters_less16_) * data->bytes_per_cluster_,
                    data->count_free_clusters_ - data->count_clusters_less16_,
                    (double) ((data->count_free_clusters_ - data->count_clusters_less16_) * 100) / data->
                            count_free_clusters_));
        } else {
            log_->log(std::format(L"- Size of big gaps: " NUM_FMT " bytes, " NUM_FMT " clusters",
                                  (data->count_free_clusters_ - data->count_clusters_less16_) *
                                  data->bytes_per_cluster_,
                                  data->count_free_clusters_ - data->count_clusters_less16_));
        }

        if (data->count_gaps_ > 0) {
            log_->log(std::format(L"- Average gap size: " FLT4_FMT " clusters",
                                  (double) data->count_free_clusters_ / data->count_gaps_));
        }

        if (data->count_free_clusters_ > 0) {
            log_->log(std::format(
                    L"- Biggest gap: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of free disk space",
                    data->biggest_gap_ * data->bytes_per_cluster_, data->biggest_gap_,
                    (double) (data->biggest_gap_ * 100) / data->count_free_clusters_));
        } else {
            log_->log(std::format(L"- Biggest gap: " NUM_FMT " bytes, " NUM_FMT " clusters",
                                  data->biggest_gap_ * data->bytes_per_cluster_, data->biggest_gap_));
        }

        if (data->total_clusters_ > 0) {
            log_->log(std::format(L"- Average end-begin distance: " FLT0_FMT " clusters, " FLT4_FMT "% of volume size",
                                  data->average_distance_, 100.0 * data->average_distance_ / data->total_clusters_));
        } else {
            log_->log(std::format(L"- Average end-begin distance: " FLT0_FMT " clusters", data->average_distance_));
        }

        for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
            if (!item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->is_dir_ && data->cannot_move_dirs_ > 20) continue;
            break;
        }

        if (item != nullptr) {
            log_->log(L"These items could not be moved:");
            log_->log(L"  Fragments       Bytes  Clusters Name");

            total_fragments = 0;
            total_bytes = 0;
            total_clusters = 0;

            for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
                if (!item->is_unmovable_) continue;
                if (item->is_excluded_) continue;
                if (item->is_dir_ && data->cannot_move_dirs_ > 20) continue;
                if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
                     _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
                    continue;
                }

                fragments = DefragLib::get_fragment_count(item);

                if (!item->have_long_path()) {
                    log_->log(std::format(L"  " SUMMARY_FMT " [at cluster " NUM_FMT "]",
                                          fragments, item->bytes_, item->clusters_count_,
                                          item->get_item_lcn()));
                } else {
                    log_->log(std::format(L"  " SUMMARY_FMT " {}", fragments, item->bytes_, item->clusters_count_,
                                          item->get_long_path()));
                }

                total_fragments = total_fragments + fragments;
                total_bytes = total_bytes + item->bytes_;
                total_clusters = total_clusters + item->clusters_count_;
            }

            log_->log(L"  " SUMMARY_DASH_LINE);
            log_->log(std::format(L"  " SUMMARY_FMT " Total",
                                  total_fragments, total_bytes, total_clusters));
        }

        for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
            if (item->is_excluded_) continue;
            if (item->is_dir_ && data->cannot_move_dirs_ > 20) continue;

            fragments = DefragLib::get_fragment_count(item);

            if (fragments <= 1) continue;

            break;
        }

        if (item != nullptr) {
            log_->log(L"These items are still fragmented:");
            log_->log(L"  Fragments       Bytes  Clusters Name");

            total_fragments = 0;
            total_bytes = 0;
            total_clusters = 0;

            for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
                if (item->is_excluded_) continue;
                if (item->is_dir_ && data->cannot_move_dirs_ > 20) continue;

                fragments = DefragLib::get_fragment_count(item);

                if (fragments <= 1) continue;

                if (!item->have_long_path()) {
                    log_->log(std::format(L"  " SUMMARY_FMT " [at cluster " NUM_FMT "]",
                                          fragments, item->bytes_, item->clusters_count_,
                                          item->get_item_lcn()));
                } else {
                    log_->log(std::format(L"  " SUMMARY_FMT " {}",
                                          fragments, item->bytes_, item->clusters_count_, item->get_long_path()));
                }

                total_fragments = total_fragments + fragments;
                total_bytes = total_bytes + item->bytes_;
                total_clusters = total_clusters + item->clusters_count_;
            }

            log_->log(L"  " SUMMARY_DASH_LINE);
            log_->log(std::format(L"  " SUMMARY_FMT " Total",
                                  total_fragments, total_bytes, total_clusters));
        }

        int last_largest = 0;

        for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
            if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
                 _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
                continue;
            }

            int i;
            for (i = last_largest - 1; i >= 0; i--) {
                if (item->clusters_count_ < largest_items[i]->clusters_count_) break;

                if (item->clusters_count_ == largest_items[i]->clusters_count_ &&
                    item->bytes_ < largest_items[i]->bytes_) {
                    break;
                }

                if (item->clusters_count_ == largest_items[i]->clusters_count_ &&
                    item->bytes_ == largest_items[i]->bytes_ &&
                    _wcsicmp(item->get_long_fn(), largest_items[i]->get_long_fn()) > 0) {
                    break;
                }
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
            log_->log(L"The 25 largest items on disk:");
            log_->log(L"  Fragments       Bytes  Clusters Name");

            for (auto i = 0; i < last_largest; i++) {
                if (!largest_items[i]->have_long_path()) {
                    log_->log(std::format(L"  " SUMMARY_FMT " [at cluster " NUM_FMT "]",
                                          DefragLib::get_fragment_count(largest_items[i]),
                                          largest_items[i]->bytes_, largest_items[i]->clusters_count_,
                                          largest_items[i]->get_item_lcn()));
                } else {
                    log_->log(std::format(L"  " SUMMARY_FMT " {}",
                                          DefragLib::get_fragment_count(largest_items[i]),
                                          largest_items[i]->bytes_, largest_items[i]->clusters_count_,
                                          largest_items[i]->get_long_path()));
                }
            }
        }
    }
}

void DefragGui::on_paint(HDC dc) const {
    Graphics graphics(dc);
    graphics.DrawImage(bmp_.get(), 0, 0);
}

// Message handler
LRESULT CALLBACK DefragGui::process_messagefn(HWND wnd, const UINT message, const WPARAM w_param,
                                              const LPARAM l_param) {
    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_TIMER:
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;

        case WM_PAINT: {
            // Grab the display mutex, to make sure that we are the only thread changing the window
            WaitForSingleObject(instance_->display_mutex_, 100);
            instance_->display_mutex_ = CreateMutex(nullptr, FALSE, DISPLAY_MUTEX);

            PAINTSTRUCT ps;

            instance_->dc_ = BeginPaint(wnd, &ps);
            instance_->on_paint(instance_->dc_);
            EndPaint(wnd, &ps);

            ReleaseMutex(instance_->display_mutex_);
        }

            return 0;

        case WM_ERASEBKGND: {
            InvalidateRect(instance_->wnd_, nullptr, FALSE);
            return 0;
        }

        case WM_SIZE: {
            PAINTSTRUCT ps;

            WaitForSingleObject(instance_->display_mutex_, 100);

            instance_->display_mutex_ = CreateMutex(nullptr, FALSE, DISPLAY_MUTEX);

            {
                instance_->dc_ = BeginPaint(wnd, &ps);
                instance_->set_display_data(instance_->dc_);
                instance_->prepare_cells_for_cluster_range(0, instance_->color_map_.get_total_count());
                instance_->repaint_window(instance_->dc_);
                EndPaint(wnd, &ps);
            }

            ReleaseMutex(instance_->display_mutex_);
            return 0;
        }
    }

    return DefWindowProc(wnd, message, w_param, l_param);
}

/*
Show a map on the screen of all the clusters on disk. The map shows
which clusters are free and which are in use.
The data->RedrawScreen flag controls redrawing of the screen. It is set
to "2" (busy) when the subroutine starts. If another thread changes it to
"1" (request) while the subroutine is busy then it will immediately exit
without completing the redraw. When redrawing is completely finished the
flag is set to "0" (no).
*/
void DefragGui::show_diskmap(DefragDataStruct *data) {
    ItemStruct *item;
    STARTING_LCN_INPUT_BUFFER bitmap_param;
    struct {
        uint64_t StartingLcn;
        uint64_t BitmapSize;

        BYTE Buffer[65536]; // Most efficient if binary multiple
    } bitmap_data{};

    uint64_t Lcn;
    uint64_t cluster_start;
    uint32_t error_code;
    int index;
    int index_max;
    BYTE mask;
    int in_use;
    int prev_in_use;
    DWORD w;

    // Exit if the library is not processing a disk yet.
    if (data->disk_.volume_handle_ == nullptr) {
        return;
    }

    // Clear screen
    clear_screen({});

    // Show the map of all the clusters in use
    Lcn = 0;
    cluster_start = 0;
    prev_in_use = 1;

    do {
        if (*data->running_ != RunningState::RUNNING) break;
        //		if (*data->RedrawScreen != 2) break;
        if (data->disk_.volume_handle_ == INVALID_HANDLE_VALUE) break;

        // Fetch a block of cluster data
        bitmap_param.StartingLcn.QuadPart = Lcn;

        error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                     &bitmap_param, sizeof bitmap_param, &bitmap_data, sizeof bitmap_data, &w,
                                     nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        // Sanity check
        if (Lcn >= bitmap_data.StartingLcn + bitmap_data.BitmapSize) break;

        // Analyze the clusterdata. We resume where the previous block left off
        Lcn = bitmap_data.StartingLcn;
        index = 0;
        mask = 1;

        index_max = sizeof bitmap_data.Buffer;

        if (bitmap_data.BitmapSize / 8 < index_max) index_max = (int) (bitmap_data.BitmapSize / 8);

        while (index < index_max && *data->running_ == RunningState::RUNNING) {
            in_use = bitmap_data.Buffer[index] & mask;

            /* If at the beginning of the disk then copy the in_use value as our
            starting value. */
            if (Lcn == 0) prev_in_use = in_use;

            // At the beginning and end of an Exclude draw the cluster
            if (Lcn == data->mft_excludes_[0].start_ || Lcn == data->mft_excludes_[0].end_ ||
                Lcn == data->mft_excludes_[1].start_ || Lcn == data->mft_excludes_[1].end_ ||
                Lcn == data->mft_excludes_[2].start_ || Lcn == data->mft_excludes_[2].end_) {
                if (Lcn == data->mft_excludes_[0].end_ ||
                    Lcn == data->mft_excludes_[1].end_ ||
                    Lcn == data->mft_excludes_[2].end_) {
                    draw_cluster(data, cluster_start, Lcn, DrawColor::Unmovable);
                } else if (prev_in_use == 0) {
                    draw_cluster(data, cluster_start, Lcn, DrawColor::Empty);
                } else {
                    draw_cluster(data, cluster_start, Lcn, DrawColor::Allocated);
                }

                in_use = 1;
                prev_in_use = 1;
                cluster_start = Lcn;
            }

            // Free
            if (prev_in_use == 0 && in_use != 0) {
                draw_cluster(data, cluster_start, Lcn, DrawColor::Empty);
                cluster_start = Lcn;
            }

            // In use
            if (prev_in_use != 0 && in_use == 0) {
                draw_cluster(data, cluster_start, Lcn, DrawColor::Allocated);
                cluster_start = Lcn;
            }

            prev_in_use = in_use;

            if (mask == 128) {
                mask = 1;
                index = index + 1;
            } else {
                mask = mask << 1;
            }

            Lcn = Lcn + 1;
        }
    } while (error_code == ERROR_MORE_DATA && Lcn < bitmap_data.StartingLcn + bitmap_data.BitmapSize);

    if (Lcn > 0) {
        if (prev_in_use == 0) {
            // Free
            draw_cluster(data, cluster_start, Lcn, DrawColor::Empty);
        } else {
            // in use
            draw_cluster(data, cluster_start, Lcn, DrawColor::Allocated);
        }
    }

    // Show the MFT zones
    for (auto i = 0; i < 3; i++) {
        //		if (*data->RedrawScreen != 2) break;
        if (data->mft_excludes_[i].start_ <= 0) continue;

        draw_cluster(data, data->mft_excludes_[i].start_, data->mft_excludes_[i].end_, DrawColor::Mft);
    }

    /* Colorize all the files on the screen.
    Note: the "$BadClus" file on NTFS disks maps the entire disk, so we have to
    ignore it. */
    for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
        if (*data->running_ != RunningState::RUNNING) break;
        //		if (*data->RedrawScreen != 2) break;

        if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0))
            continue;

        defrag_lib_->colorize_disk_item(data, item, 0, 0, false);
    }

    // Set the flag to "no"
    //	if (*data->RedrawScreen == 2) *data->RedrawScreen = 0;
}

