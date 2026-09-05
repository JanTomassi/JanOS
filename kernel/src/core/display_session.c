#include <kernel/display_session.h>

void display_session_init(struct display_session *session)
{
	if (session != 0)
		*session = (struct display_session){
			.mode = DISPLAY_SESSION_BOOT,
		};
}

bool display_session_claim(struct display_session *session, uint32_t owner,
	uint32_t token)
{
	if (session == 0 || owner == 0 || token == 0 ||
		(session->mode != DISPLAY_SESSION_BOOT &&
		 session->mode != DISPLAY_SESSION_FOREGROUND))
		return false;
	if (session->mode == DISPLAY_SESSION_FOREGROUND &&
		(session->owner != owner || session->token != token))
		return false;
	session->mode = DISPLAY_SESSION_FOREGROUND;
	session->owner = owner;
	session->token = token;
	return true;
}

bool display_session_release(struct display_session *session, uint32_t owner,
	uint32_t token)
{
	if (session == 0 || owner == 0 || token == 0 ||
		session->mode != DISPLAY_SESSION_FOREGROUND || session->owner != owner ||
		session->token != token)
		return false;
	session->mode = DISPLAY_SESSION_BOOT;
	session->owner = 0;
	session->token = 0;
	return true;
}

bool display_session_allows(const struct display_session *session, uint32_t owner)
{
	if (session == 0)
		return false;
	if (session->mode == DISPLAY_SESSION_BOOT)
		return true;
	return session->mode == DISPLAY_SESSION_FOREGROUND && owner != 0 &&
		session->owner == owner;
}
