-- Extension of the sql schema for content-addressed derivations.
-- Won't be loaded unless the experimental feature `ca-derivations`
-- is enabled

create table if not exists OutputMappings (
    id integer primary key autoincrement not null,
    drvPath text not null,
    outputName text not null, -- symbolic output id, usually "out"
    outputPath integer not null,
    foreign key (outputPath) references ValidPaths(id) on delete cascade
);

create index if not exists IndexOutputMappings on OutputMappings(outputPath);

create table if not exists DerivationOutputRefs (
    referrer integer not null,
    drvOutputReference integer,
    opaquePathReference integer,
    foreign key (referrer) references OutputMappings(id) on delete cascade,
    foreign key (drvOutputReference) references OutputMappings(id) on delete restrict,
    foreign key (opaquePathReference) references ValidPaths(id) on delete restrict,
    CHECK ((drvOutputReference is null AND opaquePathReference is not null)
      OR (opaquePathReference is null AND drvOutputReference is not null))
)
