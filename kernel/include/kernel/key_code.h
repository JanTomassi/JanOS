#pragma once

enum key_code {
	KEY_CODE_0, // 0 pressed
	KEY_CODE_1, // 1 pressed
	KEY_CODE_2, // 2 pressed
	KEY_CODE_3, // 3 pressed
	KEY_CODE_4, // 4 pressed
	KEY_CODE_5, // 5 pressed
	KEY_CODE_6, // 6 pressed
	KEY_CODE_7, // 7 pressed
	KEY_CODE_8, // 8 pressed
	KEY_CODE_9, // 9 pressed
	KEY_CODE_A, // A pressed
	KEY_CODE_B, // B pressed
	KEY_CODE_C, // C pressed
	KEY_CODE_D, // D pressed
	KEY_CODE_E, // E pressed
	KEY_CODE_F, // F pressed
	KEY_CODE_G, // G pressed
	KEY_CODE_H, // H pressed
	KEY_CODE_I, // I pressed
	KEY_CODE_J, // J pressed
	KEY_CODE_K, // K pressed
	KEY_CODE_L, // L pressed
	KEY_CODE_M, // M pressed
	KEY_CODE_N, // N pressed
	KEY_CODE_O, // O pressed
	KEY_CODE_P, // P pressed
	KEY_CODE_Q, // Q pressed
	KEY_CODE_R, // R pressed
	KEY_CODE_S, // S pressed
	KEY_CODE_T, // T pressed
	KEY_CODE_U, // U pressed
	KEY_CODE_V, // V pressed
	KEY_CODE_W, // W pressed
	KEY_CODE_X, // X pressed
	KEY_CODE_Y, // Y pressed
	KEY_CODE_Z, // Z pressed

	KEY_CODE_BACKTICK, // ` (back tick) pressed
	KEY_CODE_MINUS, // - pressed
	KEY_CODE_EQ, // = pressed
	KEY_CODE_LSBRACKET, // [ pressed
	KEY_CODE_RSBRACKET, // ] pressed
	KEY_CODE_BACKSLASH, // \ pressed
	KEY_CODE_SEMICOLON, // ; pressed
	KEY_CODE_SQUOTE, // ' (single quote) pressed
	KEY_CODE_COMMA, // , pressed
	KEY_CODE_POINT, // . pressed
	KEY_CODE_SLASH, // / pressed

	KEY_CODE_LAST_PRINTABLE, // marker for the last printable char

	KEY_CODE_SPACE, // space pressed
	KEY_CODE_TAB, // tab pressed

	KEY_CODE_ENTER, // enter pressed
	KEY_CODE_BACKSPACE, // backspace pressed
	KEY_CODE_CAPSLOCK, // CapsLock pressed

	KEY_CODE_ESC, // ESC pressed

	KEY_CODE_HOME, // home pressed
	KEY_CODE_PAGE_UP, // page up pressed
	KEY_CODE_PAGE_DOWN, // page down pressed
	KEY_CODE_END, // end pressed

	KEY_CODE_INSERT, // insert pressed
	KEY_CODE_DELETE, // delete pressed

	KEY_CODE_ARROW_UP, // cursor up pressed
	KEY_CODE_ARROW_DOWN, // cursor down pressed
	KEY_CODE_ARROW_LEFT, // cursor left pressed
	KEY_CODE_ARROW_RIGHT, // cursor right pressed

	KEY_CODE_LALT, // left alt pressed
	KEY_CODE_RALT, // right alt (or altGr) pressed
	KEY_CODE_LCTRL, // left control presse
	KEY_CODE_RCTRL, // right control pressed
	KEY_CODE_LGUI, // left GUI pressed
	KEY_CODE_RGUI, // right GUI pressed
	KEY_CODE_LSHIFT, // left shift pressed
	KEY_CODE_RSHIFT, // right shift pressed

