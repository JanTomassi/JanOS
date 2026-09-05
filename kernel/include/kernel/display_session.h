#pragma once

#include <stdbool.h>
#include <stdint.h>

enum display_session_mode {
	DISPLAY_SESSION_BOOT,
	DISPLAY_SESSION_FOREGROUND,
};

struct display_session {
	enum display_session_mode mode;
	uint32_t owner;
};

void display_session_init(struct display_session *session);
bool display_session_claim(struct display_session *session, uint32_t owner);
bool display_session_release(struct display_session *session, uint32_t owner);
bool display_session_allows(const struct display_session *session, uint32_t owner);
