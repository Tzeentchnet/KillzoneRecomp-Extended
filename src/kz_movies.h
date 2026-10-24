#pragma once

class PS2Runtime;

// [Game] SkipIntro and KZ_SKIP_LONG_INTRO=0/1 skip only the long boot intro; KZ_MOVIE_LOG logs movie names.
void kzMoviesInstall(PS2Runtime &runtime);
