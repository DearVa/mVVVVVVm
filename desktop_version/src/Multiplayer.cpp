#define MP_DEFINITION
#define LIBRG_IMPL
#include "librg.h"
#include "Multiplayer.h"

#include <Vlogging.h>
#include <Entity.h>
#include <Map.h>
#include <UtilityClass.h>

extern Multiplayer mp;

bool Multiplayer::Initialize()
{
	buffer = static_cast<char *>(malloc(buffer_length));
	return enet_initialize() == 0;
}

void Multiplayer::Deinitialize()
{
	free(buffer);
	buffer = nullptr;
	enet_deinitialize();
}

bool Multiplayer::StartMultiPlayer()
{
	if (isServer)
	{
		return HostWorld();
	}
	else
	{
		return JoinWorld();
	}
}


void Multiplayer::Update()
{
	if (isServer)
	{
		ServerUpdate();
	}
	else
	{
		ClientUpdate();
	}
}

bool Multiplayer::HostWorld()
{
	constexpr ENetAddress address = { ENET_HOST_ANY , 6154 };

#define MAX_CLIENTS 3

	/* create a server */
	ENetHost *host = enet_host_create(&address, MAX_CLIENTS, 2, 0, 0);

	if (host == nullptr) {
		vlog_error("An error occurred while trying to create an ENet server host.\n");
		return false;
	}

	vlog_info("[server] Started an ENet server...\n");
	world = librg_world_create();

	if (world == nullptr) {
		vlog_info("[server] An error occurred while trying to create a server world.\n");
		return false;
	}

	vlog_info("[server] Created a new server world\n");

	/* store our host to the user data */
	librg_world_userdata_set(world, host);

	/* config our world grid */
	librg_config_chunksize_set(world, 40, 30, 1);
	librg_config_chunkamount_set(world, 20, 20, 13);
	librg_config_chunkoffset_set(world, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID);

	librg_event_set(world, LIBRG_WRITE_UPDATE, ServerWriteUpdate);
	librg_event_set(world, LIBRG_READ_UPDATE, ServerReadUpdate);

	currentID = 0;

	// 服务器添加自己
	librg_entity_track(world, 0);
	librg_entity_owner_set(world, 0, 0);
	librg_entity_chunk_set(world, 0, 1);
	librg_entity_userdata_set(world, 0, host); /* save ptr to peer */

	// 传输存档的信息
	MessageEntity me{};
	game.GetTSave(me.tsave());
	TrackAndSetEntity(&me);

	return true;
}

int32_t Multiplayer::ServerReadUpdate(librg_world *w, librg_event *e)
{
	const int64_t owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);

	vlog_info("ServerReadUpdate %lld %lld", owner_id, entity_id);

	const auto size = librg_event_size_get(w, e);
	if (size > sizeof(MessageEntity))
	{
		vlog_error("ServerReadUpdate Error: Entity size not match");
		return 0;
	}

	if (mp.messageEntities.find(entity_id) == mp.messageEntities.end())
	{
		vlog_error("ServerReadUpdate Error: Entity not existed in messageEntities");
		return 0;
	}

	auto &me = mp.messageEntities.at(entity_id);
	me.Parse(librg_event_buffer_get(w, e), size);

	switch (me.type)
	{
	case TSaveMsg:
		break;
	case PlayerStatusMsg:
		HandlePlayerStatusRead(w, e, me.ps);
		break;
	}

	return 0;
}

int32_t Multiplayer::ServerWriteUpdate(librg_world *w, librg_event *e)
{
	const int64_t owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);

	vlog_info("ServerWriteUpdate %lld %lld", owner_id, entity_id);

	// 服务器id为0，所以如果需要更新的Entity的id不是0，那就不是自己的，不要更新
	if (librg_entity_owner_get(w, entity_id) != 0) {
		return LIBRG_WRITE_REJECT;
	}

	/* read our current status */
	char *buffer = librg_event_buffer_get(w, e);
	const size_t max_length = librg_event_size_get(w, e);

	/* check if we have enough space to write and valid position */
	if (sizeof(PlayerStatus) > max_length) {
		return LIBRG_WRITE_REJECT;
	}

	MessageEntity me{};
	const int i = obj.getplayer();
	if (INBOUNDS_VEC(i, obj.entities))
	{
		me.ps.set_id(0);
		me.ps.set_rx(map.currentRx);
		me.ps.set_ry(map.currentRy);
		me.ps.set_x(obj.entities[i].xp);
		me.ps.set_y(obj.entities[i].yp);
		me.ps.set_vx(obj.entities[i].vx);
		me.ps.set_vy(obj.entities[i].vy);
		me.ps.set_dir(obj.entities[i].dir);
		me.ps.set_tile(obj.entities[i].tile);
		me.ps.set_flip(game.gravitycontrol);
	}

	/* write data and return how much we've written */
	return me.Serialize(buffer, max_length);
}

