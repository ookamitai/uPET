#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <windows.h>
#include <string>
#include <array>
#include <thread>
#include <vector>
#include <iostream>
#include <windows.h>
#include "ui.h"
#include "awacorn/awacorn.h"
#include "awacorn/promise.h"

namespace Audio {

	HMIDIOUT outHandle;
	std::thread midi_thread;
	bool channel0 = true;
	int device = 0;
	std::string t;
	

	std::array<size_t, 96> const frequencyTable = {{
		33,   35,   37,   39,   41,   44,   46,   49,   52,   55,   58,   62 ,
		65,   69,   73,   78,   82,   87,   92,   98,  104,  110,  117,  123 ,
		131,  139,  147,  156,  165,  175,  185,  196,  208,  220,  233,  247 ,
		262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494 ,
		524,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988 ,
		1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976 ,
		2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951 ,
		4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902 ,
	}};

	void PlayBeep(size_t freq, size_t duration_ms) {
		std::thread bp_thread(Beep, freq, duration_ms);
		bp_thread.detach();
	}

	int InitMidiDevice() {
		if (device != -2) {
			return !midiOutOpen(&outHandle, (UINT)device, 0, 0, CALLBACK_WINDOW);
		}

		return 0;
	}

	void CloseMidiDevice() {
		midiOutClose(outHandle);
	}


	void MidiDeviceHelper(std::string lyric, size_t note_num, size_t duration_ns) {
		if (lyric == "R" || lyric == " " || lyric == ""){
			std::this_thread::sleep_for(std::chrono::nanoseconds(duration_ns));
			return;
		}
		std::stringstream _begin;
		unsigned int begin_int, end_int;
		_begin << std::hex << note_num; // int decimal_value
		try {
		begin_int = std::stoul("0x0070" + _begin.str() + (channel0 ? "90" : "91"), nullptr, 16);
		end_int = std::stoul("0x0000" + _begin.str() + (channel0 ? "90" : "91"), nullptr, 16);
		} catch(...) {}
		midiOutShortMsg(outHandle, begin_int);
		std::this_thread::sleep_for(std::chrono::nanoseconds(duration_ns));
		midiOutShortMsg(outHandle, end_int);
		channel0 = !channel0;
	}

	void PlayMidi(std::string lyric, size_t note_num, size_t length, size_t tempo) {
		MidiDeviceHelper(lyric, note_num,
		(size_t)(length / 480.0 / tempo * 60.0 * 1000000000));
	}

	void ThreadedPlayMidi(std::string lyric, size_t note_num, size_t length, size_t tempo) {
		midi_thread = std::thread(MidiDeviceHelper, lyric, note_num,
		(size_t)(length / 480.0 / tempo * 60.0 * 1000000000));
		midi_thread.detach();
	}

	
	template <typename Rep, typename Period>
    Awacorn::AsyncFn<std::pair<const Awacorn::Event*, Promise::Promise<void>>> play_note(const std::string& lyric, size_t note_num, const std::chrono::duration<Rep, Period>& tm) {
        return [lyric, note_num, tm](Awacorn::EventLoop* ev) {
            Promise::Promise<void> pm;
            const Awacorn::Event* task;
            if (lyric == "R" || lyric == " " || lyric == "") {
                task = ev->create([pm](Awacorn::EventLoop*, const Awacorn::Event*) -> void {
                    pm.resolve();
                }, tm);
            } else {
				std::stringstream _begin;
				_begin.str("");
                unsigned int begin_int, end_int;
                _begin << std::hex << note_num;  // int decimal_value
				try {
                begin_int = std::stoul("0x0070" + _begin.str() + (channel0 ? "90" : "91"), nullptr, 16);
				t = "0x0000" + _begin.str() + (channel0 ? "90": "91");
                end_int = std::stoul("0x0000" + _begin.str() + (channel0 ? "90": "91"), nullptr, 16);
				} catch (...) {
					t = "None";
				}
                channel0 = !channel0;
                midiOutShortMsg(outHandle, begin_int);
                task = ev->create([pm, end_int](Awacorn::EventLoop*, const Awacorn::Event*) -> void {
                    // 在指定时间后，发送结束的信号，并且调用回调开始播放下一个音符
                    // 在指定时间之间，一直运行async_getch中的逻辑来获取输入，这样就不使用多线程而完成了多线程的工作 ok
                    midiOutShortMsg(outHandle, end_int);
                    pm.resolve();
                }, tm);
            }
            return std::make_pair(task, pm); // ? 鸡巴
        };
    }

	constexpr size_t ToFreq(size_t note_num) {
		if (note_num < 24 || note_num > 108) return frequencyTable[0];
		return frequencyTable[note_num - 24];
	}

	void MidiPanic() {
		size_t panic;
		try {
			panic = std::stoul(t, nullptr, 16);
		} catch (...) {
			panic = 0x000000FF;
		}	
		midiOutShortMsg(outHandle, panic);
	}

	void SetDeviceWizard(Screen* scr) {
		SelectUI ui(scr);
		MIDIOUTCAPSA moc;
		size_t iNumDevs, selected = 0;
		std::vector<std::string> device_list;

		iNumDevs = midiOutGetNumDevs();
		if (iNumDevs == 1) {
			return;
		}

		device_list.emplace_back("(Default)");
		
		for (size_t i = 0; i < iNumDevs; i++) {
			if (!midiOutGetDevCapsA(i, &moc, sizeof(MIDIOUTCAPSA))) {
				device_list.emplace_back(moc.szPname);
			}
		}

		device_list.emplace_back("(None)");

		while (1) {
			ui.render(ColorText("SetDeviceWizard has found multiple MIDI OUT devices: ", ""), device_list, ColorText("Please specify an output device. Quit: [C | ESC]", ""), selected);
			ui.update();
			int g = _getch();
			if (g == 224) {
				switch (_getch()) {
					case 72: {
						if (selected > 0) {
							selected--;
						} else {
							selected = device_list.size() - 1;
						}
						break;
					}

					case 80: {
						if (selected < device_list.size() - 1) {
							selected++;
						} else {
							selected = 0;
						}
						break;
					}
				}
			} else if (g == '\r') {
				if (selected == 0) {
					device = -1;
				} else if (selected == device_list.size() - 1) {
                    // do not use...
					device = -2;
				} else {
					device = selected - 1;
				} 
				
				break;
			} else if (g == 'c' || g == 'C' || g == '\x1b') {
				exit(0);
			}
		}
	}

}

#endif