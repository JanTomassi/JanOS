#include <stddef.h>
#include <stdint.h>

#include <kernel/key_code.h>
#include <kernel/interrupt.h>
#include <kernel/spinlock.h>
#include <kernel/tty.h>
#include <kernel/display.h>
#include <kernel/syscall.h>
#include <kernel/scheduler.h>
#include <kernel/process/wait_queue.h>
#include <kernel/ipc.h>

#include <janos/input.h>

#include <arch/i386/irq.h>
#include <arch/i386/ps2.h>

struct active_mod_key {
	bool caplock : 1;
	bool left_alt : 1;
	bool left_ctrl : 1;
	bool left_shift : 1;
	bool win : 1;
	bool right_alt : 1;
	bool right_ctrl : 1;
	bool right_shift : 1;
} mod_key = {
	.caplock = false,
	.left_alt = false,
	.left_ctrl = false,
	.left_shift = false,
	.win = false,
	.right_alt = false,
	.right_ctrl = false,
	.right_shift = false,
};

struct key_event {
	bool pressed;
	uint8_t scan_code;
	enum key_code key_code;
};

static void update_mod_key(struct key_event event);
static struct key_event scan_code1_to_key_event(uint8_t scan_code);

#define CONSOLE_LINE_SIZE 256
#define CONSOLE_INPUT_SIZE 1024

static spinlock_t console_lock = { 0 };
static char edit_line[CONSOLE_LINE_SIZE];
static size_t edit_length;
static char input_buffer[CONSOLE_INPUT_SIZE];
static size_t input_read;
static size_t input_write;
static struct wait_queue console_readers;
static volatile uint32_t input_endpoint;

static bool input_push(char ch)
{
	size_t next = (input_write + 1) % CONSOLE_INPUT_SIZE;
	if (next == input_read)
		return false;
	input_buffer[input_write] = ch;
	input_write = next;
	return true;
}

static bool input_pop(char *ch)
{
	if (input_read == input_write)
		return false;
	*ch = input_buffer[input_read];
	input_read = (input_read + 1) % CONSOLE_INPUT_SIZE;
	return true;
}

static void console_key_event(struct key_event event)
{
	if (!event.pressed)
		return;

	bool wake_readers = false;
	uint32_t flags = spin_lock_irqsave(&console_lock);
	if (event.key_code == KEY_CODE_BACKSPACE) {
		if (edit_length != 0) {
			edit_line[--edit_length] = '\0';
			kprintf("\b \b");
		}
	} else if (event.key_code == KEY_CODE_ENTER) {
		for (size_t i = 0; i < edit_length; i++)
			input_push(edit_line[i]);
		input_push('\n');
		wake_readers = true;
		edit_length = 0;
		edit_line[0] = '\0';
		kprintf("\n");
	} else if (event.key_code == KEY_CODE_C &&
		   (mod_key.left_ctrl || mod_key.right_ctrl)) {
		input_push(0x03);
		edit_length = 0;
		edit_line[0] = '\0';
		kprintf("^C\n");
	} else if (event.key_code < KEY_CODE_LAST_PRINTABLE ||
		   event.key_code == KEY_CODE_SPACE) {
		if (edit_length < CONSOLE_LINE_SIZE - 1) {
			const char *text = event.key_code == KEY_CODE_SPACE
				? " "
				: keysym[event.key_code][mod_key.left_shift ||
							 mod_key.right_shift || mod_key.caplock];
			if (text[0] != '\0') {
				edit_line[edit_length++] = text[0];
				edit_line[edit_length] = '\0';
				kprintf("%c", text[0]);
			}
		}
	}
	spin_unlock_irqrestore(&console_lock, flags);
	if (wake_readers) {
		struct process *reader;
		while ((reader = wait_queue_pop(&console_readers)) != nullptr)
			scheduler_wake(reader);
	}
}

static void ps2_irq_handler(uint8_t irq_line, void *context)
{
	(void)irq_line;
	(void)context;
	uint8_t scan_code;

	__asm__("mov    $0,    %0;"   /* zeroing scan_code */
		"inb 	$0x60, %%al;" /* reading input from keyboard */
		"mov    %%al,  %0"    /* coping keyboard input to scan_code */
		: "=g"(scan_code)
		:
		: "%al");
	if (input_endpoint != 0) {
		(void)ipc_kernel_notify(input_endpoint, JANOS_INPUT_MSG_RAW, scan_code);
		return;
	}

	struct key_event event = scan_code1_to_key_event(scan_code);
	update_mod_key(event);
	console_key_event(event);
}