int Multiplayer::ServerUpdate()
{
	if (!librg_world_valid(world))
		return 1;

	const auto server = static_cast<ENetHost *>(librg_world_userdata_get(world));
	ENetEvent event{};

	while (enet_host_service(server, &event, 2) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			vlog_info("[server] A new user %d connected. Peer ID: %d", event.peer->connectID, event.peer->incomingPeerID);

			// 新玩家加入
			const int64_t player_entity_id = event.peer->incomingPeerID + 1;

			/* we create an entity for our client */
			/* in our case it is going to have same id as owner id */
			/* since we do not really plan on adding more entities per client for now */
			/* and place his entity right in the centerl of the world */
			librg_entity_track(world, player_entity_id);

			vlog_info("[server] librg_entity_track: %lld", player_entity_id);

			librg_entity_owner_set(world, player_entity_id, player_entity_id);
			librg_entity_chunk_set(world, player_entity_id, 1);
			// librg_entity_userdata_set(world, entity_id, &me);

			MessageEntity me{};
			me.ps.set_id(player_entity_id);
			messageEntities.insert_or_assign(player_entity_id, me);
			playerList.insert_or_assign(player_entity_id, &me.ps);  // 将玩家添加到列表

			obj.createentity(0, 0, 132, 0, 8, player_entity_id, 1);

			/* allocate and store entity position in the data part of peer */
			//vec3 entity_position = {};
			//event.peer->data = malloc(sizeof(PlayerStatus));
			//*static_cast<PlayerStatus *>(event.peer->data) = status;

		} break;
		case ENET_EVENT_TYPE_DISCONNECT: {
			const int64_t player_entity_id = event.peer->incomingPeerID + 1;
			messageEntities.erase(player_entity_id);
			playerList.erase(player_entity_id);

			vlog_info("[server] A user %d disconnected.\n", event.peer->connectID);
			librg_entity_untrack(world, player_entity_id);
			free(event.peer->data);
		} break;

		case ENET_EVENT_TYPE_RECEIVE: {
			/* handle a newly received event */
			librg_world_read(
				world,
				currentID,
				reinterpret_cast<char *>(event.packet->data),
				event.packet->dataLength,
				nullptr
			);

			/* Clean up the packet now that we're done using it. */
			enet_packet_destroy(event.packet);
		} break;

		case ENET_EVENT_TYPE_NONE: break;
		}
	}

	for (ENetPeer* current_peer = server->peers; current_peer < &server->peers[server->peerCount]; ++current_peer) {
		if (current_peer->state != ENET_PEER_STATE_CONNECTED) {
			continue;
		}

		/* serialize peer's the world view to a buffer */
		librg_world_write(
			world,
			currentID,
			2, /* chunk radius */
			buffer,
			&buffer_length,
			nullptr
		);

		/* create packet with actual length, and send it */
		ENetPacket *packet = enet_packet_create(buffer, buffer_length, ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(current_peer, 0, packet);
	}

	return 0;
}

bool Multiplayer::JoinWorld()
{
	ENetAddress address = { 0, 6154 };
	enet_address_set_host(&address, "127.0.0.1");

	ENetHost *host = enet_host_create(nullptr, 1, 2, 0, 0);
	ENetPeer *peer = enet_host_connect(host, &address, 2, 0);

	if (peer == nullptr) {
		vlog_error("[client] Cannot connect\n");
		return false;
	}

	world = librg_world_create();
	librg_world_userdata_set(world, peer);

	librg_event_set(world, LIBRG_READ_CREATE, ClientReadCreate);
	librg_event_set(world, LIBRG_READ_UPDATE, ClientReadUpdate);
	librg_event_set(world, LIBRG_READ_REMOVE, ClientReadRemove);
	librg_event_set(world, LIBRG_WRITE_UPDATE, ClientWriteUpdate);

	return true;
}

