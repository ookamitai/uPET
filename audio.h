#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <windows.h>
#include <string>
#include <array>
#include <thread>
#include <vector>
#include <iostream>

#include <windows.h>

namespace Audio {

	HMIDIOUT outHandle;
	unsigned long result;
	std::vector<std::thread*> thread_pool;

	const std::array<size_t, 96> frequencyTable = {{
		33,   35,   37,   39,   41,   44,   46,   49,   52,   55,   58,   62 ,
		65,   69,   73,   78,   82,   87,   92,   98,  104,  110,  117,  123 ,
		131,  139,  147,  156,  165,  175,  185,  196,  208,  220,  233,  247 ,
		262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494 ,
		524,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988 ,
		1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976 ,
		2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951 ,
		4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902 ,
	}};

	void play_beep(size_t freq, size_t duration_ms) {
		std::shared_ptr<std::thread> bp_thread_ptr = std::make_shared<std::thread>(Beep, freq, duration_ms);
		bp_thread_ptr->detach();
	}

	int init_midi_device() {
		result = midiOutOpen(&outHandle, (UINT)-1, 0, 0, CALLBACK_WINDOW);
		
		if (result) {
			return -1;
		}
		return 0;
	}

	void close_midi_device() {
		midiOutClose(outHandle);
	}


	void midi_device_helper(std::string lyric, size_t note_num, size_t duration_ns) {
		if (lyric == "R" || lyric == " "){
			std::this_thread::sleep_for(std::chrono::nanoseconds(duration_ns));
			return;
		}
		std::stringstream _begin;
		unsigned int begin_int, end_int;
		_begin << std::hex << note_num; // int decimal_value
		begin_int = std::stoul("0x0064" + _begin.str() + "90", nullptr, 16);
		end_int = std::stoul("0x0000" + _begin.str() + "90", nullptr, 16);
		midiOutShortMsg(outHandle, begin_int);
		std::this_thread::sleep_for(std::chrono::nanoseconds(duration_ns));
		midiOutShortMsg(outHandle, end_int);
	}

	void play_midi_device(std::string lyric, size_t note_num, size_t length, size_t tempo, bool is_ex = false) {
		std::shared_ptr<std::thread> midi_thread_ptr = std::make_shared<std::thread>(midi_device_helper, lyric, note_num,
		(size_t)(length / 480.0 / tempo * 60.0 * 1000000000));
		if (is_ex)
			midi_thread_ptr->join();
		else
			midi_thread_ptr->detach();
	}
	

	size_t to_freq(size_t note_num) {
		if (note_num < 24 || note_num > 108) return frequencyTable[0];
		return frequencyTable[note_num - 24];
	}

}

#endif