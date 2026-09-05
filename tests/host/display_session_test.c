#include "test.h"

#include <kernel/display_session.h>

int main(void)
{
	struct display_session session;
	display_session_init(&session);
	TEST_ASSERT(session.mode == DISPLAY_SESSION_BOOT);
	TEST_ASSERT(session.owner == 0);
	TEST_ASSERT(session.token == 0);
	TEST_ASSERT(display_session_allows(&session, 0));
	TEST_ASSERT(display_session_allows(&session, 7));

	TEST_ASSERT(display_session_claim(&session, 7, 41));
	TEST_ASSERT(session.mode == DISPLAY_SESSION_FOREGROUND);
	TEST_ASSERT(session.owner == 7);
	TEST_ASSERT(session.token == 41);
	TEST_ASSERT(display_session_allows(&session, 7));
	TEST_ASSERT(!display_session_allows(&session, 8));
	TEST_ASSERT(!display_session_allows(&session, 0));
	TEST_ASSERT(display_session_claim(&session, 7, 41));
	TEST_ASSERT(!display_session_claim(&session, 7, 42));
	TEST_ASSERT(!display_session_claim(&session, 8, 41));
	TEST_ASSERT(!display_session_release(&session, 8, 41));
	TEST_ASSERT(!display_session_release(&session, 7, 42));
	TEST_ASSERT(display_session_release(&session, 7, 41));
	TEST_ASSERT(session.mode == DISPLAY_SESSION_BOOT);
	TEST_ASSERT(session.owner == 0);
	TEST_ASSERT(session.token == 0);
	return 0;
}