int32_t Multiplayer::ClientReadCreate(librg_world *w, librg_event *e)
{
	// 读取对应的信息，存入本地
	const int64_t owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);
	vlog_info("ClientReadCreate %lld %lld", owner_id, entity_id);

	const auto size = librg_event_size_get(w, e);
	MessageEntity me{};
	me.Parse(librg_event_buffer_get(w, e), size);

	if (me.id != entity_id)
	{
		vlog_error("ClientReadCreate Error: Entity id not match");
		return 0;
	}

	mp.messageEntities.insert_or_assign(entity_id, me);

	return 0;
}

int32_t Multiplayer::ClientReadUpdate(librg_world *w, librg_event *e)
{
	const int64_t owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);

	vlog_info("ClientReadUpdate %lld %lld", owner_id, entity_id);

	if (mp.messageEntities.find(entity_id) == mp.messageEntities.end())
	{
		vlog_error("ClientReadUpdate Error: Entity not existed in messageEntities");
		return 0;
	}
	
	auto &me = mp.messageEntities.at(entity_id);
	me.Parse(librg_event_buffer_get(w, e), librg_event_size_get(w, e));

	switch (me.type)
	{
	case TSaveMsg:
		break;
	case PlayerStatusMsg:
		HandlePlayerStatusRead(w, e, me.ps);
		break;
	}

	return 0;
}

int32_t Multiplayer::ClientReadRemove(librg_world *w, librg_event *e)
{
	const int64_t entity_id = librg_event_entity_get(w, e);
	mp.messageEntities.erase(entity_id);
	return 0;
}

int32_t Multiplayer::ClientWriteUpdate(librg_world *w, librg_event *e)
{
	const int64_t owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);

	vlog_info("ClientWriteUpdate %lld %lld", owner_id, entity_id);
	
	const size_t max_length = librg_event_size_get(w, e);

	/* check if we have enough space to write and valid position */
	if (sizeof(MessageEntity) > max_length) {
		return LIBRG_WRITE_REJECT;
	}

	MessageEntity me{};
	const int i = obj.getplayer();
	if (INBOUNDS_VEC(i, obj.entities))
	{
		me.ps.set_id(entity_id);
		me.ps.set_rx(map.currentRx);
		me.ps.set_ry(map.currentRy);
		me.ps.set_x(obj.entities[i].xp);
		me.ps.set_y(obj.entities[i].yp);
		me.ps.set_vx(obj.entities[i].vx);
		me.ps.set_vy(obj.entities[i].vy);
		me.ps.set_dir(obj.entities[i].dir);
		me.ps.set_tile(obj.entities[i].tile);
		me.ps.set_flip(game.gravitycontrol);
	}
	
	return me.Serialize(librg_event_buffer_get(w, e), max_length);
}

int Multiplayer::ClientUpdate()
{
	if (!librg_world_valid(world))
		return 1;

	ENetEvent event = {};

	auto *peer = static_cast<ENetPeer *>(librg_world_userdata_get(world));
	ENetHost *host = peer->host;
	currentID = peer->incomingPeerID;  // 自己的id

	while (enet_host_service(host, &event, 2) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			MessageEntity me{};
			me.ps.set_id(0);
			messageEntities.insert_or_assign(0, me);
			playerList.insert_or_assign(0, &me.ps);  // 添加服务器的玩家对象

			obj.createentity(0, 0, 132, 0, 8, 0, 1);
			const int player = obj.getplayer();
			if (INBOUNDS_VEC(player, obj.entities))
			{
				obj.entities[player].id = currentID;  // 设置自己的玩家对象id
			}
			vlog_info("[client] User %d has connected to the server. Peer ID: %d\n", peer->connectID, peer->incomingPeerID);
		} break;
		case ENET_EVENT_TYPE_DISCONNECT: {
			mp.playerList.erase(0);

			currentID = 0;
			const int player = obj.getplayer();
			if (INBOUNDS_VEC(player, obj.entities))
			{
				obj.entities[player].id = 0;  // 设置自己的玩家对象id
			}

			vlog_info("[client] A user %d has disconnected from server.\n", peer->connectID);
		} break;

		case ENET_EVENT_TYPE_RECEIVE: {
			/* handle a newly received event */
			librg_world_read(
				world,
				currentID,
				reinterpret_cast<char *>(event.packet->data),
				event.packet->dataLength,
				nullptr
			);

			/* Clean up the packet now that we're done using it. */
			enet_packet_destroy(event.packet);
		} break;

		case ENET_EVENT_TYPE_NONE: break;
		}
	}

	/* serialize peer's the world view to a buffer */
	librg_world_write(
		world,
		currentID,
		2,
		buffer,
		&buffer_length,
		nullptr
	);

	/* create packet with actual length, and send it */
	ENetPacket *packet = enet_packet_create(buffer, buffer_length, ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, packet);

	return 0;
}

