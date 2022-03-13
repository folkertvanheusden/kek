// (C) 2018-2022 by Folkert van Heusden
// Released under AGPL v3.0
#ifndef NDEBUG
#define D(x) do { x } while(0);
#else
#define D(...) do { } while(0);
#endif