	KEY_CODE_F1, // F1 pressed
	KEY_CODE_F2, // F2 pressed
	KEY_CODE_F3, // F3 pressed
	KEY_CODE_F4, // F4 pressed
	KEY_CODE_F5, // F5 pressed
	KEY_CODE_F6, // F6 pressed
	KEY_CODE_F7, // F7 pressed
	KEY_CODE_F8, // F8 pressed
	KEY_CODE_F9, // F9 pressed
	KEY_CODE_F10, // F10 pressed
	KEY_CODE_F11, // F11 pressed
	KEY_CODE_F12, // F12 pressed

	KEY_CODE_NUMBERLOCK, // NUMBERLOCK PRESSED
	KEY_CODE_KEYPAD_0, // (keypad) 0 pressed
	KEY_CODE_KEYPAD_1, // (keypad) 1 pressed
	KEY_CODE_KEYPAD_2, // (keypad) 2 pressed
	KEY_CODE_KEYPAD_3, // (keypad) 3 pressed
	KEY_CODE_KEYPAD_4, // (keypad) 4 pressed
	KEY_CODE_KEYPAD_5, // (keypad) 5 pressed
	KEY_CODE_KEYPAD_6, // (keypad) 6 pressed
	KEY_CODE_KEYPAD_7, // (keypad) 7 pressed
	KEY_CODE_KEYPAD_8, // (keypad) 8 pressed
	KEY_CODE_KEYPAD_9, // (keypad) 9 pressed
	KEY_CODE_KEYPAD_ENTER, // (keypad) enter pressed
	KEY_CODE_KEYPAD_MINUS, // (keypad) - pressed
	KEY_CODE_KEYPAD_PLUS, // (keypad) + pressed
	KEY_CODE_KEYPAD_POINT, // (keypad) . pressed
	KEY_CODE_KEYPAD_SLASH, // (keypad) / pressed
	KEY_CODE_KEYPAD_STAR, // (keypad) * pressed

	KEY_CODE_PAUSE, // paused pressed
	KEY_CODE_PRINTSCREEN, // screen pressed
	KEY_CODE_SCROLLLOCK, // ScrollLock pressed

	KEY_CODE_ACPI_POWER, // (ACPI) power pressed
	KEY_CODE_ACPI_SLEEP, // (ACPI) sleep pressed
	KEY_CODE_ACPI_WAKE, // (ACPI) wake pressed
	KEY_CODE_APPS, // "apps" pressed

	KEY_CODE_MULTIMEDIA_CALCULATOR, // (multimedia) calculator pressed
	KEY_CODE_MULTIMEDIA_EMAIL, // (multimedia) email pressed
	KEY_CODE_MULTIMEDIA_MEDIASELECT, // (multimedia) media select pressed
	KEY_CODE_MULTIMEDIA_MUTE, // (multimedia) mute pressed
	KEY_CODE_MULTIMEDIA_MYCOMPUTER, // (multimedia) my computer pressed
	KEY_CODE_MULTIMEDIA_NEXTTRACK, // (multimedia) next track pressed
	KEY_CODE_MULTIMEDIA_PLAY, // (multimedia) play pressed
	KEY_CODE_MULTIMEDIA_PREVTRACK, // (multimedia) previous track pressed
	KEY_CODE_MULTIMEDIA_STOP, // (multimedia) stop pressed
	KEY_CODE_MULTIMEDIA_VOL_DOWN, // (multimedia) volume down pressed
	KEY_CODE_MULTIMEDIA_VOL_UP, // (multimedia) volume up pressed
	KEY_CODE_MULTIMEDIA_WWW_BACK, // (multimedia) WWW back pressed
	KEY_CODE_MULTIMEDIA_WWW_FAVORITES, // (multimedia) WWW favorites pressed
	KEY_CODE_MULTIMEDIA_WWW_FORWARD, // (multimedia) WWW forward pressed
	KEY_CODE_MULTIMEDIA_WWW_HOME, // (multimedia) WWW home pressed
	KEY_CODE_MULTIMEDIA_WWW_REFRESH, // (multimedia) WWW refresh pressed
	KEY_CODE_MULTIMEDIA_WWW_SEARCH, // (multimedia) WWW search pressed
	KEY_CODE_MULTIMEDIA_WWW_STOP, // (multimedia) WWW stop pressed

	KEY_CODE_NONE,
};

