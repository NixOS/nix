-- Extension of the sql schema for content-addressing derivations.
-- Won't be loaded unless the experimental feature `ca-derivations`
-- is enabled

-- Why the `*V<N>` tables
--
-- We are trying to keep different versions of the experiment to have
-- completely independent extra schemas from one another. This will
-- enable people to switch between versions of the experiment (including
-- newer to older) without migrating between them, but at the cost
-- of having many abandoned tables lying around. Closer to the end of
-- the experiment, we'll provide guidance on how to clean this up.

create table if not exists BuildTraceV3 (
    id integer primary key autoincrement not null,
    drvPath text not null,
    outputName text not null, -- symbolic output id, usually "out"
    outputPath text not null,
    signatures text -- space-separated list
);

create index if not exists IndexBuildTraceV3 on BuildTraceV3(drvPath, outputName);
