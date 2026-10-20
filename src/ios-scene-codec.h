#pragma once
#include <stdint.h>
#include "dictionary.h"
struct ios_scene_geometry { uint32_t width,height,refresh; };
/* Caller owns the returned dictionary. No IPC or lifecycle effects. */
spc_dictionary_t *ios_scene_create_event(const char *identifier,const struct ios_scene_geometry *display);
