#define MP_DEFINITION
#define LIBRG_IMPL
#include "librg.h"
#include "Multiplayer.h"

#include <Vlogging.h>

bool Multiplayer::Initialize()
{
	return enet_initialize() == 0;
}

void Multiplayer::Deinitialize()
{
	enet_deinitialize();
}

bool Multiplayer::HostServer()
{
	ENetAddress address = { 0 };

	address.host = ENET_HOST_ANY; /* Bind the server to the default localhost.     */
	address.port = 6154; /* Bind the server to port 7777. */

#define MAX_CLIENTS 3

	/* create a server */
	server = enet_host_create(&address, MAX_CLIENTS, 2, 0, 0);

	if (server == NULL) {
		vlog_error("An error occurred while trying to create an ENet server host.\n");
		return false;
	}

	vlog_info("[server] Started an ENet server...\n");
	serverWorld = librg_world_create();

	if (serverWorld == NULL) {
		vlog_info("[server] An error occurred while trying to create a server world.\n");
		return false;
	}

	vlog_info("[server] Created a new server world\n");

	/* store our host to the userdata */
	librg_world_userdata_set(serverWorld, server);

	/* config our world grid */
	librg_config_chunksize_set(serverWorld, 40, 30, 1);
	librg_config_chunkamount_set(serverWorld, 20, 20, 4);
	librg_config_chunkoffset_set(serverWorld, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID);

	librg_event_set(serverWorld, LIBRG_WRITE_UPDATE, ServerWriteUpdate);
	librg_event_set(serverWorld, LIBRG_READ_UPDATE, ServerReadUpdate);

	ENetEvent event;

	/* Wait up to 1000 milliseconds for an event. (WARNING: blocking) */
	while (enet_host_service(server, &event, 2) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT:
			vlog_info("A new client connected from %x:%u.\n", event.peer->address.host, event.peer->address.port);
			/* Store any relevant client information here. */
			event.peer->data = "Client information";
			break;

		case ENET_EVENT_TYPE_RECEIVE:
			vlog_info("A packet of length %lu containing %s was received from %s on channel %u.\n",
				event.packet->dataLength,
				event.packet->data,
				event.peer->data,
				event.channelID);
			/* Clean up the packet now that we're done using it. */
			enet_packet_destroy(event.packet);
			break;

		case ENET_EVENT_TYPE_DISCONNECT:
			vlog_info("%s disconnected.\n", event.peer->data);
			/* Reset the peer's client information. */
			event.peer->data = NULL;
			break;

		case ENET_EVENT_TYPE_NONE:
			break;
		}
	}

	enet_host_destroy(server);
}

int32_t Multiplayer::ServerReadUpdate(librg_world* w, librg_event* e)
{
	int64_t entity_id = librg_event_entity_get(w, e);
	size_t actual_length = librg_event_size_get(w, e);

	if (actual_length != sizeof(vec3)) {
		printf("[server] Invalid data size coming from client\n");
		return 0;
	}

	ENetPeer* peer = (ENetPeer*)librg_entity_userdata_get(w, entity_id);
	char* buffer = librg_event_buffer_get(w, e);

	/* read and update actual position */
	vec3 position = { 0 };
	memcpy(peer->data, buffer, actual_length);
	memcpy(&position, buffer, actual_length);

	/* and update librg actual chunk id */
	librg_chunk chunk = librg_chunk_from_realpos(w, position.x, position.y, position.z);
	librg_entity_chunk_set(w, entity_id, chunk);

	return 0;
}

int32_t Multiplayer::ServerWriteUpdate(librg_world* w, librg_event* e)
{
	int64_t owner_id = librg_event_owner_get(w, e);
	int64_t entity_id = librg_event_entity_get(w, e);

	/* prevent sending updates to users who own that entity */
	/* since they will be responsible on telling where that entity is supposed to be */
	if (librg_entity_owner_get(w, entity_id) == owner_id) {
		return LIBRG_WRITE_REJECT;
	}

	/* read our current position */
	ENetPeer* peer = (ENetPeer*)librg_entity_userdata_get(w, entity_id);

	char* buffer = librg_event_buffer_get(w, e);
	size_t max_length = librg_event_size_get(w, e);

	/* check if we have enough space to write and valid position */
	if (sizeof(vec3) > max_length || !peer->data) {
		return LIBRG_WRITE_REJECT;
	}

	/* write data and return how much we've written */
	memcpy(buffer, peer->data, sizeof(vec3));
	return sizeof(vec3);
}

