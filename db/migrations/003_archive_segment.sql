-- Persist recording segment metadata required by Playback/HLS VOD.
-- Apply with: psql "$VMS_DB_CONNINFO" -v ON_ERROR_STOP=1 -f db/migrations/003_archive_segment.sql

BEGIN;

-- Keep this migration compatible with databases where ARCHIVE_SEGMENT was
-- created manually from the ERD before it became a managed migration.
CREATE TABLE IF NOT EXISTS archive_segment (
    seg_id bigserial PRIMARY KEY
);

ALTER TABLE archive_segment
    ADD COLUMN IF NOT EXISTS profile_id bigint,
    ADD COLUMN IF NOT EXISTS file_path text,
    ADD COLUMN IF NOT EXISTS start_ts timestamptz,
    ADD COLUMN IF NOT EXISTS end_ts timestamptz,
    ADD COLUMN IF NOT EXISTS file_size bigint,
    ADD COLUMN IF NOT EXISTS codec varchar(32),
    ADD COLUMN IF NOT EXISTS status varchar(16),
    ADD COLUMN IF NOT EXISTS has_gap boolean;

-- Migrate the former channel_id relationship to the channel's selected
-- profile. If use_profile is unset, use the first profile deterministically.
DO $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name = 'archive_segment'
          AND column_name = 'channel_id'
    ) THEN
        EXECUTE $sql$
            UPDATE archive_segment AS s
            SET profile_id = (
                SELECT p.profile_id
                FROM profiles AS p
                LEFT JOIN channels AS c ON c.channel_id = p.channel_id
                WHERE p.channel_id = s.channel_id
                ORDER BY
                    CASE
                        WHEN c.use_profile IS NOT NULL
                         AND p.profile_no = c.use_profile THEN 0
                        ELSE 1
                    END,
                    p.profile_no,
                    p.profile_id
                LIMIT 1
            )
            WHERE s.profile_id IS NULL
        $sql$;

        IF EXISTS (
            SELECT 1
            FROM archive_segment
            WHERE profile_id IS NULL
        ) THEN
            RAISE EXCEPTION
                'archive_segment contains channel rows that cannot be mapped to profiles';
        END IF;

        ALTER TABLE archive_segment
            DROP CONSTRAINT IF EXISTS archive_segment_channel_id_fkey;
        ALTER TABLE archive_segment
            DROP COLUMN channel_id;
    END IF;
END
$$;

-- A legacy row cannot be played without its source file. Abort instead of
-- silently creating unusable metadata.
DO $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM archive_segment
        WHERE profile_id IS NULL
           OR file_path IS NULL
           OR btrim(file_path) = ''
           OR start_ts IS NULL
    ) THEN
        RAISE EXCEPTION
            'archive_segment contains rows missing profile_id, file_path, or start_ts';
    END IF;
END
$$;

UPDATE archive_segment
SET status = CASE
    WHEN end_ts IS NULL THEN 'recording'
    ELSE 'completed'
END
WHERE status IS NULL OR btrim(status) = '';

UPDATE archive_segment
SET has_gap = false
WHERE has_gap IS NULL;

ALTER TABLE archive_segment
    ALTER COLUMN profile_id SET NOT NULL,
    ALTER COLUMN file_path SET NOT NULL,
    ALTER COLUMN start_ts SET NOT NULL,
    ALTER COLUMN status SET DEFAULT 'recording',
    ALTER COLUMN status SET NOT NULL,
    ALTER COLUMN has_gap SET DEFAULT false,
    ALTER COLUMN has_gap SET NOT NULL;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conrelid = 'archive_segment'::regclass
          AND conname = 'archive_segment_profile_id_fkey'
    ) THEN
        ALTER TABLE archive_segment
            ADD CONSTRAINT archive_segment_profile_id_fkey
            FOREIGN KEY (profile_id)
            REFERENCES profiles(profile_id);
    END IF;
END
$$;

CREATE UNIQUE INDEX IF NOT EXISTS uq_archive_segment_file_path
    ON archive_segment (file_path);

CREATE INDEX IF NOT EXISTS ix_archive_segment_profile_start
    ON archive_segment (profile_id, start_ts);

CREATE INDEX IF NOT EXISTS ix_archive_segment_status
    ON archive_segment (status);

INSERT INTO schema_migrations (version)
VALUES ('003_archive_segment')
ON CONFLICT (version) DO NOTHING;

COMMIT;
