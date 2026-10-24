#pragma once
/* Caller owns the returned session JSON and must free it. */
char *bober_prompt_session(const char *fallback_session);
