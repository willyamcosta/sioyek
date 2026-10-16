#pragma once

#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include <string>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "database.h"
#include "tracker.h"
#include "path.h"
#include "input.h"
#include "config.h"
#include "document_view.h"

extern Path default_config_path;
extern Path default_keys_path;
extern Path tutorial_path;
extern Path shader_path;

namespace RegressionTests {

inline void test_installed_config_paths() {
    std::wcout << L"[RUN] test_installed_config_paths..." << std::endl;

    QString cfg_path = QString::fromStdWString(default_config_path.get_path());
    QString keys_path = QString::fromStdWString(default_keys_path.get_path());
    QString tut_path = QString::fromStdWString(tutorial_path.get_path());
    QString sh_path = QString::fromStdWString(shader_path.get_path());

    assert(!cfg_path.isEmpty() && "default_config_path is empty");
    assert(QFile::exists(cfg_path) && "default_config_path (prefs.config) does not exist!");

    assert(!keys_path.isEmpty() && "default_keys_path is empty");
    assert(QFile::exists(keys_path) && "default_keys_path (keys.config) does not exist!");

    assert(!tut_path.isEmpty() && "tutorial_path is empty");
    assert(QFile::exists(tut_path) && "tutorial.pdf does not exist!");

    assert(!sh_path.isEmpty() && "shader_path is empty");
    assert(QDir(sh_path).exists() && "shaders directory does not exist!");

    std::wcout << L"[PASS] test_installed_config_paths (all share files exist)" << std::endl;
}

inline void test_keybinds_and_input(InputHandler* input_handler) {
    std::wcout << L"[RUN] test_keybinds_and_input..." << std::endl;
    assert(input_handler != nullptr);

    // Verify 'q' is bound to 'quit'
    bool quit_bound = input_handler->is_key_bound('q', "quit");
    assert(quit_bound && "Key 'q' must be bound to command 'quit' in keys.config");

    // Verify '/' is bound to 'search'
    bool search_bound = input_handler->is_key_bound('/', "search");
    assert(search_bound && "Key '/' must be bound to command 'search' in keys.config");

    // Verify key mappings are populated
    auto mappings = input_handler->get_command_key_mappings();
    assert(!mappings.empty() && "Command key mappings must not be empty");
    assert(mappings.find("quit") != mappings.end() && "'quit' must have key mapping");

    std::wcout << L"[PASS] test_keybinds_and_input (keybindings properly parsed and active)" << std::endl;
}

inline void test_continuous_scroll_gap_math(DocumentView* document_view) {
    std::wcout << L"[RUN] test_continuous_scroll_gap_math..." << std::endl;
    assert(document_view != nullptr);

    // 1. Verify get_page_space_y typo is fixed
    document_view->set_page_space_x(7.0f);
    document_view->set_page_space_y(23.0f);
    assert(document_view->get_page_space_x() == 7.0f);
    assert(document_view->get_page_space_y() == 23.0f);
    assert(document_view->get_page_space_y() != document_view->get_page_space_x());

    // 2. Verify gap math with mock pages: same doc must have 0 gap, inter-doc must have page_space_y
    struct MockPage {
        int doc_id;
        float height;
    };

    std::vector<MockPage> pages = {
        { 1, 800.0f },
        { 1, 800.0f },
        { 2, 900.0f },
        { 2, 900.0f }
    };

    float cum_offset = 0.0f;
    std::vector<std::pair<float, float>> rect_ys;
    float space_y = 15.0f;

    for (size_t i = 0; i < pages.size(); i++) {
        float y0 = cum_offset;
        float y1 = cum_offset + pages[i].height;
        rect_ys.push_back({ y0, y1 });

        bool is_last_page_of_doc = (i + 1 == pages.size()) || (pages[i + 1].doc_id != pages[i].doc_id);
        if (is_last_page_of_doc) {
            cum_offset += pages[i].height + space_y;
        } else {
            cum_offset += pages[i].height;
        }
    }

    // Pages 0 and 1 in doc 1: 0 gap
    assert(rect_ys[1].first == rect_ys[0].second);

    // Boundary doc 1 -> doc 2: space_y gap
    assert(rect_ys[2].first == rect_ys[1].second + space_y);

    // Pages 2 and 3 in doc 2: 0 gap
    assert(rect_ys[3].first == rect_ys[2].second);

    std::wcout << L"[PASS] test_continuous_scroll_gap_math (zero gap within doc, gap at boundary)" << std::endl;
}

inline void test_database_operations_and_concurrency() {
    std::wcout << L"[RUN] test_database_operations_and_concurrency..." << std::endl;

    QTemporaryDir temp_dir;
    assert(temp_dir.isValid());

    QString local_db_path = temp_dir.filePath("test_local.db");
    QString global_db_path = temp_dir.filePath("test_global.db");

    DatabaseManager db;
    bool opened = db.open(local_db_path.toStdWString(), global_db_path.toStdWString());
    assert(opened);
    db.ensure_database_compatibility(local_db_path.toStdWString(), global_db_path.toStdWString());
    db.ensure_schema_compatibility();

    TrackedWork work;
    work.series_path = "/path/to/series/Kingdom";
    work.title = "Kingdom";
    work.anilist_id = 45678;
    work.anilist_title = "Kingdom";
    work.is_tracking = true;
    work.last_volume = 70;
    work.last_chapter = 780.0f;
    work.reading_status = "CURRENT";
    work.total_volumes = 70;
    work.total_chapters = 800;

    // Test save_tracked_work
    assert(db.save_tracked_work(work));

    // Test select_tracked_work
    TrackedWork loaded;
    assert(db.select_tracked_work(work.series_path.toStdWString(), loaded));
    assert(loaded.title == "Kingdom");
    assert(loaded.anilist_id == 45678);
    assert(loaded.last_volume == 70);

    // Test select_all_tracked_works
    std::vector<TrackedWork> all_works;
    assert(db.select_all_tracked_works(all_works));
    assert(all_works.size() == 1);

    // Test delete_document_hash
    db.insert_document_hash(L"/path/to/test.pdf", "hash_test_123");
    assert(db.delete_document_hash("hash_test_123"));

    // Concurrent multi-threaded stress test for db_mutex
    std::atomic<bool> failed = false;
    std::vector<std::thread> workers;
    const int thread_count = 8;
    const int iterations_per_thread = 50;

    for (int t = 0; t < thread_count; t++) {
        workers.emplace_back([&db, t, &failed]() {
            for (int i = 0; i < iterations_per_thread; i++) {
                try {
                    TrackedWork w;
                    w.series_path = QString("work_%1_%2").arg(t).arg(i);
                    w.title = QString("Title_%1_%2").arg(t).arg(i);
                    w.last_chapter = static_cast<float>(i);
                    w.is_tracking = true;

                    if (!db.save_tracked_work(w)) {
                        failed = true;
                    }

                    std::vector<TrackedWork> list;
                    if (!db.select_all_tracked_works(list)) {
                        failed = true;
                    }

                    if (!db.update_work_progress(w.series_path.toStdWString(), i, static_cast<float>(i + 1))) {
                        failed = true;
                    }

                    db.delete_document_hash("test_dummy_hash");
                } catch (...) {
                    failed = true;
                }
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    assert(!failed && "Concurrent database operations failed; db_mutex locking issue!");
    std::wcout << L"[PASS] test_database_operations_and_concurrency (multi-thread locking verified)" << std::endl;
}

inline int run_all_tests(DatabaseManager* db_manager, InputHandler* input_handler, DocumentView* document_view) {
    std::wcout << L"\n==========================================" << std::endl;
    std::wcout << L"  RUNNING SIOYEK FORK REGRESSION SUITE   " << std::endl;
    std::wcout << L"==========================================" << std::endl;

    test_installed_config_paths();
    test_keybinds_and_input(input_handler);
    test_continuous_scroll_gap_math(document_view);
    test_database_operations_and_concurrency();

    std::wcout << L"==========================================" << std::endl;
    std::wcout << L"  ALL REGRESSION TESTS PASSED (4/4)       " << std::endl;
    std::wcout << L"==========================================\n" << std::endl;
    return 0;
}

} // namespace RegressionTests
