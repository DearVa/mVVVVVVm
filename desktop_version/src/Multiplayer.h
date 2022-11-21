#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

#include <enet/enet.h>
#include "librg.h"
#include "librg_enet.h"
#include "protobuf/mvvvvvvm.pb.h"
#include <vector>

class Multiplayer
{
public:
	bool isServer;
	librg_world *world = nullptr;
	UINT32 currentID = 0;

	size_t buffer_length = 10240;
	char *buffer = nullptr;

	std::unordered_map<int64_t, PlayerStatus *> playerList;

	/**
	 * \brief id: me
	 */
	std::unordered_map<int64_t, MessageEntity> messageEntities;

	/**
	 * \brief ��¼ĳ��MessageEntity�Ƿ���Ҫ���͸������ݣ�������ֻ��¼�����Լ���Entity
	 */
	std::unordered_map<int64_t, bool> messageEntitiesDirty;

	bool Initialize();
	void Deinitialize();

	bool StartMultiPlayer();

	void Update();
private:
	bool HostWorld();
	static int32_t ServerWriteUpdate(librg_world *w, librg_event *e);
	static int32_t ServerReadUpdate(librg_world *w, librg_event *e);
	int ServerUpdate();

	bool JoinWorld();
	static int32_t ClientReadCreate(librg_world *w, librg_event *e);
	static int32_t ClientReadUpdate(librg_world *w, librg_event *e);
	static int32_t ClientReadRemove(librg_world *w, librg_event *e);
	static int32_t ClientWriteUpdate(librg_world *w, librg_event *e);
	int ClientUpdate();

	int64_t currentAutoAllocEntityID = INT64_MAX / 2;
	/**
	 * \brief �½�һ��Entity��Track����д�����ݣ����������ã�������id��id��INT64_MAX / 2��ʼ������
	 * \param data
	 */
	int64_t TrackAndSetEntity(void *data);

	static int32_t CommonReadUpdate(const int64_t owner_id, const int64_t entity_id, librg_world *w, librg_event *e);
	static int32_t CommonWriteUpdate(const int64_t owner_id, const int64_t entity_id, librg_world *w, librg_event *e);

	static int32_t HandlePlayerStatusWrite(MessageEntity &me, void *buffer, const int32_t max_length);
	static void HandlePlayerStatusRead(librg_world *w, librg_event *e, const PlayerStatus &status);
};

#ifndef MP_DEFINITION
extern Multiplayer mp;
#endif

#endif
