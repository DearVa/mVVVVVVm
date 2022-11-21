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
	MessageEntity me_ps{};
	me_ps.set_id(0);
	me_ps.set_type(PLAYER_STATUS);
	me_ps.set_allocated_ps(new PlayerStatus());
	messageEntities.insert_or_assign(0, me_ps);
	messageEntitiesDirty.insert_or_assign(0, true);

	// 传输存档的信息
	MessageEntity me_tsave{};
	me_tsave.set_allocated_tsave(game.GetTSave());
	const auto entity_id = TrackAndSetEntity(&me_tsave);
	me_tsave.set_id(entity_id);
	me_tsave.set_type(TSAVE);
	messageEntities.insert_or_assign(entity_id, me_tsave);
	messageEntitiesDirty.insert_or_assign(entity_id, true);

	return true;
}

int32_t Multiplayer::ServerReadUpdate(librg_world *w, librg_event *e)
{
	const int64_t event_owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);
	const int64_t owner_id = librg_entity_owner_get(w, entity_id);

	vlog_info("ServerReadUpdate %lld %lld %lld", event_owner_id, entity_id, owner_id);

	return CommonReadUpdate(owner_id, entity_id, w, e);
}

int32_t Multiplayer::ServerWriteUpdate(librg_world *w, librg_event *e)
{
	const int64_t event_owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);
	const int64_t owner_id = librg_entity_owner_get(w, entity_id);

	vlog_info("ServerWriteUpdate %lld %lld %lld", event_owner_id, entity_id, owner_id);

	return CommonWriteUpdate(owner_id, entity_id, w, e);
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

			// 新玩家加入，创建对应的玩家实体
			const enet_uint16 player_entity_id = event.peer->incomingPeerID + 1;

			librg_entity_track(world, player_entity_id);
			vlog_info("[server] librg_entity_track: %lld", player_entity_id);
			librg_entity_owner_set(world, player_entity_id, player_entity_id);
			librg_entity_chunk_set(world, player_entity_id, 1);

			MessageEntity me{};
			const auto ps = new PlayerStatus();
			ps->set_id(player_entity_id);
			me.set_id(player_entity_id);
			me.set_type(PLAYER_STATUS);
			me.set_allocated_ps(ps);
			messageEntities.insert_or_assign(player_entity_id, me);
			playerList.insert_or_assign(player_entity_id, ps);  // 将玩家添加到列表

			obj.createentity(0, 0, 132, 0, 8, player_entity_id, 1);
		} break;
		case ENET_EVENT_TYPE_DISCONNECT: {
			const enet_uint16 player_entity_id = event.peer->incomingPeerID + 1;
			playerList.erase(player_entity_id);
			messageEntities.erase(player_entity_id);

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

	for (ENetPeer *current_peer = server->peers; current_peer < &server->peers[server->peerCount]; ++current_peer) {
		if (current_peer->state != ENET_PEER_STATE_CONNECTED) {
			continue;
		}

		/* serialize peer's the world view to a buffer */
		buffer_length = 10240;
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
	const int64_t event_owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);
	const int64_t owner_id = librg_entity_owner_get(w, entity_id);

	vlog_info("ClientReadCreate %lld %lld %lld", event_owner_id, entity_id, owner_id);

	const auto size = librg_event_size_get(w, e);
	MessageEntity me{};

	if (size > 0) {
		me.ParseFromArray(librg_event_buffer_get(w, e), size);

		if (me.id() != entity_id)
		{
			vlog_error("ClientReadCreate Error: Entity id not match: %lld %lld", me.id(), entity_id);
			return 0;
		}
	}

	mp.messageEntities.insert_or_assign(entity_id, me);

	if (entity_id == mp.currentID) {  // 是自己的东西！好耶！
		mp.messageEntitiesDirty.insert_or_assign(entity_id, true);
	}

	return 0;
}

int32_t Multiplayer::ClientReadUpdate(librg_world *w, librg_event *e)
{
	const int64_t event_owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);
	const int64_t owner_id = librg_entity_owner_get(w, entity_id);

	vlog_info("ClientReadUpdate %lld %lld %lld", event_owner_id, entity_id, owner_id);

	return CommonReadUpdate(owner_id, entity_id, w, e);
}

