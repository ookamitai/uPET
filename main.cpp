#include <windows.h>
#include <iostream>

#include "editor.h"
#include "audio.h"
#include "func.h"
#include "parser.h"

void main_ui(Screen *screen, const std::string& default_path) {
    UI ui(screen);
    Editor editor;
    Parser parser = get_cmd();
    UINT cp = GetConsoleOutputCP();
    if (!Audio::InitMidiDevice()) {
        editor._midi_ok = false;
        ui.render_log(ColorText("E: Unable to init midi device", "\x1b[31m").output());
        ui.update();
        _getch();
    } else {
        editor._midi_ok = true;
    }
    // 开头提示
    bool cp_flag = false;
    // 是否刷新屏幕的 flag
    bool refresh = true;
    if (cp != 932) {
        system("chcp 932>nul");
        SetConsoleOutputCP(932);
        // logger.log_info(
        //    "Your code page is not UTAU-friendly. Switched to 932.\n");
    }

    if (!default_path.empty()) {
        editor._in_utau = true;
        editor.open(default_path);   
    }
    while (true) {
        if (refresh) {
            ui.clear();
            editor.render(&ui);
        }
        else
            refresh = true;
        if ((!cp_flag) && cp != 932) {
            ui.render_log(ColorText("Your code page (" + std::to_string(cp) +
                                        ") is not UTAU-friendly. Switched to "
                                        "932.",
                                    "\x1b[33m")
                              .output());
            cp_flag = true;
        }
        ui.update();
        int g = _getch();
        if (g == ':') {
            // 命令系统
            std::string tmp;
            size_t cursor = tmp.length();
            int key;
            // somewhat looks like a cursor
            std::vector<Character> t = {Character(':')};
            std::vector<Character> cursor_tmp = text_cursor(tmp, cursor);
            t.insert(t.cend(), cursor_tmp.cbegin(), cursor_tmp.cend());
            ui.render_log(t);
            ui.update();
            while ((key = getch())) {
                if (key == '\r') {
                    refresh = parser.execute(tmp, &ui, &editor);
                    break;
                } else if (key == '\t') {
                    break;
                } else if (key == '\x1b') {
                    break;
                } else if (key == '\b') {
                    if (cursor > 0) {
                        tmp = tmp.substr(0, cursor - 1) + tmp.substr(cursor);
                        cursor--;
                    }
                } else if (key == 224) {
                    switch (_getch()) {
                        case 75: {
                            // 左键
                            if (cursor > 0) cursor--;
                            break;
                        }
                        case 77: {
                            if (cursor < tmp.length()) cursor++;
                            break;
                        }
                    }
                } else if (key) {
                    tmp.insert(tmp.cbegin() + cursor, key);
                    cursor++;
                }
                t = {Character(':')};
                cursor_tmp = text_cursor(tmp, cursor);
                t.insert(t.cend(), cursor_tmp.cbegin(), cursor_tmp.cend());
                ui.render_log(t);
                ui.update();
            }
        } else {
            refresh = editor.process(
                &ui,
                g);  // 好会写 没需求就删了，不留任何额外参数。（来自 Lightpad）
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    HANDLE hStd = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hStd, &dwMode);
    dwMode |= 0x4;
    SetConsoleMode(hStd, dwMode);
#endif
    Screen screen(getsize());
    Audio::SetDeviceWizard(&screen);
    system(argc == 2 ? "title uPET Plugin Mode>nul" : "title uPET@okmt_branch>nul");
    main_ui(&screen, argc == 2 ? std::string(argv[1]) : std::string());
    return 0;
}