void ps2_init(void)
{
	wait_queue_init(&console_readers);
	irq_register_handler(1, ps2_irq_handler, nullptr);
}

void ps2_set_input_endpoint(uint32_t endpoint)
{
	input_endpoint = endpoint;
}

int32_t console_read(char *buffer, size_t length, struct i386_trap_frame *frame)
{
	if (buffer == nullptr || length == 0)
		return 0;

	size_t count = 0;
	for (;;) {
		uint32_t flags = spin_lock_irqsave(&console_lock);
		bool complete = false;
		for (size_t i = input_read; i != input_write; i = (i + 1) % CONSOLE_INPUT_SIZE)
			if (input_buffer[i] == '\n') {
				complete = true;
				break;
			}
		if (complete) {
			while (count < length && input_pop(&buffer[count])) {
				if (buffer[count++] == '\n')
					break;
			}
			spin_unlock(&console_lock);
			local_irq_restore(flags);
			return count;
		}
		spin_unlock(&console_lock);
		local_irq_restore(flags);
		if (scheduler_block_current(&console_readers, frame))
			return -SYSCALL_EIPC_BLOCKED;
	}
}

size_t console_write(const char *buffer, size_t length)
{
	return display_write(buffer, length);
}

static void update_mod_key(struct key_event event)
{
	switch (event.key_code) {
	case KEY_CODE_LALT: // left alt pressed
		mod_key.left_alt = event.pressed;
		break;
	case KEY_CODE_RALT: // right alt (or altGr) pressed
		mod_key.right_alt = event.pressed;
		break;
	case KEY_CODE_LCTRL: // left control presse
		mod_key.left_ctrl = event.pressed;
		break;
	case KEY_CODE_RCTRL: // right control pressed
		mod_key.right_ctrl = event.pressed;
		break;
	case KEY_CODE_LGUI: // left GUI pressed
	case KEY_CODE_RGUI: // right GUI pressed
		mod_key.win = event.pressed;
		break;
	case KEY_CODE_LSHIFT: // left shift pressed
		mod_key.left_shift = event.pressed;
		break;
	case KEY_CODE_RSHIFT: // right shift pressed
		mod_key.right_shift = event.pressed;
		break;
	case KEY_CODE_CAPSLOCK: // CapsLock pressed
		mod_key.caplock = event.pressed;
		break;
	default:
		break;
	}
}

