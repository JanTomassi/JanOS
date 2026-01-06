#pragma once

#define WITH(acquire, release) \
  for (int _once = ((acquire), 0); _once == 0; (release), _once = 1)
