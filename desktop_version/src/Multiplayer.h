#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

#include <enet/enet.h>
#include "librg.h"
#include "librg_enet.h"

typedef struct { float x, y, z; } vec3;

class Multiplayer
{
public:

	ENetHost *server = nullptr;
	librg_world *serverWorld = nullptr;

	static bool Initialize();
	static void Deinitialize();

	bool HostServer();

private:
	static int32_t ServerWriteUpdate(librg_world* w, librg_event* e);
	static int32_t ServerReadUpdate(librg_world* w, librg_event* e);
};

#ifndef MP_DEFINITION
extern Multiplayer mp;
#endif

#endif