static struct key_event scan_code1_to_key_event(uint8_t scan_code)
{
#define X(KEY_CODE) ((struct key_event){.pressed = true, .scan_code = scan_code, .key_code = (KEY_CODE)})
	switch (scan_code) {
	case 0x01: // escape pressed
            return X(KEY_CODE_ESC);
	case 0x02: // 1 pressed
            return X(KEY_CODE_1);
	case 0x03: // 2 pressed
            return X(KEY_CODE_2);
	case 0x04: // 3 pressed
            return X(KEY_CODE_3);
	case 0x05: // 4 pressed
            return X(KEY_CODE_4);
	case 0x06: // 5 pressed
            return X(KEY_CODE_5);
	case 0x07: // 6 pressed
            return X(KEY_CODE_6);
	case 0x08: // 7 pressed
            return X(KEY_CODE_7);
	case 0x09: // 8 pressed
            return X(KEY_CODE_8);
	case 0x0A: // 9 pressed
            return X(KEY_CODE_9);
	case 0x0B: // 0 (zero) pressed
            return X(KEY_CODE_0);
	case 0x0C: // - pressed
            return X(KEY_CODE_MINUS);
	case 0x0D: // = pressed
            return X(KEY_CODE_EQ);
	case 0x0E: // backspace pressed
            return X(KEY_CODE_BACKSPACE);
	case 0x0F: // tab pressed
            return X(KEY_CODE_TAB);
	case 0x10: // Q pressed
            return X(KEY_CODE_Q);
	case 0x11: // W pressed
            return X(KEY_CODE_W);
	case 0x12: // E pressed
            return X(KEY_CODE_E);
	case 0x13: // R pressed
            return X(KEY_CODE_R);
	case 0x14: // T pressed
            return X(KEY_CODE_T);
	case 0x15: // Y pressed
            return X(KEY_CODE_Y);
	case 0x16: // U pressed
            return X(KEY_CODE_U);
	case 0x17: // I pressed
            return X(KEY_CODE_I);
	case 0x18: // O pressed
            return X(KEY_CODE_O);
	case 0x19: // P pressed
            return X(KEY_CODE_P);
	case 0x1A: // [ pressed
            return X(KEY_CODE_LSBRACKET);
	case 0x1B: // ] pressed
            return X(KEY_CODE_RSBRACKET);
	case 0x1C: // enter pressed
            return X(KEY_CODE_ENTER);
	case 0x1D: // left control pressed
            return X(KEY_CODE_LCTRL);
	case 0x1E: // A pressed
            return X(KEY_CODE_A);
	case 0x1F: // S pressed
            return X(KEY_CODE_S);
	case 0x20: // D pressed
            return X(KEY_CODE_D);
	case 0x21: // F pressed
            return X(KEY_CODE_F);
	case 0x22: // G pressed
            return X(KEY_CODE_G);
	case 0x23: // H pressed
            return X(KEY_CODE_H);
	case 0x24: // J pressed
            return X(KEY_CODE_J);
	case 0x25: // K pressed
            return X(KEY_CODE_K);
	case 0x26: // L pressed
            return X(KEY_CODE_L);
	case 0x27: // ; pressed
            return X(KEY_CODE_SEMICOLON);
	case 0x28: // ' (single quote) pressed
            return X(KEY_CODE_SQUOTE);
	case 0x29: // ` (back tick) pressed
            return X(KEY_CODE_BACKTICK);
	case 0x2A: // left shift pressed
            return X(KEY_CODE_LSHIFT);
	case 0x2B: // \ pressed
            return X(KEY_CODE_BACKSLASH);
	case 0x2C: // Z pressed
            return X(KEY_CODE_Z);
	case 0x2D: // X pressed
            return X(KEY_CODE_X);
	case 0x2E: // C pressed
            return X(KEY_CODE_C);
	case 0x2F: // V pressed
            return X(KEY_CODE_V);
	case 0x30: // B pressed
            return X(KEY_CODE_B);
	case 0x31: // N pressed
            return X(KEY_CODE_N);
	case 0x32: // M pressed
            return X(KEY_CODE_M);
	case 0x33: // , pressed
            return X(KEY_CODE_COMMA);
	case 0x34: // . pressed
            return X(KEY_CODE_POINT);
	case 0x35: // / pressed
            return X(KEY_CODE_SLASH);
	case 0x36: // right shift pressed
            return X(KEY_CODE_RSHIFT);
	case 0x37: // (keypad) * pressed
            return X(KEY_CODE_KEYPAD_STAR);
	case 0x38: // left alt pressed
            return X(KEY_CODE_LALT);
	case 0x39: // space pressed
            return X(KEY_CODE_SPACE);
	case 0x3A: // CapsLock pressed
            return X(KEY_CODE_CAPSLOCK);
	case 0x3B: // F1 pressed
            return X(KEY_CODE_F1);
	case 0x3C: // F2 pressed
            return X(KEY_CODE_F2);
	case 0x3D: // F3 pressed
            return X(KEY_CODE_F3);
	case 0x3E: // F4 pressed
            return X(KEY_CODE_F4);
	case 0x3F: // F5 pressed
            return X(KEY_CODE_F5);
	case 0x40: // F6 pressed
            return X(KEY_CODE_F6);
	case 0x41: // F7 pressed
            return X(KEY_CODE_F7);
	case 0x42: // F8 pressed
            return X(KEY_CODE_F8);
	case 0x43: // F9 pressed
            return X(KEY_CODE_F9);
	case 0x44: // F10 pressed
            return X(KEY_CODE_F10);
	case 0x45: // NumberLock pressed
            return X(KEY_CODE_NUMBERLOCK);
	case 0x46: // ScrollLock pressed
            return X(KEY_CODE_SCROLLLOCK);
	case 0x47: // (keypad) 7 pressed
            return X(KEY_CODE_KEYPAD_7);
	case 0x48: // (keypad) 8 pressed
            return X(KEY_CODE_KEYPAD_8);
	case 0x49: // (keypad) 9 pressed
            return X(KEY_CODE_KEYPAD_9);
	case 0x4A: // (keypad) - pressed
            return X(KEY_CODE_KEYPAD_MINUS);
	case 0x4B: // (keypad) 4 pressed
            return X(KEY_CODE_KEYPAD_4);
	case 0x4C: // (keypad) 5 pressed
            return X(KEY_CODE_KEYPAD_5);
	case 0x4D: // (keypad) 6 pressed
            return X(KEY_CODE_KEYPAD_6);
	case 0x4E: // (keypad) + pressed
            return X(KEY_CODE_KEYPAD_PLUS);
	case 0x4F: // (keypad) 1 pressed
            return X(KEY_CODE_KEYPAD_1);
	case 0x50: // (keypad) 2 pressed
            return X(KEY_CODE_KEYPAD_2);
	case 0x51: // (keypad) 3 pressed
            return X(KEY_CODE_KEYPAD_3);
	case 0x52: // (keypad) 0 pressed
            return X(KEY_CODE_KEYPAD_0);
	case 0x53: // (keypad) . pressed
            return X(KEY_CODE_KEYPAD_POINT);
	case 0x57: // F11 pressed
            return X(KEY_CODE_F11);
	case 0x58: // F12 pressed
            return X(KEY_CODE_F12);

#define X(KEY_CODE) (struct key_event){.pressed = false, .scan_code = scan_code, .key_code = (KEY_CODE)}
		/* Released scancode */
	case 0x81: // escape released
            return X(KEY_CODE_ESC);
	case 0x82: // 1 released
            return X(KEY_CODE_1);
	case 0x83: // 2 released
            return X(KEY_CODE_2);
	case 0x84: // 3 released
            return X(KEY_CODE_3);
	case 0x85: // 4 released
            return X(KEY_CODE_4);
	case 0x86: // 5 released
            return X(KEY_CODE_5);
	case 0x87: // 6 released
            return X(KEY_CODE_6);
	case 0x88: // 7 released
            return X(KEY_CODE_7);
	case 0x89: // 8 released
            return X(KEY_CODE_8);
	case 0x8A: // 9 released
            return X(KEY_CODE_9);
	case 0x8B: // 0 (zero) released
            return X(KEY_CODE_0);
	case 0x8C: // - released
            return X(KEY_CODE_MINUS);
	case 0x8D: // = released
            return X(KEY_CODE_EQ);
	case 0x8E: // backspace released
            return X(KEY_CODE_BACKSPACE);
	case 0x8F: // tab released
            return X(KEY_CODE_TAB);
	case 0x90: // Q released
            return X(KEY_CODE_Q);
	case 0x91: // W released
            return X(KEY_CODE_W);
	case 0x92: // E released
            return X(KEY_CODE_E);
	case 0x93: // R released
            return X(KEY_CODE_R);
	case 0x94: // T released
            return X(KEY_CODE_T);
	case 0x95: // Y released
            return X(KEY_CODE_Y);
	case 0x96: // U released
            return X(KEY_CODE_U);
	case 0x97: // I released
            return X(KEY_CODE_I);
	case 0x98: // O released
            return X(KEY_CODE_O);
	case 0x99: // P released
            return X(KEY_CODE_P);
	case 0x9A: // [ released
            return X(KEY_CODE_LSBRACKET);
	case 0x9B: // ] released
            return X(KEY_CODE_RSBRACKET);
	case 0x9C: // enter released
            return X(KEY_CODE_ENTER);
	case 0x9D: // left control released
            return X(KEY_CODE_LCTRL);
	case 0x9E: // A released
            return X(KEY_CODE_A);
	case 0x9F: // S released
            return X(KEY_CODE_S);
	case 0xA0: // D released
            return X(KEY_CODE_D);
	case 0xA1: // F released
            return X(KEY_CODE_F);
	case 0xA2: // G released
            return X(KEY_CODE_G);
	case 0xA3: // H released
            return X(KEY_CODE_H);
	case 0xA4: // J released
            return X(KEY_CODE_J);
	case 0xA5: // K released
            return X(KEY_CODE_K);
	case 0xA6: // L released
            return X(KEY_CODE_L);
	case 0xA7: //  ; released
            return X(KEY_CODE_SEMICOLON);
	case 0xA8: // ' (single quote) released
            return X(KEY_CODE_SQUOTE);
	case 0xA9: // ` (back tick) released
            return X(KEY_CODE_BACKTICK);
	case 0xAA: // left shift released
            return X(KEY_CODE_LSHIFT);
	case 0xAB: // \ released
            return X(KEY_CODE_BACKSLASH);
	case 0xAC: // Z released
            return X(KEY_CODE_Z);
	case 0xAD: // X released
            return X(KEY_CODE_X);
	case 0xAE: // C released
            return X(KEY_CODE_C);
	case 0xAF: // V released
            return X(KEY_CODE_V);
	case 0xB0: // B released
            return X(KEY_CODE_B);
	case 0xB1: // N released
            return X(KEY_CODE_N);
	case 0xB2: // M released
            return X(KEY_CODE_M);
	case 0xB3: // , released
            return X(KEY_CODE_COMMA);
	case 0xB4: // . released
            return X(KEY_CODE_POINT);
	case 0xB5: // / released
            return X(KEY_CODE_SLASH);
	case 0xB6: // right shift released
            return X(KEY_CODE_RSHIFT);
	case 0xB7: // (keypad) * released
            return X(KEY_CODE_KEYPAD_STAR);
	case 0xB8: // left alt released
            return X(KEY_CODE_LALT);
	case 0xB9: // space released
            return X(KEY_CODE_SPACE);
	case 0xBA: // CapsLock released
            return X(KEY_CODE_CAPSLOCK);
	case 0xBB: // F1 released
            return X(KEY_CODE_F1);
	case 0xBC: // F2 released
            return X(KEY_CODE_F2);
	case 0xBD: // F3 released
            return X(KEY_CODE_F3);
	case 0xBE: // F4 released
            return X(KEY_CODE_F4);
	case 0xBF: // F5 released
            return X(KEY_CODE_F5);
	case 0xC0: // F6 released
            return X(KEY_CODE_F6);
	case 0xC1: // F7 released
            return X(KEY_CODE_F7);
	case 0xC2: // F8 released
            return X(KEY_CODE_F8);
	case 0xC3: // F9 released
            return X(KEY_CODE_F9);
	case 0xC4: // F10 released
            return X(KEY_CODE_F10);
	case 0xC5: // NumberLock released
            return X(KEY_CODE_NUMBERLOCK);
	case 0xC6: // ScrollLock released
            return X(KEY_CODE_SCROLLLOCK);
	case 0xC7: // (keypad) 7 released
            return X(KEY_CODE_KEYPAD_7);
	case 0xC8: // (keypad) 8 released
            return X(KEY_CODE_KEYPAD_8);
	case 0xC9: // (keypad) 9 released
            return X(KEY_CODE_KEYPAD_9);
	case 0xCA: // (keypad) - released
            return X(KEY_CODE_KEYPAD_MINUS);
	case 0xCB: // (keypad) 4 released
            return X(KEY_CODE_KEYPAD_4);
	case 0xCC: // (keypad) 5 released
            return X(KEY_CODE_KEYPAD_5);
	case 0xCD: // (keypad) 6 released
            return X(KEY_CODE_KEYPAD_6);
	case 0xCE: // (keypad) + released
            return X(KEY_CODE_KEYPAD_PLUS);
	case 0xCF: // (keypad) 1 released
            return X(KEY_CODE_KEYPAD_1);
	case 0xD0: // (keypad) 2 released
            return X(KEY_CODE_KEYPAD_2);
	case 0xD1: // (keypad) 3 released
            return X(KEY_CODE_KEYPAD_3);
	case 0xD2: // (keypad) 0 released
            return X(KEY_CODE_KEYPAD_0);
	case 0xD3: // (keypad) . released
            return X(KEY_CODE_KEYPAD_POINT);
	case 0xD7: // F11 released
            return X(KEY_CODE_F11);
	case 0xD8: // F12 released
            return X(KEY_CODE_F12);
	default:
            return X(KEY_CODE_NONE);
	}
#undef X
	/**
	 * 0xE0, 0x10 // (multimedia) previous track pressed
	 * 0xE0, 0x19 // (multimedia) next track pressed
	 * 0xE0, 0x1C // (keypad) enter pressed
	 * 0xE0, 0x1D // right control pressed
	 * 0xE0, 0x20 // (multimedia) mute pressed
	 * 0xE0, 0x21 // (multimedia) calculator pressed
	 * 0xE0, 0x22 // (multimedia) play pressed
	 * 0xE0, 0x24 // (multimedia) stop pressed
	 * 0xE0, 0x2E // (multimedia) volume down pressed
	 * 0xE0, 0x30 // (multimedia) volume up pressed
	 * 0xE0, 0x32 // (multimedia) WWW home pressed
	 * 0xE0, 0x35 // (keypad) / pressed
	 * 0xE0, 0x38 // right alt (or altGr) pressed
	 * 0xE0, 0x47 // home pressed
	 * 0xE0, 0x48 // cursor up pressed
	 * 0xE0, 0x49 // page up pressed
	 * 0xE0, 0x4B // cursor left pressed
	 * 0xE0, 0x4D // cursor right pressed
	 * 0xE0, 0x4F // end pressed
	 * 0xE0, 0x50 // cursor down pressed
	 * 0xE0, 0x51 // page down pressed
	 * 0xE0, 0x52 // insert pressed
	 * 0xE0, 0x53 // delete pressed
	 * 0xE0, 0x5B // left GUI pressed
	 * 0xE0, 0x5C // right GUI pressed
	 * 0xE0, 0x5D // "apps" pressed
	 * 0xE0, 0x5E // (ACPI) power pressed
	 * 0xE0, 0x5F // (ACPI) sleep pressed
	 * 0xE0, 0x63 // (ACPI) wake pressed
	 * 0xE0, 0x65 // (multimedia) WWW search pressed
	 * 0xE0, 0x66 // (multimedia) WWW favorites pressed
	 * 0xE0, 0x67 // (multimedia) WWW refresh pressed
	 * 0xE0, 0x68 // (multimedia) WWW stop pressed
	 * 0xE0, 0x69 // (multimedia) WWW forward pressed
	 * 0xE0, 0x6A // (multimedia) WWW back pressed
	 * 0xE0, 0x6B // (multimedia) my computer pressed
	 * 0xE0, 0x6C // (multimedia) email pressed
	 * 0xE0, 0x6D // (multimedia) media select pressed
	 * 0xE0, 0x90 // (multimedia) previous track released
	 * 0xE0, 0x99 // (multimedia) next track released
	 * 0xE0, 0x9C // (keypad) enter released
	 * 0xE0, 0x9D // right control released
	 * 0xE0, 0xA0 // (multimedia) mute released
	 * 0xE0, 0xA1 // (multimedia) calculator released
	 * 0xE0, 0xA2 // (multimedia) play released
	 * 0xE0, 0xA4 // (multimedia) stop released
	 * 0xE0, 0xAE // (multimedia) volume down released
	 * 0xE0, 0xB0 // (multimedia) volume up released
	 * 0xE0, 0xB2 // (multimedia) WWW home released
	 * 0xE0, 0xB5 // (keypad) / released
	 * 0xE0, 0xB8 // right alt (or altGr) released
	 * 0xE0, 0xC7 // home released
	 * 0xE0, 0xC8 // cursor up released
	 * 0xE0, 0xC9 // page up released
	 * 0xE0, 0xCB // cursor left released
	 * 0xE0, 0xCD // cursor right released
	 * 0xE0, 0xCF // end released
	 * 0xE0, 0xD0 // cursor down released
	 * 0xE0, 0xD1 // page down released
	 * 0xE0, 0xD2 // insert released
	 * 0xE0, 0xD3 // delete released
	 * 0xE0, 0xDB // left GUI released
	 * 0xE0, 0xDC // right GUI released
	 * 0xE0, 0xDD // "apps" released
	 * 0xE0, 0xDE // (ACPI) power released
	 * 0xE0, 0xDF // (ACPI) sleep released
	 * 0xE0, 0xE3 // (ACPI) wake released
	 * 0xE0, 0xE5 // (multimedia) WWW search released
	 * 0xE0, 0xE6 // (multimedia) WWW favorites released
	 * 0xE0, 0xE7 // (multimedia) WWW refresh released
	 * 0xE0, 0xE8 // (multimedia) WWW stop released
	 * 0xE0, 0xE9 // (multimedia) WWW forward released
	 * 0xE0, 0xEA // (multimedia) WWW back released
	 * 0xE0, 0xEB // (multimedia) my computer released
	 * 0xE0, 0xEC // (multimedia) email released
	 * 0xE0, 0xED // (multimedia) media select released
	 * 0xE0, 0x2A, 0xE0, 0x37 // print screen pressed
	 * 0xE0, 0xB7, 0xE0, 0xAA // print screen released
	 * 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 // pause pressed
	 */
};
