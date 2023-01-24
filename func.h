#ifndef _FUNC_H_
#define _FUNC_H_

#include <conio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <regex>
#include <string>

#include <windows.h>

#include "awacorn/awacorn.h"
#include "awacorn/promise.h"
#include "command.h"
#include "audio.h"

/**
 * @brief 异步的 getch。
 *
 * @return AsyncFn<Promise::Promise<int>> 异步的 getch 结果
 */
Awacorn::AsyncFn<Promise::Promise<int>> async_getch() {
    // 返回一个 async 包装，然后我们可以使用 ev->run(async_getch())
    // 这样的语法，非常方便。
    return [](Awacorn::EventLoop *ev) {
        Promise::Promise<int> pm;  // 创建一个 Promise(承诺，回调式)
        ev->create(
            [pm](Awacorn::EventLoop *ev,
                 const Awacorn::Interval *task) -> void {
                if (_kbhit()) {  // 如果检测到输入
                    pm.resolve(
                        _getch());  // 解决这个 Promise（我们可以用 Promise.then
                                    // 来实现回调注册（链式调用））
                    ev->clear(task);  // 清除这个 task
                }
            },
            std::chrono::milliseconds(
                100));  // 创建一个检测输入的定期循环事件(100ms)。
        // 定期检测大概间隔多少？越大的间隔会导致输入延迟越严重，但占用率越低；越小的间隔会让输入延迟更低，但占用率更高。
        return pm;  // this is awacorn rather strange attempt to understand
                    // 打算以后去找个电子厂混混日子
                    // 10ms 怎么样？还是需要更低？ok的
        // 这个蛮简单的，想学也可以学会！(我可以教你)*objection.wav*
    };
}

bool file_exist(const std::string &file) {
    struct stat buffer {};
    return (stat(file.c_str(), &buffer) ==
            0);  // I'm sorry for this strange solution
}

std::string get_current_dir() {
   char buff[FILENAME_MAX]; //create string buffer to hold path
   _getcwd( buff, FILENAME_MAX );
   std::string current_working_dir(buff);
   return current_working_dir;
}