int64_t Multiplayer::TrackAndSetEntity(void *data)
{
	const int64_t entity_id = currentAutoAllocEntityID++;
	librg_entity_track(world, entity_id);

	vlog_info("[server] librg_entity_track: %lld", entity_id);

	librg_entity_owner_set(world, entity_id, entity_id);
	librg_entity_chunk_set(world, entity_id, 1);

	librg_entity_userdata_set(world, entity_id, data);  // 将世界的信息附带
	return entity_id;
}

auto Multiplayer::HandlePlayerStatusRead(librg_world* w, librg_event* e, const PlayerStatus &status) -> void
{
	const librg_chunk chunk = librg_chunk_from_chunkpos(w, status.x() % 20, status.y() % 20, map.calct(status.x(), status.y()));
	librg_entity_chunk_set(w, status.id(), chunk);
	const int i = obj.getotherplayer(status.id());
	if (INBOUNDS_VEC(i, obj.entities))
	{
		obj.entities[i].xp = status.x();
		obj.entities[i].yp = status.y();
		obj.entities[i].vx = status.vx();
		obj.entities[i].vy = status.vy();
		obj.entities[i].dir = status.dir();
		obj.entities[i].tile = status.tile();
		obj.entities[i].rule = status.flip() ? 7 : 6;

		const bool invis = status.rx() != map.currentRx || status.ry() != map.currentRy;
		if (!invis && obj.entities[i].invis)  // 之前看不到，现在看到了
		{
			obj.entities[i].lerpoldxp = status.x();
			obj.entities[i].lerpoldyp = status.y();  // 就不lerp了

		}
		obj.entities[i].invis = invis;
	}
}

bool MessageEntity::Parse(void *data, const size_t max_size)
{
	switch (type)
	{
	case WorldMsg:
		world.ParseFromArray(static_cast<char*>(data) + sizeof(int64_t) + sizeof(MessageType), max_size - sizeof(int64_t) + sizeof(MessageType));
		break;
	case TSaveMsg:
		t_save.ParseFromArray(static_cast<char*>(data) + sizeof(int64_t) + sizeof(MessageType), max_size - sizeof(int64_t) + sizeof(MessageType));
		break;
	case PlayerStatusMsg:
		ps.ParseFromArray(static_cast<char*>(data) + sizeof(int64_t) + sizeof(MessageType), max_size - sizeof(int64_t) + sizeof(MessageType));
		break;
	default:
		vlog_error("MessageEntity::Create unsupported type");
		return false;
	}
	return true;
}

size_t MessageEntity::Serialize(void *buffer, const size_t max_size) const
{
	memcpy(buffer, this, sizeof(int64_t) + sizeof(MessageType));
	switch (type)
	{
	case WorldMsg:
		world.SerializeToArray(static_cast<char*>(buffer) + sizeof(int64_t) + sizeof(MessageType), max_size - sizeof(int64_t) + sizeof(MessageType));
		return world.ByteSizeLong();
	case TSaveMsg:
		t_save.SerializeToArray(static_cast<char *>(buffer) + sizeof(int64_t) + sizeof(MessageType), max_size - sizeof(int64_t) + sizeof(MessageType));
		return t_save.ByteSizeLong();
	case PlayerStatusMsg:
		ps.SerializeToArray(static_cast<char *>(buffer) + sizeof(int64_t) + sizeof(MessageType), max_size - sizeof(int64_t) + sizeof(MessageType));
		return ps.ByteSizeLong();
	default:
		vlog_error("MessageEntity::Serialize unsupported type");
		return 0;
	}
}

void *MessageEntity::data()
{
	return reinterpret_cast<char*>(this) + sizeof(int64_t) + sizeof(MessageType);
}
