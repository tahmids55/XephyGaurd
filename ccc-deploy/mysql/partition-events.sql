-- Optional: monthly RANGE partitioning of the high-volume `events` table (MySQL 8).
-- MySQL requires the partitioning column in every unique key, so the primary key and the
-- (agent_id, seq) de-duplication key are rebuilt to include occurred_at. Run during maintenance:
--   docker compose exec -T mysql mysql -uroot -p"$DB_ROOT_PASSWORD" xephyguard < mysql/partition-events.sql
-- Dropping an old partition is then an instant retention purge:
--   ALTER TABLE events DROP PARTITION p2026_01;

ALTER TABLE events DROP FOREIGN KEY events_pc_id_foreign;
ALTER TABLE events DROP PRIMARY KEY, ADD PRIMARY KEY (id, occurred_at);
ALTER TABLE events DROP INDEX events_agent_id_seq_unique, ADD UNIQUE KEY events_agent_seq_time (agent_id, seq, occurred_at);
ALTER TABLE events PARTITION BY RANGE COLUMNS (occurred_at) (
    PARTITION p2026_09 VALUES LESS THAN ('2026-10-01'),
    PARTITION p2026_10 VALUES LESS THAN ('2026-11-01'),
    PARTITION p2026_11 VALUES LESS THAN ('2026-12-01'),
    PARTITION p2026_12 VALUES LESS THAN ('2027-01-01'),
    PARTITION p2027_01 VALUES LESS THAN ('2027-02-01'),
    PARTITION p2027_02 VALUES LESS THAN ('2027-03-01'),
    PARTITION pmax VALUES LESS THAN (MAXVALUE)
);
-- Note: with occurred_at in the unique key, de-duplication relies on the agent's monotonic seq
-- plus agents.last_seq (EventIngestor filters seq <= last_seq), which remains exact.