Parser get_cmd() {
    Parser p;
    std::shared_ptr<size_t> penalty = std::make_shared<size_t>(5);
    std::function<bool(std::string)> fileExistence =
                [](const std::string &file) -> bool {
                    struct stat buffer{};
                    return (stat(file.c_str(), &buffer) == 0);
                };

    std::function<std::string(std::string)> truncate_zero =
            [](const std::string& x) -> std::string {
            std::string y(x.rbegin(), x.rend()), tmp, ret;
            for (char & a : y) {
                if (a == '0')
                    a = 'd';
                else
                    break;
            }
            for (char& a : y) {
                if (a != 'd')
                    tmp += a;
            }
            ret.assign(tmp.rbegin(), tmp.rend());
            return ret;
        };

    std::function<bool(std::string)> isNumber =
    [](const std::string &x) -> bool {
        return (std::all_of(x.cbegin(), x.cend(),
                            [](const char c) -> bool { return std::isdigit(c);}) && !x.empty());
    };

    p.set("q", [](const std::string &, UI *ui, Editor *editor) -> bool {
        if (editor->dirty()) {
            ui->render_log(
                ColorText(
                    "Changes unsaved. Still quit? Yes: [Y] No: [N]",
                    "\x1b[32m")
                    .output());
            ui->update();
            switch (_getch()) {
                case 'Y':
                case 'y': {
                    Audio::CloseMidiDevice();
                    ui->clear();
                    ui->update();
                    exit(0);
                    break;
                }
                default: {
                    return true;
                }
            }
        } else {
            ui->clear();
            ui->update();
            exit(0);
        }
    });

    p.set("save", [fileExistence](const std::string &args, UI *ui, Editor *editor) -> bool {
        std::string path;

        path = args;

        if (args.empty()) {
            path = editor->path();
        }

        if (path[0] == '\"' && path[path.length() - 1] == '\"') {
            path = path.substr(1, path.length() - 2);
        }

        std::ofstream f;
        
        if (fileExistence(path) && path != editor->path()) {
            ui->render_log(
                    ColorText("File exists. Replace? Yes: [Y] No: [N]", "\x1b[32;40m")
                            .output());
            ui->update();
            switch (_getch()) {
                case 'y':
                case 'Y': {
                    f.open(path);
                    break;
                }
                case 'n':
                case 'N': {
                    return true;
                }
                default:
                    return true;
            }
        } else {
            f.open(path);
        }

        if (!f.is_open()) {
            ui->render_log(
                    ColorText("E: Error while opening file.", "\x1b[31m")
                            .output());
            ui->update();
            return false;
        }

        f << editor->project.to_string();
        f.close();
        editor->_dirty = false;
        ui->render_log(
                ColorText("File saved to " + path + ".", "\x1b[32;40m")
                        .output());
        ui->update();
         return false;
    });

    p.set("play", [truncate_zero, penalty](const std::string &args, UI *ui, Editor *editor) -> bool {
        size_t end;
        if (args.size() >= 2) {
            if (args.substr(args.size() - 2, args.size()) == "ms") {
                try {
                    *penalty = std::stoi(args.substr(0, args.size() - 2));
                    return true;
                } catch(...) {return true;}
            }
        }
        try {
            end = std::stoi(args);
        } catch (...) {
            // 不是 number 的情况
            end = editor->project.notes.size();
        }
        // 缩小 tmp 的生命周期(尽可能地)
        if (end >= editor->project.notes.size())
            end = editor->project.notes.size() - 1;

        double total = 0;
        for (size_t tmp = editor->count; tmp < end; tmp++) {
            total += editor->project.notes[tmp].Length / 480.0 /
                     editor->project.tempo * 60.0;
        }
        Awacorn::EventLoop ev;  // 创建一个 EventLoop
        const Awacorn::Event *next = nullptr;
        std::function<void(Awacorn::EventLoop *, const Awacorn::Event *)>
            play_fn = [ui, editor, end, &total, &next, &play_fn, penalty, truncate_zero](
                          Awacorn::EventLoop *ev,
                          const Awacorn::Event *task) -> void {
            editor->render(ui);
            ui->render_log(ColorText("Playing " +
                                         std::to_string(editor->count + 1) +
                                         "/" + std::to_string(end + 1) + " (" +
                                         truncate_zero(std::to_string(total)) + "s, " + std::to_string(*penalty) + "ms) Abort: [C]",
                                     "\x1b[32m")
                               .output());
            ui->update();
            if (editor->count < end) {
                std::chrono::nanoseconds tmp = std::chrono::nanoseconds((size_t)(editor->project.notes[editor->count].Length /
                                 480.0 / editor->project.tempo * 60.0 * 1000000000));

                next = ev->create(
                    play_fn,
                    tmp < std::chrono::milliseconds(*penalty) ? tmp : tmp - std::chrono::milliseconds(*penalty /* Penalty here*/ ));
                editor->count++;
            } else {
                ui->render_log(
                    ColorText("Playback finished. Proceed: [ANY]",
                              "\x1b[32m")
                        .output());
                ui->update();
                next = nullptr;
            }
        };
        // shared_ptr 是带引用计数的智能指针。由于多个事件需要获得
        // ev，同时还要保证 ev 的生命周期，故使用 shared_ptr。 我是傻逼，忘了
        // ev.start() 是 blocking 的了
        std::function<Promise::Promise<void>(int)> getch_fn = [&ev, &next,
                                                               &getch_fn](
                                                                  int result) {
            if (result == 'c' || result == 'C' ||
                (!next)) {  // q? Ctrl+C！having lunch misete
                            // cCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC（超大声
                if (next) ev.clear(next);
                return Promise::resolve<void>();
            }
            return ev.run(async_getch()).then<void>(getch_fn);
        };
        next = ev.create(
            play_fn,
            std::chrono::nanoseconds(0));  // 创建第一个歌词的任务，立即执行
        ev.run(async_getch())
            .then<void>(
                getch_fn);

        timeBeginPeriod(1);
        ev.start();
        timeEndPeriod(1);  // 等待播放完成或者中断
        return true;
        // _getch();
    });

    p.set("version", [](const std::string &, UI *ui, Editor *) -> bool {
        ui->render_log(
            ColorText("uPET@okmt_branch ver1a by FurryR & ookamitai, 2023", "")
                .output());
        ui->update();
        return false;
    });

    p.set("help", [](const std::string &, UI *ui, Editor *) -> bool {
        ui->render_log(ColorText("see https://github.com/ookamitai/uPET "
                                 "for more information",
                                 "")
                           .output());
        ui->update();
        return false;
    });

    p.set("find", [](const std::string &cmd, UI *ui, Editor *editor) -> bool {
        // 教我multi params
        std::vector<std::string> args = splitBy(cmd, ' ');
        std::vector<int> res_count;
        int index = 0;
        // args format:
        // for col=1, 4 [/REGEX/]
        // for col=0, 2, 3 [< | <= | == | >= | >] [VALUE]
        switch (editor->column) {
            case 1:
            case 4: {
                if (args.size() != 1) {
                    ui->render_log(
                        ColorText("Usage: find [REGEX]", "\x1b[31m").output());
                    ui->update();
                    return false;
                }

                if (editor->column == 1){
                    for (int start = 0; start < editor->project.notes.size(); start++) {
                        if (std::regex_match(editor->project.notes[start].Lyric, std::regex(args[0]))) {
                           res_count.push_back(start);
                        }
                    }
                }

                if (editor->column == 4){
                    for (int start = 0; start < editor->project.notes.size(); start++) {
                        if (std::regex_match(editor->project.notes[start].Flags, std::regex(args[0]))) {
                           res_count.push_back(start);
                        }
                    }
                }

                if (res_count.empty()) {
                    ui->render_log(
                        ColorText("No suitable matches found.", "\x1b[32m").output());
                    ui->update();
                    return false;
                }
                // GET MATCHED NOTES' ID
                while (1) {
                    editor->count = res_count[index];
                    editor->render(ui);

                    ui->render_log(
                        ColorText("Matches: " + std::to_string(index + 1) + "/" + std::to_string(res_count.size()) +  + " Next: [Z] Prev: [X] Abort: [C] (De)select: [L] (De)Select All: [V]", "\x1b[32m").output());
                    ui->update();
                    
                    switch(_getch()) {
                        case 'z':
                        case 'Z': {
                            if (index < res_count.size() - 1)
                                index++;
                                break;
                        }
                        case 'x':
                        case 'X': {
                            if (index > 0)
                                index--;
                                break;
                        }
                        case 'c':
                        case 'C': {
                            return true;
                        }
                        case 'L':
                        case 'l': {
                            editor->toggle_sel();
                            break;
                        }
                        case 'v':
                        case 'V': {
                            for (auto& s: res_count) {
                                editor->toggle_sel(s);
                            }
                        }
                    }

                }
            
            }

            case 0:
            case 2:
            case 3: {
                int value;
                if (args.size() != 2 || 
                    !(args[0] == "<" || args[0] == "<=" || args[0] == "==" || args[0] == ">=" || args[0] == ">")) {
                    ui->render_log(
                        ColorText("Usage: find [ < | <= | == | >= | > ] [VALUE]", "\x1b[31m").output());
                    ui->update();
                    return false;
                }

                try {
                    value = std::stoi(args[1]);
                } catch (...) {
                    ui->render_log(
                        ColorText("Usage: Value error", "\x1b[31m").output());
                    ui->update();
                    return false;
                }

                switch (editor->column) {
                    case 0: {
                        if (args[0] == "<") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].NoteNum < value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == "<=") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].NoteNum <= value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == "==") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].NoteNum == value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == ">=") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].NoteNum >= value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == ">") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].NoteNum > value) {
                                    res_count.push_back(start);
                                }
                            }
                        }
                           
                    }

                    case 3: {
                        if (args[0] == "<") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Length < value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == "<=") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Length <= value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == "==") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Length == value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == ">=") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Length >= value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == ">") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Length > value) {
                                    res_count.push_back(start);
                                }
                            }
                        }
                           
                    }

                    case 4: {
                        if (args[0] == "<") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Velocity < value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == "<=") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Velocity <= value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == "==") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Velocity == value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == ">=") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Velocity >= value) {
                                    res_count.push_back(start);
                                }
                            }
                        }

                        if (args[0] == ">") {
                            for (int start; start < editor->project.notes.size(); start++) {
                                if (editor->project.notes[start].Length > value) {
                                    res_count.push_back(start);
                                }
                            }
                        }
                           
                    }
                }
                if (res_count.empty()) {
                    ui->render_log(
                        ColorText("No suitable matches found.", "\x1b[32m").output());
                    ui->update();
                    return false;
                }

                while (1) {
                    editor->count = res_count[index];
                    editor->render(ui);

                    ui->render_log(
                        ColorText("Matches: " + std::to_string(index + 1) + "/" + std::to_string(res_count.size()) +  + " Next: [Z] Prev: [X] Abort: [C]", "\x1b[32m").output());
                    ui->update();
                    
                    switch(_getch()) {
                        case 'z':
                        case 'Z': {
                            if (index < res_count.size() - 1)
                                index++;
                                break;
                        }
                        case 'x':
                        case 'X': {
                            if (index > 0)
                                index--;
                                break;
                        }
                        case 'c':
                        case 'C': {
                            return true;
                        }
                    }

                }

                
            }
        }
        return true;
    });

    p.set("load", [](const std::string &args, UI *ui, Editor *editor) -> bool {
        std::string path = args;
        std::vector<std::string> list;
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir (get_current_dir().c_str())) != NULL) {
            while ((ent = readdir (dir)) != NULL) {
                if (file_exist(std::string(ent->d_name)) && std::string(ent->d_name) != std::string(".") && std::string(ent->d_name) != std::string("..")) {
                    list.emplace_back(ent->d_name);
                }
            }
            closedir (dir);
        }
        if (args.empty()) {
            // ui calling
            SelectUI selui(ui->sub_ui<SelectUI>());
            long long int x = selui.render_choice(ColorText("In function load: ", ""), 
								ColorText("As no parameter is passed in, file select UI is automatically opened.", ""), 
								list, ColorText("Please specify a file name. Quit: [ESC]", ""));
            if (x == INT32_MIN) {
                return true;
            }

            path = list[x];
        }

        if (editor->dirty()) {
            ui->render_log(
                ColorText(
                    "Changes unsaved. Still load? Yes: [Y] No: [N]",
                    "\x1b[32m")
                    .output());
            ui->update();
            switch (_getch()) {
                case 'y':
                case 'Y': {
                    break;
                }
                default: {
                    return true;
                }
            }
        }

        if (path[0] == '\"' && path[path.length() - 1] == '\"') {
            path = path.substr(1, path.length() - 2);
        }
        try {
            if (!editor->open(path)) {
                ui->render_log(
                    ColorText("E: File not found", "\x1b[31m").output());
                ui->update();
                return false;
            }
        } catch (...) {
            ui->render_log(
                ColorText("E: File format error", "\x1b[31m").output());
            ui->update();
            return false;
        }
        return true;
    });

    p.set("selall", [](const std::string &, UI *ui, Editor *editor) -> bool {
        for (auto& nt: editor->project.notes) {
            nt.sel = true;
        }
        return true;
    });

    p.set("desel", [](const std::string &, UI *ui, Editor *editor) -> bool {
        for (auto& nt: editor->project.notes) {
            nt.sel = false;
        }
        return true;
    });

    p.set("revsel", [](const std::string &, UI *ui, Editor *editor) -> bool {
        for (auto& nt: editor->project.notes) {
            nt.sel = !nt.sel;
        }
        return true;
    });

    p.set("close", [](const std::string &, UI *ui, Editor *editor) -> bool {
        if (editor->dirty()) {
            ui->render_log(ColorText("Changes unsaved. Still close? Yes: [Y] No: [N]",
                                     "\x1b[32m")
                               .output());
            ui->update();
            switch (_getch()) {
            case 'y':
            case 'Y': {
                editor->project.notes.clear();
                editor->load(Project());
                break;
            }
            } 
        } else {
            editor->project.notes.clear();
            editor->load(Project());
        }

        return true;
    });

    p.set("delsel", [](const std::string &, UI *, Editor *editor) -> bool {
        editor->_dirty = true;
        // y，同时在sel里面删除一次减1 O(n^2)但是我真的很想delete selection
        // 也就是说我可以随便改本家？ ？是的
        // y
        std::vector<Note> tmp;
        tmp.reserve(editor->project.notes.size());
        for (auto&& element : editor->project.notes) {
            //   ^ ??????????????????
            // &&? auto&& 和 T&& 均属于万能引用，可以接受任意类型的引用，包括左右值和const 左值。不会导致任何拷贝。
            if (!element.sel)
                tmp.push_back(std::move(element)); // move
        }
        editor->project.notes = tmp; // 注意：vector.insert vector.erase 都是 O(N) 操作，很慢。// test flight
        return true;
    });

    p.set("sel", [](const std::string &cmd, UI *ui, Editor *editor) -> bool {
        auto args = splitBy(cmd, ' ');
        if (args.size() == 1) {
            int value1 = 0;
            try {
                value1 = std::stoi(args[0]);
            } catch (...) {
                ui->render_log(ColorText("E: Value error",
                                     "\x1b[31m")
                               .output());
                ui->update();
                return false;
            }

            if (value1 > editor->project.notes.size())
                value1 = editor->project.notes.size();
            if (value1 < 1) value1 = 1;

            if (value1 > editor->count) {
                for (; editor->count < value1; editor->count++) {
                    editor->project.notes[editor->count].sel = true;
                }
            } else {
                for (; editor->count > value1; editor->count--) {
                    editor->project.notes[editor->count].sel = true;
                }
            }

            return true;
        } // splitBy avoid as possible

        if (args.size() == 2) {
            int value1 = 0;
            int value2 = 0;

            try {
                value1 = std::stoi(args[0]);
                value2 = std::stoi(args[1]);
            } catch (...) {
                ui->render_log(ColorText("E: Value error",
                                     "\x1b[31m")
                               .output());
                ui->update();
                return false;
            }

            if (value1 > editor->project.notes.size())
                value1 = editor->project.notes.size();
            if (value1 < 1) value1 = 1;
            if (value2 > editor->project.notes.size())
                value2 = editor->project.notes.size();
            if (value2 < 1) value2 = 1;


            if (value1 > value2) {
                for (editor->count = value2; editor->count < value1; editor->count++) {
                    editor->project.notes[editor->count].sel = true;
                }
            } else {
                for (editor->count = value1; editor->count < value2; editor->count++) {
                    editor->project.notes[editor->count].sel = true;
                }
            }

            return true;
        }
        ui->render_log(ColorText("Usage: sel [BEGIN] [END] | [END]",
                                "\x1b[31m")
                        .output());
        ui->update();
        return false;
    });

    p.set("theme", [](const std::string &args, UI *ui, Editor *editor) -> bool {
        auto color_set = splitBy(args, ' ');
        if (color_set.size() != 3) {
            ui->render_log(ColorText("Usage: theme [R] [G] [B]",
                                     "\x1b[31m")
                               .output());
            ui->update();
            return false;
        }
        for (auto& element: color_set) {
            try {
                std::stoi(element);
            } catch (...) {
                ui->render_log(ColorText("E: Value error",
                                         "\x1b[31m")
                                   .output());
                ui->update();
                return false;
            }
        }
        editor->set_color(std::stoi(color_set[0]), std::stoi(color_set[1]), std::stoi(color_set[2]));
        return true;
    });

    p.set("playmidi", [isNumber](const std::string &args, UI *ui, Editor *editor) -> bool {

        if (!editor->_midi_ok) {
            ui->render_log(ColorText("E: Midi device not configured", "\x1b[31m").output());
            ui->update();
            return false;
        }
        
        int end = editor->project.notes.size() - 1;

        if(isNumber(args)) {
            end = std::stoi(args);
        }
        
        Awacorn::EventLoop ev;
        const Awacorn::Event* next = nullptr;
        Promise::Promise<void> next_pm;
        std::function<void()> play_fn = [ui, editor, end, &next, &play_fn, &next_pm, &ev]() -> void {
            ui->clear();
            editor->render(ui);
            std::stringstream _begin;
            _begin.str("");
            unsigned int begin_int, end_int;
            _begin << std::hex << editor->project.notes[editor->count].NoteNum;  // int decimal_value
        
            ui->render_log(
                    ColorText("Sent midi signal: 0x0070" + _begin.str() + (Audio::channel0 ? "90" : "91") + " " +
                                std::to_string(editor->count + 1) + "/" + std::to_string(end + 1) + " Abort: [C]", "\x1b[32m")
                        .output());
                ui->update();
            if (editor->count < end) {
                std::chrono::nanoseconds tmp = std::chrono::nanoseconds(
                        (size_t)(editor->project.notes[editor->count].Length /
                                 480.0 / editor->project.tempo * 60.0 *
                                 1000000000));
                std::pair<const Awacorn::Event *, Promise::Promise<void>>
                        ret = ev.run(Audio::play_note(
                            editor->project.notes[editor->count].Lyric,
                            editor->project.notes[editor->count].NoteNum,
                            tmp < std::chrono::milliseconds(1)
                                ? tmp
                                : tmp - std::chrono::milliseconds(1)
                        ));
                next = ret.first;
                Promise::Promise<void> pm;
                next_pm = ret.second.then<void>(play_fn);
                editor->count++;
            } else {
                if (editor->count > 0) editor->count--;
                ui->render_log(
                        ColorText(
                            "Playback finished. Press any key to continue...",
                            "\x1b[32m")
                            .output());
                ui->update();
                next = nullptr;
            }
        };
        std::function<Promise::Promise<void>(int)> getch_fn = 
        [&ev, &next, &getch_fn, &next_pm, &editor](int result) {
            if (result == 'c' || result == 'C' || (!next)) {
                Audio::MidiPanic();
                if (next) ev.clear(next);
                next_pm.reject(std::exception());
                return Promise::resolve<void>();
            }
            return ev.run(async_getch()).then<void>(getch_fn);
        };
        next = ev.create([play_fn](Awacorn::EventLoop *, const Awacorn::Event*) -> void {
            play_fn();
        }, std::chrono::nanoseconds(0));
        ev.run(async_getch()).then<void>(getch_fn);
        timeBeginPeriod(1);
        ev.start();
        timeEndPeriod(1);
        return true;
    });
        
    p.set("set", [isNumber](const std::string &args, UI *ui, Editor *editor) -> bool {

        switch (editor->column) {
            case 0: {
                for (auto& nt: editor->project.notes) {
                    if (nt.sel) {
                        if (isNumber(args))
                            nt.NoteNum = std::stoi(args);
                        else 
                            try {
                                nt.NoteNum = get_note_num(args);
                            } catch (...) {
                                ui->render_log(ColorText("E: Value error",
                                         "\x1b[31m")
                                   .output());
                                ui->update();
                                return false;
                            }
                    }
                    
                }
                break;
            }

            case 1: {
                for (auto& nt: editor->project.notes) {
                    if (nt.sel) {
                    nt.Lyric = args;
                }
                }
                break;
            }

            case 2: {
                for (auto& nt: editor->project.notes) {
                    if (nt.sel) {
                        if (isNumber(args))
                            nt.Length = std::stoi(args);
                        else {
                            ui->render_log(ColorText("E: Value error",
                                         "\x1b[31m")
                                   .output());
                            ui->update();
                            return false;
                        }
                    }
                }
                break;
            }

            case 3: {
                for (auto& nt: editor->project.notes) {
                    if (nt.sel) {
                        if (isNumber(args))
                            nt.Velocity = std::stoi(args);
                        else {
                            ui->render_log(ColorText("E: Value error",
                                         "\x1b[31m")
                                   .output());
                            ui->update();
                            return false;
                        }
                    }
                }
                break;
            }

            case 4: {
                for (auto& nt: editor->project.notes) {
                    if (nt.sel) {
                        nt.Flags = args;
                }
                }
                break;
            }
        }
        editor->render(ui);
        return true;
    });

    p.set_default([](const std::string &, UI *ui, Editor *) -> bool {
        ui->render_log(ColorText("E: Command not found", "\x1b[31m").output());
        ui->update();
        return false;
    });

    return p;
}

#endif  //_FUNC_H_
