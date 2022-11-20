## type

0 - Player

12 - crew

14 - Super Crew Member
       This special crewmember is way more advanced than the usual kind, and can interact with game objects

132 - Another player

**meta1 - dir**

**meta2 - colour**



RRLRFRRL



Entities定义：

0 ~ MAX_CLIENT_COUNT 为玩家实体，玩家实体的id就是实体id，这与owner_id相同



INT64_MAX / 2 ~ INT64_MAX 为传输消息所用的实体，其中

INT64_MAX / 2 = 世界存档