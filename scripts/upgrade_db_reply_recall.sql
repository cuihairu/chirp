-- 存量库升级：messages 表补 reply_to / is_recalled 两列
--
-- 背景：chat 的 MySQL 档案层（mysql_message_store.cc）自消息引用（P1）与
-- 撤回墓碑（P0）起就写这两列，但 scripts/init_db.sql 漏跟，2026-10-04 文档
-- 对账补齐。新建库直接跑 init_db.sql 已含两列，本文件只给存量库用。
--
-- 注意：MySQL 不支持 ADD COLUMN IF NOT EXISTS（MariaDB 才支持），所以本
-- 文件不是幂等的——已经跑过/已手工加过列的表会报 duplicate column 错误，
-- 忽略该错误即可（其余语句可继续）。docker 首次卷初始化（entrypoint-initdb）
-- 只会跑 init_db.sql，不会重复执行这里。
--
-- 存量库升级方式：
--   mysql -u chirp -pchirp123 chirp < scripts/upgrade_db_reply_recall.sql

USE chirp;

ALTER TABLE messages ADD COLUMN reply_to VARCHAR(255) NOT NULL DEFAULT '';
ALTER TABLE messages ADD COLUMN is_recalled TINYINT(1) NOT NULL DEFAULT 0;

-- 旧数据保持原语义：存量消息视为非引用、未撤回。
-- 读取端（GetHistory SELECT）对缺失列本就宽容（row.size() 守卫），但写入端
-- （INSERT 恒带两列）无此容错——不加列则存量库上 StoreMessage 必失败。