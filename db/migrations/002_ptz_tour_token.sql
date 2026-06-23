-- Add ONVIF preset tour token mapping for existing databases.
-- Apply with: psql "$VMS_DB_CONNINFO" -f db/migrations/002_ptz_tour_token.sql

ALTER TABLE ptz_tours
    ADD COLUMN IF NOT EXISTS tour_token varchar(128);

CREATE UNIQUE INDEX IF NOT EXISTS uq_ptz_tours_channel_token
    ON ptz_tours (channel_id, tour_token)
    WHERE tour_token IS NOT NULL AND tour_token <> '';

INSERT INTO schema_migrations (version)
VALUES ('002_ptz_tour_token')
ON CONFLICT (version) DO NOTHING;
