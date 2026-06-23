-- VMS core schema
-- Apply with: psql "$VMS_DB_CONNINFO" -f db/migrations/001_core_schema.sql

BEGIN;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version varchar(64) PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS users (
    user_id bigserial PRIMARY KEY,
    login_id varchar(64) NOT NULL,
    name varchar(64) NOT NULL,
    pw_hash text NOT NULL,
    is_active boolean NOT NULL DEFAULT false,
    created_at timestamptz NOT NULL DEFAULT NOW(),
    last_login_at timestamptz
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_users_login_id ON users (login_id);

CREATE TABLE IF NOT EXISTS user_approvals (
    approval_id bigserial PRIMARY KEY,
    user_id bigint NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    status varchar(32) NOT NULL,
    requested_at timestamptz NOT NULL DEFAULT NOW(),
    reviewed_by bigint REFERENCES users(user_id) ON DELETE SET NULL,
    reviewed_at timestamptz,
    reject_reason text
);

CREATE INDEX IF NOT EXISTS ix_user_approvals_user_requested
    ON user_approvals (user_id, requested_at DESC, approval_id DESC);

CREATE INDEX IF NOT EXISTS ix_user_approvals_status_requested
    ON user_approvals (status, requested_at DESC, approval_id DESC);

CREATE TABLE IF NOT EXISTS roles (
    role_id bigserial PRIMARY KEY,
    role_code varchar(64) NOT NULL,
    role_name varchar(128) NOT NULL
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_roles_role_code ON roles (role_code);

CREATE TABLE IF NOT EXISTS user_roles (
    user_id bigint NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    role_id bigint NOT NULL REFERENCES roles(role_id) ON DELETE CASCADE,
    assinged_by bigint REFERENCES users(user_id) ON DELETE SET NULL,
    assinged_at timestamptz NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, role_id)
);

CREATE TABLE IF NOT EXISTS devices (
    device_id bigserial PRIMARY KEY,
    type smallint NOT NULL,
    uuid varchar(128) NOT NULL,
    addr varchar(128),
    media_xaddr varchar(256),
    ptz_xaddr varchar(256),
    name varchar(128),
    manufacturer varchar(128),
    model varchar(128),
    username varchar(128),
    password_enc text,
    created_at timestamptz NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_devices_uuid ON devices (uuid);

CREATE TABLE IF NOT EXISTS device_health (
    device_id bigint PRIMARY KEY REFERENCES devices(device_id) ON DELETE CASCADE,
    online_state smallint NOT NULL DEFAULT 0,
    video_state smallint NOT NULL DEFAULT 0,
    rtt_ms int,
    rssi int,
    updated_at timestamptz NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS device_health_log (
    id bigserial PRIMARY KEY,
    device_id bigint NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    ts timestamptz NOT NULL DEFAULT NOW(),
    conn_state smallint NOT NULL DEFAULT 0,
    video_state smallint NOT NULL DEFAULT 0,
    detail_json jsonb
);

CREATE INDEX IF NOT EXISTS ix_device_health_log_device_ts
    ON device_health_log (device_id, ts DESC);

CREATE TABLE IF NOT EXISTS channels (
    channel_id bigserial PRIMARY KEY,
    device_id bigint NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    channel_no int NOT NULL,
    name varchar(128),
    use_profile int,
    service_rtsp_url varchar(256),
    enabled boolean NOT NULL DEFAULT true
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_channels_device_channel
    ON channels (device_id, channel_no);

CREATE TABLE IF NOT EXISTS profiles (
    profile_id bigserial PRIMARY KEY,
    channel_id bigint NOT NULL REFERENCES channels(channel_id) ON DELETE CASCADE,
    profile_no int NOT NULL,
    name varchar(128),
    onvif_profile_token varchar(64),
    video_codec varchar(32),
    audio_codec varchar(32),
    cctv_rtsp_url varchar(256),
    enabled boolean NOT NULL DEFAULT true
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_profiles_channel_token
    ON profiles (channel_id, onvif_profile_token)
    WHERE onvif_profile_token IS NOT NULL AND onvif_profile_token <> '';

CREATE TABLE IF NOT EXISTS ptz_capabilities (
    channel_id bigint PRIMARY KEY REFERENCES channels(channel_id) ON DELETE CASCADE,
    supports_pan boolean,
    supports_tilt boolean,
    supports_zoom boolean,
    supports_preset boolean,
    supports_tour boolean,
    supports_linear boolean,
    min_pan numeric(8,4),
    max_pan numeric(8,4),
    min_tilt numeric(8,4),
    max_tilt numeric(8,4),
    min_zoom numeric(8,4),
    max_zoom numeric(8,4)
);

CREATE TABLE IF NOT EXISTS ptz_preset (
    preset_id bigserial PRIMARY KEY,
    channel_id bigint NOT NULL REFERENCES channels(channel_id) ON DELETE CASCADE,
    preset_token varchar(128),
    preset_name varchar(128),
    pan numeric(8,4),
    tilt numeric(8,4),
    zoom numeric(8,4),
    linear numeric(8,4),
    created_at timestamptz NOT NULL DEFAULT NOW(),
    updated_at timestamptz
);

CREATE INDEX IF NOT EXISTS ix_ptz_preset_channel
    ON ptz_preset (channel_id, preset_id);

CREATE UNIQUE INDEX IF NOT EXISTS uq_ptz_preset_channel_token
    ON ptz_preset (channel_id, preset_token)
    WHERE preset_token IS NOT NULL AND preset_token <> '';

CREATE TABLE IF NOT EXISTS ptz_tours (
    tour_id bigserial PRIMARY KEY,
    channel_id bigint NOT NULL REFERENCES channels(channel_id) ON DELETE CASCADE,
    tour_token varchar(128),
    tour_name varchar(128),
    repeat_mode varchar(32),
    schedule_enabled boolean,
    is_enabled boolean,
    created_at timestamptz NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_ptz_tours_channel
    ON ptz_tours (channel_id, tour_id);

CREATE UNIQUE INDEX IF NOT EXISTS uq_ptz_tours_channel_token
    ON ptz_tours (channel_id, tour_token)
    WHERE tour_token IS NOT NULL AND tour_token <> '';

CREATE TABLE IF NOT EXISTS ptz_tours_items (
    tour_item_id bigserial PRIMARY KEY,
    tour_id bigint NOT NULL REFERENCES ptz_tours(tour_id) ON DELETE CASCADE,
    preset_id bigint NOT NULL REFERENCES ptz_preset(preset_id) ON DELETE CASCADE,
    seq_no integer,
    stay_seconds integer,
    move_speed numeric(5,2)
);

CREATE INDEX IF NOT EXISTS ix_ptz_tours_items_tour_seq
    ON ptz_tours_items (tour_id, seq_no, tour_item_id);

INSERT INTO roles (role_code, role_name) VALUES
    ('ADMIN', 'Administrator'),
    ('OPERATOR', 'Operator'),
    ('VIEWER', 'Viewer')
ON CONFLICT (role_code) DO UPDATE SET
    role_name = EXCLUDED.role_name;

INSERT INTO schema_migrations (version)
VALUES ('001_core_schema')
ON CONFLICT (version) DO NOTHING;

COMMIT;