int32_t Multiplayer::ClientReadRemove(librg_world *w, librg_event *e)
{
	const int64_t entity_id = librg_event_entity_get(w, e);
	mp.playerList.erase(entity_id);
	mp.messageEntities.erase(entity_id);
	mp.messageEntitiesDirty.erase(entity_id);
	return 0;
}

int32_t Multiplayer::ClientWriteUpdate(librg_world *w, librg_event *e)
{
	const int64_t event_owner_id = librg_event_owner_get(w, e);
	const int64_t entity_id = librg_event_entity_get(w, e);
	const int64_t owner_id = librg_entity_owner_get(w, entity_id);

	vlog_info("ClientWriteUpdate %lld %lld %lld", event_owner_id, entity_id, owner_id);

	return CommonWriteUpdate(owner_id, entity_id, w, e);
}

int Multiplayer::ClientUpdate()
{
	if (!librg_world_valid(world))
		return 1;

	ENetEvent event = {};

	auto *peer = static_cast<ENetPeer *>(librg_world_userdata_get(world));
	ENetHost *host = peer->host;
	currentID = peer->incomingPeerID + 1;  // 自己的id

	while (enet_host_service(host, &event, 2) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			MessageEntity me{};
			const auto ps = new PlayerStatus();
			ps->set_id(0);
			me.set_allocated_ps(ps);
			messageEntities.insert_or_assign(0, me);
			playerList.insert_or_assign(0, ps);  // 添加服务器的玩家对象

			vlog_info("[client] User %d has connected to the server. Peer ID: %d\n", peer->connectID, peer->incomingPeerID);
		} break;
		case ENET_EVENT_TYPE_DISCONNECT: {
			mp.playerList.erase(0);
			messageEntities.erase(0);  // 移除服务器对象
			currentID = 0;

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
	buffer_length = 10240;
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

	librg_entity_owner_set(world, entity_id, currentID);
	librg_entity_chunk_set(world, entity_id, 1);

	if (data != nullptr) {
		librg_entity_userdata_set(world, entity_id, data);
	}
	return entity_id;
}

int32_t Multiplayer::CommonReadUpdate(const int64_t owner_id, const int64_t entity_id, librg_world *w, librg_event *e)
{
	if (!mp.messageEntities.contains(entity_id))
	{
		vlog_error("CommonReadUpdate Error: Entity not existed in messageEntities");
		return 0;
	}

	auto &me = mp.messageEntities.at(entity_id);
	me.ParseFromArray(librg_event_buffer_get(w, e), librg_event_size_get(w, e));   // 更新自己的数据

	switch (me.type())
	{
	case TSAVE: {
		int a = 0;
		break;
	}
	case PLAYER_STATUS:
		if (me.ps().id() != entity_id)
		{
			// 不应该有这种情况
			vlog_error("CommonReadUpdate Error: PlayerStatus id not match entity_id");
			return 0;
		}
		HandlePlayerStatusRead(w, e, me.ps());  // 如果是PlayerStatus，就更新
		break;
	default:
		vlog_error("CommonReadUpdate Error: Unknown Type");
	}

	return 0;
}

int32_t Multiplayer::CommonWriteUpdate(const int64_t owner_id, const int64_t entity_id, librg_world *w, librg_event *e)
{
	if (owner_id != mp.currentID)
	{
		// 不是自己的就不要写数据！
		// vlog_info("CommonWriteUpdate Reject: owner_id != mp.currentID %lld %lld", owner_id, mp.currentID);
		return LIBRG_WRITE_REJECT;
	}

	if (const auto it = mp.messageEntitiesDirty.find(entity_id); it == mp.messageEntitiesDirty.end() || !(*it).second)
	{
		return LIBRG_WRITE_REJECT;  // 不是dirty
	}

	if (!mp.messageEntities.contains(entity_id))
	{
		vlog_error("CommonWriteUpdate Error: Entity not existed in messageEntities");  // 自己的列表里没有？TODO：让服务器重新发送创建
		return LIBRG_WRITE_REJECT;
	}

	auto &me = mp.messageEntities.at(entity_id);

	void *buffer = librg_event_buffer_get(w, e);
	const int32_t max_length = librg_event_size_get(w, e);
	int32_t bytes_written;

	// 根据情况判定要不要写
	switch (me.type())
	{
	case TSAVE: {
		if (!mp.isServer)
		{
			vlog_error("CommonWriteUpdate Error: TSAVE must be written by server!");
			return LIBRG_WRITE_REJECT;
		}
		me.set_allocated_tsave(game.GetTSave());
		bytes_written = me.SerializeToArray(buffer, max_length) ? me.ByteSizeLong() : 0;
		mp.messageEntitiesDirty.insert_or_assign(entity_id, false);
		break;
	}
	case PLAYER_STATUS:
		if (me.ps().id() != owner_id)
		{
			// 不应该有这种情况
			vlog_error("CommonWriteUpdate Error: PlayerStatus id not match owner_id");
			return LIBRG_WRITE_REJECT;
		}
		bytes_written = HandlePlayerStatusWrite(me, buffer, max_length);  // 如果是PlayerStatus，就更新
		// 永远不要把这个的dirty设为false，玩家位置保持实时更新
		break;
	default:
		vlog_error("CommonWriteUpdate Error: Unknown Type");
		return LIBRG_WRITE_REJECT;
	}

	return bytes_written;
}

int32_t Multiplayer::HandlePlayerStatusWrite(MessageEntity &me, void *buffer, const int32_t max_length)
{
	if (const int i = obj.getplayer(); INBOUNDS_VEC(i, obj.entities))
	{
		const auto ps = new PlayerStatus();
		ps->set_id(mp.currentID);
		ps->set_rx(map.currentRx);
		ps->set_ry(map.currentRy);
		ps->set_x(obj.entities[i].xp);
		ps->set_y(obj.entities[i].yp);
		ps->set_vx(obj.entities[i].vx);
		ps->set_vy(obj.entities[i].vy);
		ps->set_dir(obj.entities[i].dir);
		ps->set_tile(obj.entities[i].tile);
		ps->set_flip(game.gravitycontrol);
		me.set_allocated_ps(ps);

		const auto byte_size = me.ByteSizeLong();
		if (byte_size > max_length)
		{
			return LIBRG_WRITE_REJECT;
		}

		if (!me.SerializeToArray(buffer, max_length))
		{
			return LIBRG_WRITE_REJECT;
		}

		return byte_size;
	}

	// 没有找到玩家对象
	return LIBRG_WRITE_REJECT;
}

auto Multiplayer::HandlePlayerStatusRead(librg_world *w, librg_event *e, const PlayerStatus &status) -> void
{
	const librg_chunk chunk = librg_chunk_from_chunkpos(w, status.x() % 20, status.y() % 20, map.calct(status.x(), status.y()));
	librg_entity_chunk_set(w, status.id(), chunk);

	const bool invis = status.rx() != map.currentRx || status.ry() != map.currentRy;
	int i = obj.getotherplayer(status.id());
	if (!INBOUNDS_VEC(i, obj.entities))
	{
		// 没找到其他玩家，尝试创建
		if (obj.getplayer() != -1 && !invis)
		{
			obj.createentity(status.x(), status.y(), 132, status.dir(), 8, status.id(), 0);
			i = obj.getotherplayer(status.id());
		}
	}

	if (INBOUNDS_VEC(i, obj.entities))
	{
		obj.entities[i].xp = status.x();
		obj.entities[i].yp = status.y();
		obj.entities[i].vx = status.vx();
		obj.entities[i].vy = status.vy();
		obj.entities[i].dir = status.dir();
		obj.entities[i].tile = status.tile();
		obj.entities[i].rule = status.flip() ? 7 : 6;

		if (!invis && obj.entities[i].invis)  // 之前看不到，现在看到了
		{
			obj.entities[i].lerpoldxp = status.x();
			obj.entities[i].lerpoldyp = status.y();  // 就不lerp了

		}
		obj.entities[i].invis = invis;
	}
}