const char *KEY_CODE_str[] = {
	"", // KEY_CODE_ESC
	"1", // KEY_CODE_1
	"2", // KEY_CODE_2
	"3", // KEY_CODE_3
	"4", // KEY_CODE_4
	"5", // KEY_CODE_5
	"6", // KEY_CODE_6
	"7", // KEY_CODE_7
	"8", // KEY_CODE_8
	"9", // KEY_CODE_9
	"0", // KEY_CODE_0
	"-", // KEY_CODE_MINUS
	"=", // KEY_CODE_EQ
	"", // KEY_CODE_BACKSPACE
	"\t", // KEY_CODE_TAB
	"Q", // KEY_CODE_Q
	"W", // KEY_CODE_W
	"E", // KEY_CODE_E
	"R", // KEY_CODE_R
	"T", // KEY_CODE_T
	"Y", // KEY_CODE_Y
	"U", // KEY_CODE_U
	"I", // KEY_CODE_I
	"O", // KEY_CODE_O
	"P", // KEY_CODE_P
	"[", // KEY_CODE_LSBRACKET
	"]", // KEY_CODE_RSBRACKET
	"\n", // KEY_CODE_ENTER
	"", // KEY_CODE_LCTRL
	"A", // KEY_CODE_A
	"S", // KEY_CODE_S
	"D", // KEY_CODE_D
	"F", // KEY_CODE_F
	"G", // KEY_CODE_G
	"H", // KEY_CODE_H
	"J", // KEY_CODE_J
	"K", // KEY_CODE_K
	"L", // KEY_CODE_L
	";", // KEY_CODE_SEMICOLON
	"'", // KEY_CODE_SQUOTE
	"`", // KEY_CODE_BACKTICK
	"", // KEY_CODE_LSHIFT
	"\\", // KEY_CODE_BACKSLASH
	"Z", // KEY_CODE_Z
	"X", // KEY_CODE_X
	"C", // KEY_CODE_C
	"V", // KEY_CODE_V
	"B", // KEY_CODE_B
	"N", // KEY_CODE_N
	"M", // KEY_CODE_M
	",", // KEY_CODE_COMMA
	".", // KEY_CODE_POINT
	"/", // KEY_CODE_SLASH
	"", // KEY_CODE_RSHIFT
	"*", // KEY_CODE_KEYPAD_STAR
	"", // KEY_CODE_LALT
	" ", // KEY_CODE_SPACE
	"", // KEY_CODE_CAPSLOCK
	"F1", // KEY_CODE_F1
	"F2", // KEY_CODE_F2
	"F3", // KEY_CODE_F3
	"F4", // KEY_CODE_F4
	"F5", // KEY_CODE_F5
	"F6", // KEY_CODE_F6
	"F7", // KEY_CODE_F7
	"F8", // KEY_CODE_F8
	"F9", // KEY_CODE_F9
	"F10", // KEY_CODE_F10
	"", // KEY_CODE_NUMBERLOCK
	"", // KEY_CODE_SCROLLLOCK
	"KEYPAD_7", // KEY_CODE_KEYPAD_7
	"KEYPAD_8", // KEY_CODE_KEYPAD_8
	"KEYPAD_9", // KEY_CODE_KEYPAD_9
	"KEYPAD_MINUS", // KEY_CODE_KEYPAD_MINUS
	"KEYPAD_4", // KEY_CODE_KEYPAD_4
	"KEYPAD_5", // KEY_CODE_KEYPAD_5
	"KEYPAD_6", // KEY_CODE_KEYPAD_6
	"KEYPAD_PLUS", // KEY_CODE_KEYPAD_PLUS
	"KEYPAD_1", // KEY_CODE_KEYPAD_1
	"KEYPAD_2", // KEY_CODE_KEYPAD_2
	"KEYPAD_3", // KEY_CODE_KEYPAD_3
	"KEYPAD_0", // KEY_CODE_KEYPAD_0
	"KEYPAD_POINT", // KEY_CODE_KEYPAD_POINT
	"F11", // KEY_CODE_F11
	"F12" // KEY_CODE_F12
};
