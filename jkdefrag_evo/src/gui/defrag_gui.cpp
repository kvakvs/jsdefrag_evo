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

#include <memory>
#include <format>
#include <app.h>

DefragGui *DefragGui::instance_ = nullptr;

DefragGui::DefragGui() : color_map_(), diskmap_pos_() {
    defrag_lib_ = DefragRunner::get_instance();

    square_size_ = 6;
    drawing_area_offset_ = {.x=8, .y=8};
    num_clusters_ = 1;
}

DefragGui *DefragGui::get_instance() {
    if (instance_ == nullptr) {
        instance_ = new DefragGui();
    }

    return instance_;
}

int DefragGui::initialize(HINSTANCE instance, const int cmd_show, const DebugLevel debug_level) {
    ULONG_PTR gdiplus_token;
    const GdiplusStartupInput gdiplus_startup_input;

    GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, nullptr);

    DefragLog::debug_level_ = debug_level;

    static const auto window_class_name = APP_NAME "Class";

    wnd_class_ = {
            .cbSize = sizeof(WNDCLASSEX),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = (WNDPROC) DefragGui::process_messagefn,
            .cbClsExtra = 0,
            .cbWndExtra = 0,
            .hInstance = instance,
            .hIcon = LoadIcon(nullptr, MAKEINTRESOURCE(1)),
            .hCursor = LoadCursor(nullptr, IDC_ARROW),
            .hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH),
            .lpszMenuName = nullptr,
            .lpszClassName = window_class_name,
            .hIconSm = LoadIcon(instance, MAKEINTRESOURCE(1)),
    };

    CHAR version_str[100];

    LoadString(instance, 2, version_str, 99);

    if (RegisterClassEx(&wnd_class_) == 0) {
        MessageBoxW(nullptr, L"Cannot register class", DefragApp::versiontext_,
                    MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    const wchar_t *defrag_window_class = APP_NAME_W L"Class";
    wnd_ = CreateWindowW(defrag_window_class, DefragApp::versiontext_, WS_TILEDWINDOW,
                         CW_USEDEFAULT, 0, 1024, 768, nullptr, nullptr, instance, nullptr);

    if (wnd_ == nullptr) {
        MessageBoxW(nullptr, L"Cannot create window", DefragApp::versiontext_,
                    MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Show the window in the state that Windows has specified, minimized or maximized
    ShowWindow(wnd_, cmd_show);
    UpdateWindow(wnd_);

    SetTimer(wnd_, 1, 1000, nullptr);

    return 1;
}

WPARAM DefragGui::windows_event_loop() {
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

    if (DefragLog::debug_level_ > DebugLevel::Warning) {
        top_area_height_ = 49;
    } else {
        top_area_height_ = 33;
    }

    disk_area_size_.Width = client_window_size.Width - drawing_area_offset_.x * 2;
    disk_area_size_.Height = client_window_size.Height - top_area_height_ - drawing_area_offset_.y * 2;

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

    // If there is no logfile then return.
    Log::log(DebugLevel::DetailedProgress, messages_[0].c_str());

    request_redraw();
}

// Callback: whenever an item (file, directory) is moved on disk.
void DefragGui::show_move(const FileNode *item, const uint64_t clusters, const uint64_t from_lcn,
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
    if (DefragLog::debug_level_ < DebugLevel::DetailedProgress) return;

    if (from_vcn > 0) {
        if (clusters % 10 == 1) {
            Log::log(DebugLevel::DetailedProgress,
                     std::format(L"{}\n  Moving 1 cluster from " NUM_FMT " to " NUM_FMT ", VCN=" NUM_FMT,
                                 item->get_long_path(), from_lcn, to_lcn, from_vcn));
        } else {
            Log::log(DebugLevel::DetailedProgress,
                     std::format(L"{}\n  Moving " NUM_FMT " clusters from " NUM_FMT " to " NUM_FMT ", VCN=" NUM_FMT,
                                 item->get_long_path(), clusters, from_lcn, to_lcn, from_vcn));
        }
    } else {
        if (clusters % 10 == 1) {
            Log::log(DebugLevel::DetailedProgress,
                     std::format(L"{}\n  " MOVING_1_CLUSTER_FMT, item->get_long_path(), from_lcn, to_lcn));
        } else {
            Log::log(DebugLevel::DetailedProgress,
                     std::format(L"{}\n  " MOVING_CLUSTERS_FMT, item->get_long_path(), clusters, from_lcn, to_lcn));
        }
    }

    request_redraw();
}

// Make sure this function does not run more often than 100ms
static bool show_analyze_throttle_timer() {
    static std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto duration = current_time - last_time;

    if (duration < std::chrono::milliseconds(100)) {
        return false;
    }

    last_time = current_time;
    return true;
}

void DefragGui::show_analyze_update_item_text(const FileNode *item) {
    // Save the name of the file in Messages 4
    if (item != nullptr && item->have_long_path()) {
        messages_[4] = item->get_long_path();
    } else {
        messages_[4].clear();
    }
}

void DefragGui::show_analyze_no_state(const FileNode *item) {
    if (!show_analyze_throttle_timer()) return;

    messages_[3] = L"Applying Exclude and SpaceHogs masks....";

    show_analyze_update_item_text(item);

    repaint_top_area();
    request_redraw_top_area();
//    repaint_window(dc_);
}

// Callback: for every file during analysis.
// This subroutine is called one last time with Item=nullptr when analysis has finished
void DefragGui::show_analyze(const DefragState &data, const FileNode *item) {
    if (!show_analyze_throttle_timer()) return;

    if (data.count_all_files_ != 0) {
        messages_[3] = std::format(L"Files " NUM_FMT ", Directories " NUM_FMT ", Clusters " NUM_FMT,
                                   data.count_all_files_, data.count_directories_, data.count_all_clusters_);
    }

    show_analyze_update_item_text(item);
    repaint_top_area();
    request_redraw_top_area();
//    repaint_window(dc_);
}

// Callback: show filename in the slot 4, show the message in the debug slot 5 + log the message
void DefragGui::show_debug(const DebugLevel level, const FileNode *item, std::wstring &&text) {
    // Avoid extra data motions below log level
    if (level <= DefragLog::debug_level_) {
        // Save the name of the file in messages[4]
        if (item != nullptr && item->have_long_path()) {
            messages_[4] = item->get_long_path();
        }

        // Save the debug message in Messages 5
        messages_[5] = std::move(text);
    } else {
        messages_[4].clear();
        messages_[5].clear();
    }

    // Pass the message on to the disk log
    Log::log(level, messages_[5].c_str());

    // Avoid repainting below log level
    if (level <= DefragLog::debug_level_) {
        // repaint_window(dc_);
        repaint_top_area();
        request_redraw_top_area();
    }
}

// Callback: paint a cluster on the screen in a given palette color
void DefragGui::draw_cluster(const DefragState &data, const uint64_t cluster_start, const uint64_t cluster_end,
                             const DrawColor color) {
    [[maybe_unused]] Rect window_size = client_size_;

    // Save the PhaseTodo and PhaseDone counters for later use by the progress counter
    if (data.phase_todo_ != 0) {
        progress_time_ = Clock::now();
        progress_done_ = data.clusters_done_;
        progress_todo_ = data.phase_todo_;
    }

#ifndef _WIN64
    // 32-bit drive is too big check
    if (data.total_clusters_ > 0x7FFFFFFF) {
        messages_[3] = L"Drive is too big for the 32-bit version to load";
        paint_image(dc_);
        return;
    }
#endif

    // Sanity check
    if (data.total_clusters_ == 0) return;
    if (dc_ == nullptr) return;
    if (cluster_start == cluster_end) return;

    std::lock_guard<std::mutex> display_lock(display_mutex_);

    if (num_clusters_ != data.total_clusters_ || cluster_info_ == nullptr) {
        num_clusters_ = data.total_clusters_;
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

    request_redraw();
}

// Callback: just before the defragger starts a new Phase, and when it finishes
void DefragGui::show_status(const DefragState &data) {
    // Reset the progress counter
    progress_start_time_ = Clock::now();
    progress_time_ = progress_start_time_;
    progress_done_ = 0;
    progress_todo_ = 0;

    // Reset all the messages
    for (auto &message: messages_) message.clear();

    // Update Message 0 and 1

    messages_[0] = data.disk_.mount_point_;

    switch (data.phase_) {
        case DefragPhase::Analyze:
            messages_[1] = L"Phase 1: Analyze";
            break;
        case DefragPhase::Defragment:
            messages_[1] = L"Phase 2: Defragment";
            break;
        case DefragPhase::ForcedFill:
            messages_[1] = L"Phase 3: Forced Fill";
            break;
        case DefragPhase::ZoneSort:
            messages_[1] = std::format(L"Zone {}: Sort", zone_to_str(data.zone_));
            break;
        case DefragPhase::ZoneFastOpt:
            messages_[1] = std::format(L"Zone {}: Fast Optimize", zone_to_str(data.zone_));
            break;
        case DefragPhase::MoveUp:
            messages_[1] = L"Phase 3: Move Up";
            break;
        case DefragPhase::Done:
            messages_[1] = L"Finished.";
            messages_[4] = std::format(L"Logfile: {}", DefragLog::get_instance()->get_log_filename());
            break;
        case DefragPhase::Fixup:
            messages_[1] = L"Phase 3: Fixup";
            break;
    }

    Log::log(DebugLevel::DetailedProgress, messages_[1].c_str());

    // Write some statistics to the logfile
    if (data.phase_ == DefragPhase::Done) {
        write_stats(data);
    }
}

void DefragGui::on_paint(HDC dc, const PAINTSTRUCT &ps) const {
    Graphics graphics(dc);
    RECT invalidated_rc = ps.rcPaint;
    RectF invalidated_rf(invalidated_rc.left, invalidated_rc.top,
                         invalidated_rc.right - invalidated_rc.left,
                         invalidated_rc.bottom - invalidated_rc.top);

    // graphics.DrawImage(bmp_.get(), 0, 0);
    graphics.DrawImage(bmp_.get(), invalidated_rf, invalidated_rf.X, invalidated_rf.Y, invalidated_rf.Width,
                       invalidated_rf.Height, UnitPixel);
}

// Message handler
LRESULT CALLBACK DefragGui::process_messagefn(HWND wnd, const UINT message, const WPARAM w_param,
                                              const LPARAM l_param) {
    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_TIMER: {
//          InvalidateRect(wnd, nullptr, FALSE);

            std::lock_guard<std::mutex> display_lock(instance_->display_mutex_);
            PAINTSTRUCT ps{};

            instance_->dc_ = BeginPaint(wnd, &ps);
            instance_->full_redraw_window(instance_->dc_);
            EndPaint(wnd, &ps);
            return 0;
        }

        case WM_PAINT: {
            // Grab the display mutex, to make sure that we are the only thread changing the window
            std::lock_guard<std::mutex> display_lock(instance_->display_mutex_);
            PAINTSTRUCT ps{};

            instance_->dc_ = BeginPaint(wnd, &ps);
            instance_->on_paint(instance_->dc_, ps);
            EndPaint(wnd, &ps);

            return 0;
        }

        case WM_ERASEBKGND: {
            InvalidateRect(instance_->wnd_, nullptr, FALSE);
            return 0;
        }

            // case WM_SIZING:
        case WM_SIZE: {
            std::lock_guard<std::mutex> display_lock(instance_->display_mutex_);
            PAINTSTRUCT ps{};

            instance_->dc_ = BeginPaint(wnd, &ps);
            instance_->set_display_data(instance_->dc_);
            EndPaint(wnd, &ps);

            instance_->prepare_cells_for_cluster_range(0, instance_->color_map_.get_total_count());
            instance_->request_redraw();

            return 0;
        }

        default: {
        }
    }

    return DefWindowProc(wnd, message, w_param, l_param);
}

// Show a map on the screen of all the clusters on disk. The map shows which clusters are free and which are in use.
void DefragGui::show_diskmap(DefragState &data) {
    Log::log(DebugLevel::DetailedProgress, L"Diskmap repainting started…");

    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[65536]; // Most efficient if binary multiple
    } bitmap_data{};

    // Exit if the library is not processing a disk yet.
    if (data.disk_.volume_handle_ == nullptr) {
        return;
    }

    // Clear screen
    clear_screen({});

    // Show the map of all the clusters in use
    uint64_t lcn = 0;
    uint64_t cluster_start = 0;
    int prev_in_use = 1;

    DWORD error_code;

    do {
        if (*data.running_ != RunningState::RUNNING) break;
        //		if (*data.RedrawScreen != 2) break;
        if (data.disk_.volume_handle_ == INVALID_HANDLE_VALUE) break;

        // Fetch a block of cluster data
        STARTING_LCN_INPUT_BUFFER bitmap_param = {.StartingLcn = {.QuadPart = (LONGLONG) lcn}};

        DWORD w;
        error_code = DeviceIoControl(data.disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                     &bitmap_param, sizeof bitmap_param, &bitmap_data,
                                     sizeof bitmap_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        // Sanity check
        if (lcn >= bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_) break;

        // Analyze the clusterdata. We resume where the previous block left off
        lcn = bitmap_data.starting_lcn_;
        int index = 0;
        uint8_t mask = 1;

        int index_max = sizeof bitmap_data.buffer_;

        if (bitmap_data.bitmap_size_ / 8 < index_max) index_max = (int) (bitmap_data.bitmap_size_ / 8);

        while (index < index_max && *data.running_ == RunningState::RUNNING) {
            auto in_use = bitmap_data.buffer_[index] & mask;

            // If at the beginning of the disk then copy the in_use value as our starting value
            if (lcn == 0) prev_in_use = in_use;

            // At the beginning and end of an Exclude draw the cluster
            if (lcn == data.mft_excludes_[0].start_ || lcn == data.mft_excludes_[0].end_ ||
                lcn == data.mft_excludes_[1].start_ || lcn == data.mft_excludes_[1].end_ ||
                lcn == data.mft_excludes_[2].start_ || lcn == data.mft_excludes_[2].end_) {
                if (lcn == data.mft_excludes_[0].end_ ||
                    lcn == data.mft_excludes_[1].end_ ||
                    lcn == data.mft_excludes_[2].end_) {
                    draw_cluster(data, cluster_start, lcn, DrawColor::Unmovable);
                } else if (prev_in_use == 0) {
                    draw_cluster(data, cluster_start, lcn, DrawColor::Empty);
                } else {
                    draw_cluster(data, cluster_start, lcn, DrawColor::Allocated);
                }

                in_use = 1;
                prev_in_use = 1;
                cluster_start = lcn;
            }

            // Free
            if (prev_in_use == 0 && in_use != 0) {
                draw_cluster(data, cluster_start, lcn, DrawColor::Empty);
                cluster_start = lcn;
            }

            // In use
            if (prev_in_use != 0 && in_use == 0) {
                draw_cluster(data, cluster_start, lcn, DrawColor::Allocated);
                cluster_start = lcn;
            }

            prev_in_use = in_use;

            if (mask == 128) {
                mask = 1;
                index = index + 1;
            } else {
                mask = mask << 1;
            }

            lcn = lcn + 1;
        }
    } while (error_code == ERROR_MORE_DATA && lcn < bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_);

    if (lcn > 0) {
        if (prev_in_use == 0) {
            // Free
            draw_cluster(data, cluster_start, lcn, DrawColor::Empty);
        } else {
            // in use
            draw_cluster(data, cluster_start, lcn, DrawColor::Allocated);
        }
    }

    // Show the MFT zones
    for (auto &mft_exclude: data.mft_excludes_) {
        if (mft_exclude.start_ <= 0) continue;

        draw_cluster(data, mft_exclude.start_, mft_exclude.end_, DrawColor::Mft);
    }

    // Colorize all the files on the screen
    // Note: the "$BadClus" file on NTFS disks maps the entire disk, so we have to ignore it
    for (auto item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (*data.running_ != RunningState::RUNNING) break;
        //		if (*data.RedrawScreen != 2) break;

        if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0))
            continue;

        defrag_lib_->colorize_disk_item(data, item, 0, 0, false);
    }
    Log::log(DebugLevel::DetailedProgress, L"Diskmap painting ended");
}

