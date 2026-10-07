-- 存量库升级：group_members 表补 alias 列（群昵称，message_search 批
-- 2026-10-08）。
--
-- 背景：群昵称别名随 search/alias 批引入协议面（2122-2124）与
-- GroupMember.alias 下发。group_members 表当前仍无代码读写（chat 群是进程
-- 内存态），本列先行落库，持久化写入随 chat 群持久化走；口径见
-- docs/design-notes/message_search.md。新建库直接跑 init_db.sql 已含本列。
--
-- 注意：MySQL 不支持 ADD COLUMN IF NOT EXISTS（MariaDB 才支持），本文件不
-- 是幂等的——已加过列的表会报 duplicate column 错误，忽略即可。docker 首次
-- 卷初始化只跑 init_db.sql，不会重复执行这里。
--
-- 存量库升级方式：
--   mysql -u chirp -pchirp123 chirp < scripts/upgrade_db_group_alias.sql

USE chirp;

ALTER TABLE group_members ADD COLUMN alias VARCHAR(255) NOT NULL DEFAULT '';

-- 存量行保持 '' = 未设置，渲染端按缺省回退 username，无需回填。
