#pragma once
#include <enet/enet.h>
#include "librg.h"

class Multiplayer
{
public:
    ENetHost* server;

	bool Initialize()
	{
		return enet_initialize() == 0;
	}

	void Deinitialize()
	{
		enet_deinitialize();
	}

	bool HostServer() {
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
        server_world = librg_world_create();

        if (server_world == NULL) {
            vlog_info("[server] An error occurred while trying to create a server world.\n");
            return 1;
        }

        vlog_info("[server] Created a new server world\n");

        /* store our host to the userdata */
        librg_world_userdata_set(server_world, server);

        /* config our world grid */
        librg_config_chunksize_set(server_world, 16, 16, 16);
        librg_config_chunkamount_set(server_world, 9, 9, 9);
        librg_config_chunkoffset_set(server_world, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID);

        librg_event_set(server_world, LIBRG_WRITE_UPDATE, server_write_update);
        librg_event_set(server_world, LIBRG_READ_UPDATE, server_read_update);

        ENetEvent event;

        /* Wait up to 1000 milliseconds for an event. (WARNING: blocking) */
        while (enet_host_service(server, &event, 1000) > 0) {
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
};

